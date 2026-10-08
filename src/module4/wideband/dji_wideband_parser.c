#include "m4_module4.h"
#include "wrj/module4/wideband_assembler.h"

/* This is the documented Module12 engineering proxy format. Its marker and
 * fields are not asserted to describe a vendor-private DJI protocol. */
#define M4_DJI_PROXY_SYNC_0 0xD1U
#define M4_DJI_PROXY_SYNC_1 0xB4U
#define M4_DJI_PROXY_VERSION_TYPE 0x22U
#define M4_DJI_PROXY_HEADER_BYTES 11U
#define M4_DJI_PROXY_PAYLOAD_TAG 0xD0U

static uint8_t m4_proxy_byte(const uint8_t *bytes, size_t index, uint8_t mask)
{
    return (uint8_t)(bytes[index] ^ mask);
}

static uint16_t m4_proxy_crc16(const uint8_t *bytes, size_t count, uint8_t mask)
{
    uint16_t crc = 0xFFFFU;
    size_t index;
    if (mask == 0U) return m4_crc16_ccitt(bytes, (uint32_t)count);
    for (index = 0U; index < count; ++index) {
        uint32_t bit;
        crc ^= (uint16_t)((uint16_t)m4_proxy_byte(bytes, index, mask) << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc & 0x8000U) != 0U ?
                (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U));
    }
    return crc;
}

int m4_decode_dji_wideband_proxy_frame(const uint8_t *bytes, size_t count,
                                       m4_dji_wideband_parse_t *parsed)
{
    size_t offset;
    if (bytes == NULL || parsed == NULL) return 0;
    for (offset = 0U; offset + M4_DJI_PROXY_HEADER_BYTES + 2U <= count; ++offset) {
        uint32_t polarity;
        for (polarity = 0U; polarity < 2U; ++polarity) {
            const uint8_t mask = polarity != 0U ? 0xFFU : 0U;
            const uint8_t *frame = bytes + offset;
            const size_t available = count - offset;
            uint16_t payload_length;
            size_t frame_length;
            uint16_t expected_crc, received_crc;
            if (m4_proxy_byte(frame, 0U, mask) != M4_DJI_PROXY_SYNC_0 ||
                m4_proxy_byte(frame, 1U, mask) != M4_DJI_PROXY_SYNC_1 ||
                m4_proxy_byte(frame, 2U, mask) != M4_DJI_PROXY_VERSION_TYPE) continue;
            ++parsed->header_candidates;
            payload_length = (uint16_t)(((uint16_t)m4_proxy_byte(frame, 4U, mask) << 8U) |
                                        m4_proxy_byte(frame, 5U, mask));
            frame_length = M4_DJI_PROXY_HEADER_BYTES + (size_t)payload_length + 2U;
            if (payload_length < 8U || frame_length > available) continue;
            ++parsed->complete_candidates;
            /* Redundant fields in the proxy payload protect against treating
             * a chance CRC collision as a decoded protocol frame. */
            if (m4_proxy_byte(frame, 11U, mask) != M4_DJI_PROXY_PAYLOAD_TAG ||
                m4_proxy_byte(frame, 13U, mask) != 0U ||
                m4_proxy_byte(frame, 14U, mask) != 1U ||
                m4_proxy_byte(frame, 15U, mask) != m4_proxy_byte(frame, 4U, mask) ||
                m4_proxy_byte(frame, 16U, mask) != m4_proxy_byte(frame, 5U, mask) ||
                m4_proxy_byte(frame, 17U, mask) != m4_proxy_byte(frame, 6U, mask)) continue;
            ++parsed->crc16_checked;
            expected_crc = m4_proxy_crc16(frame, frame_length - 2U, mask);
            received_crc = (uint16_t)(((uint16_t)m4_proxy_byte(frame, frame_length - 2U, mask) << 8U) |
                                      m4_proxy_byte(frame, frame_length - 1U, mask));
            if (expected_crc != received_crc) continue;
            ++parsed->crc16_passed;
            ++parsed->crc_valid_count;
            parsed->complete = 1U;
            parsed->polarity_inverted = (uint8_t)polarity;
            parsed->byte_offset = (uint16_t)offset;
            parsed->frame_id = m4_proxy_byte(frame, 3U, mask);
            parsed->message_type = (uint8_t)(m4_proxy_byte(frame, 2U, mask) & 0x0FU);
            parsed->state_code = m4_proxy_byte(frame, 6U, mask);
            parsed->device_id = (uint16_t)(((uint16_t)m4_proxy_byte(frame, 7U, mask) << 8U) |
                                           m4_proxy_byte(frame, 8U, mask));
            parsed->timestamp_counter = (uint16_t)(((uint16_t)m4_proxy_byte(frame, 9U, mask) << 8U) |
                                                  m4_proxy_byte(frame, 10U, mask));
            parsed->payload_length = payload_length;
            snprintf(parsed->status, sizeof(parsed->status), "proxy_crc_verified");
            return 1;
        }
    }
    return 0;
}

