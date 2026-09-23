#include "m4_module4.h"

static m4_field_t *m4_add_field(m4_result_t *result, uint16_t offset, uint16_t length,
                                const char *kind, const char *semantic, float confidence)
{
    m4_field_t *field;
    if (result->field_count >= WRJ_MAX_FIELDS || length == 0U) return NULL;
    field = &result->fields[result->field_count++];
    memset(field, 0, sizeof(*field));
    field->field_id = result->field_count;
    field->offset = offset;
    field->length = length;
    field->confidence = wrj_clip01(confidence);
    snprintf(field->field_type, sizeof(field->field_type), "%s", kind);
    snprintf(field->semantic, sizeof(field->semantic), "%s", semantic);
    return field;
}

void m4_parse_structural_fields(m4_result_t *result)
{
    uint16_t length = result->byte_count;
    m4_field_t *field;
    if (length == 0U) return;
    if (result->packet_count >= 2U) {
        uint16_t position = 0U, fixed = 0U, changing = 0U;
        int prefix_open = 1;
        const uint16_t limit = result->packet_lengths[0];
        m4_add_field(result, 0U, limit, "frame_extent_candidate", "UNKNOWN", .45f);
        for (position = 0U; position < limit; ++position) {
            uint16_t p;
            int same = 1;
            for (p = 1U; p < result->packet_count; ++p) {
                if (position >= result->packet_lengths[p] ||
                    result->packet_bytes[p][position] != result->packet_bytes[0][position]) {
                    same = 0; break;
                }
            }
            if (same == 0) prefix_open = 0;
            if (prefix_open != 0) ++fixed; else ++changing;
        }
        if (fixed != 0U) m4_add_field(result, 0U, fixed,
                                      "repeated_byte_positions", "UNKNOWN", .35f);
        if (changing != 0U) m4_add_field(result, fixed, changing,
                                         "payload_candidate_unverified", "UNKNOWN", .30f);
        result->semantic_confidence = 0.0f;
        if (strncmp(result->decoder_method, "blind_", 6U) == 0)
            snprintf(result->diagnostics, sizeof(result->diagnostics),
                     "blind_bits_unframed;frames=%u;fixed_positions=%u;changing_positions=%u",
                     result->packet_count, fixed, changing);
        return;
    }
    /* These bytes are demodulator output, not a proven protocol frame.
     * Give only structural hypotheses and leave semantics unassigned. */
    field = m4_add_field(result, 0U, WRJ_MIN(2U, length), "candidate_header",
                         "UNKNOWN", result->byte_recovery_confidence * 0.6f);
    if (field != NULL) field->numeric_value = result->bytes[0];
    if (length > 2U) m4_add_field(result, 2U, (uint16_t)(length - 2U),
                                  "candidate_payload", "UNKNOWN",
                                  result->byte_recovery_confidence * 0.5f);
    result->semantic_confidence = 0.0f;
}

static int32_t m4_le_i32(const uint8_t *p)
{
    const uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
                       ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
    return (int32_t)v;
}

static void m4_ascii_id(char *output, size_t capacity, const uint8_t *bytes, size_t count)
{
    size_t i, used = 0U;
    for (i = 0U; i < count && used + 1U < capacity; ++i) {
        if (bytes[i] == 0U) break;
        if (bytes[i] >= 32U && bytes[i] <= 126U) output[used++] = (char)bytes[i];
    }
    output[used] = '\0';
}

void m4_parse_remoteid_messages(const m4_ble_packet_t *packets, uint32_t count,
                                m4_result_t *result)
{
    uint32_t i;
    float confidence = 0.0f;
    int system_added = 0;
    for (i = 0U; i < count; ++i) {
        const uint8_t *message = packets[i].message;
        const uint8_t type = packets[i].message_type;
        m4_field_t *field;
        if (packets[i].pdu[12] != 0x0DU || type > 7U) continue;
        result->message_types_mask |= (uint8_t)(1U << type);
        confidence = WRJ_MAX(confidence, packets[i].confidence);
        if (type == 0U && result->uas_id[0] == '\0') {
            m4_ascii_id(result->uas_id, sizeof(result->uas_id), message + 2U, 20U);
            field = m4_add_field(result, 16U, 20U, "basic_id", "UAS_ID", packets[i].confidence);
            if (field != NULL) snprintf(field->string_value, sizeof(field->string_value), "%s", result->uas_id);
        } else if (type == 1U && !isfinite(result->latitude_deg)) {
            const uint16_t altitude_raw = (uint16_t)((uint16_t)message[15] | ((uint16_t)message[16] << 8U));
            const uint8_t flags = message[1];
            const double latitude = (double)m4_le_i32(message + 5U) / 1.0e7;
            const double longitude = (double)m4_le_i32(message + 9U) / 1.0e7;
            const double altitude = (double)altitude_raw * .5 - 1000.0;
            if (fabs(latitude) > 90.0 || fabs(longitude) > 180.0 || altitude < -1000.0) continue;
            result->latitude_deg = latitude;
            result->longitude_deg = longitude;
            result->altitude_m = altitude;
            result->heading_deg = fmod((double)message[2] + 180.0 * ((flags >> 1U) & 1U), 360.0);
            result->speed_mps = (flags & 1U) != 0U ? 63.75 + .75 * message[3] : .25 * message[3];
            field = m4_add_field(result, 19U, 4U, "location", "LATITUDE", packets[i].confidence);
            if (field != NULL) field->numeric_value = result->latitude_deg;
            field = m4_add_field(result, 23U, 4U, "location", "LONGITUDE", packets[i].confidence);
            if (field != NULL) field->numeric_value = result->longitude_deg;
            field = m4_add_field(result, 29U, 2U, "location", "ALTITUDE", packets[i].confidence);
            if (field != NULL) field->numeric_value = result->altitude_m;
            field = m4_add_field(result, 16U, 2U, "location", "HEADING", packets[i].confidence);
            if (field != NULL) field->numeric_value = result->heading_deg;
            field = m4_add_field(result, 17U, 1U, "location", "SPEED", packets[i].confidence);
            if (field != NULL) field->numeric_value = result->speed_mps;
        } else if (type == 4U && system_added == 0) {
            const double operator_latitude = (double)m4_le_i32(message + 2U) / 1.0e7;
            const double operator_longitude = (double)m4_le_i32(message + 6U) / 1.0e7;
            if (fabs(operator_latitude) <= 90.0 && fabs(operator_longitude) <= 180.0) {
                field = m4_add_field(result, 16U, 4U, "system", "OPERATOR_LATITUDE",
                                     packets[i].confidence);
                if (field != NULL) field->numeric_value = operator_latitude;
                field = m4_add_field(result, 20U, 4U, "system", "OPERATOR_LONGITUDE",
                                     packets[i].confidence);
                if (field != NULL) field->numeric_value = operator_longitude;
                system_added = 1;
            }
        } else if (type == 5U && result->operator_id[0] == '\0') {
            m4_ascii_id(result->operator_id, sizeof(result->operator_id), message + 2U, 20U);
            field = m4_add_field(result, 16U, 20U, "operator_id", "OPERATOR_ID", packets[i].confidence);
            if (field != NULL) snprintf(field->string_value, sizeof(field->string_value), "%s", result->operator_id);
        }
    }
    result->semantic_confidence = confidence;
    result->parse_complete = (uint8_t)(result->uas_id[0] != '\0' &&
                                       isfinite(result->latitude_deg) &&
                                       isfinite(result->longitude_deg));
    result->status = result->parse_complete != 0U ? M4_STATUS_PARSED : M4_STATUS_PARTIAL;
    snprintf(result->parse_status, sizeof(result->parse_status), "%s",
             result->parse_complete != 0U ? "complete" : "partial_parse");
}