static void m4_proxy_field(m4_result_t *result, uint16_t offset, uint16_t length,
                           const char *semantic, double value)
{
    m4_field_t *field;
    if (result->field_count >= WRJ_MAX_FIELDS) return;
    field = &result->fields[result->field_count++];
    memset(field, 0, sizeof(*field));
    field->field_id = result->field_count;
    field->offset = offset;
    field->length = length;
    field->numeric_value = value;
    field->confidence = .95f;
    snprintf(field->field_type, sizeof(field->field_type), "proxy_crc16_field");
    snprintf(field->semantic, sizeof(field->semantic), "%s", semantic);
}

void m4_parse_dji_wideband_proxy(const wrj_candidate_t *candidate,
                                 const m3_result_t *m3, m4_result_t *result)
{
    m4_dji_wideband_parse_t *parsed;
    if (candidate == NULL || m3 == NULL || result == NULL) return;
    parsed = &result->dji_wideband;
    memset(parsed, 0, sizeof(*parsed));
    parsed->attempted = 1U;
    parsed->observation_count = result->packet_count;
    if (strcmp(candidate->parser_template_id, "DJI_OCUSYNC_PROXY_WIDEBAND_V2") != 0) {
        snprintf(parsed->status, sizeof(parsed->status), "proxy_template_not_indicated");
        return;
    }
    for (uint16_t observation = 0U; observation < result->packet_count; ++observation) {
        const uint16_t length = result->packet_lengths[observation];
        parsed->max_observation_bytes = WRJ_MAX(parsed->max_observation_bytes, length);
    }
    /* The continuity gate precedes strict header, length and CRC validation. */
    m4_assemble_dji_wideband(m3, result);
    if (parsed->complete == 0U) {
        const char *reason = parsed->header_candidates == 0U ? "no_verified_proxy_header" :
            (parsed->complete_candidates == 0U ? "incomplete_symbol_observation" :
             "proxy_structure_or_crc_failed");
        snprintf(parsed->status, sizeof(parsed->status), "%s", reason);
        return;
    }
    /* Only the engineering proxy's documented fields are assigned. Position,
     * velocity and heading do not exist in this logical frame. */
    result->field_count = 0U;
    m4_proxy_field(result, 0U, 2U, "PROXY_SYNC", 0xD1B4);
    m4_proxy_field(result, 2U, 1U, "PROXY_VERSION_TYPE", 0x22);
    m4_proxy_field(result, 3U, 1U, "FRAME_ID", parsed->frame_id);
    m4_proxy_field(result, 4U, 2U, "PAYLOAD_LENGTH", parsed->payload_length);
    m4_proxy_field(result, 6U, 1U, "PROXY_STATE", parsed->state_code);
    m4_proxy_field(result, 7U, 2U, "PROXY_DEVICE_ID", parsed->device_id);
    m4_proxy_field(result, 9U, 2U, "TIMESTAMP_COUNTER", parsed->timestamp_counter);
    m4_proxy_field(result, (uint16_t)(M4_DJI_PROXY_HEADER_BYTES + parsed->payload_length),
                   2U, "CRC16_CCITT", 1.0);
    result->crc_valid_count = parsed->crc_valid_count;
    result->crc_checked = 1U;
    result->crc_passed = 1U;
    result->parse_complete = 1U;
    result->semantic_confidence = .95f;
    result->status = M4_STATUS_PARSED;
    snprintf(result->parse_status, sizeof(result->parse_status), "proxy_frame_complete");
    snprintf(result->protocol_type, sizeof(result->protocol_type), "DJI_OCUSYNC_PROXY_WIDEBAND_V2");
    snprintf(result->crc_candidate_status, sizeof(result->crc_candidate_status), "proxy_crc16_verified");
    snprintf(result->diagnostics, sizeof(result->diagnostics),
             "proxy_crc_verified;observation=%u;byte_offset=%u;polarity_inverted=%u",
             parsed->source_observation, parsed->byte_offset, parsed->polarity_inverted);
}
