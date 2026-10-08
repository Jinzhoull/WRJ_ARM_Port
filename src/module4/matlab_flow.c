#include "m4_matlab_flow.h"
#include "m4_module4.h"

typedef struct {
    uint8_t length, header0, header1, version, payload_start, payload_end;
    uint8_t type_position, length_position, has_timestamp, has_identifier;
} m4_matlab_schema_t;

uint32_t m4_matlab_stable_string_hash(const char *source_file)
{
    uint32_t hash = UINT32_C(2166136261);
    const unsigned char *p = (const unsigned char *)source_file;
    if (p == NULL) return hash;
    while (*p != 0U) {
        hash ^= (uint32_t)*p++;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static m4_matlab_schema_t m4_matlab_protocol_schema(const char *category)
{
    if (strcmp(category, "DJI_Control_Link") == 0)
        return (m4_matlab_schema_t){32U, 85U, 170U, 16U, 8U, 30U, 5U, 4U, 0U, 0U};
    if (strcmp(category, "DJI_Wideband_Link") == 0)
        return (m4_matlab_schema_t){48U, 165U, 90U, 33U, 12U, 46U, 4U, 5U, 1U, 0U};
    if (strcmp(category, "DJI_DroneID") == 0)
        return (m4_matlab_schema_t){56U, 250U, 188U, 48U, 20U, 54U, 4U, 5U, 1U, 1U};
    if (strcmp(category, "Autel_Control_Link") == 0)
        return (m4_matlab_schema_t){36U, 102U, 204U, 18U, 8U, 34U, 5U, 4U, 0U, 0U};
    if (strcmp(category, "Autel_Wideband_Link") == 0)
        return (m4_matlab_schema_t){52U, 195U, 60U, 36U, 12U, 50U, 4U, 5U, 1U, 0U};
    return (m4_matlab_schema_t){44U, 211U, 145U, 119U, 12U, 42U, 5U, 4U, 1U, 0U};
}

wrj_status_t m4_matlab_build_validation_frame(const char *category,
    const uint8_t *raw, uint16_t raw_count, const char *source_file,
    uint8_t sequence, uint8_t message_type, uint8_t state_index,
    uint32_t timestamp, uint32_t observation_index, m4_matlab_frame_t *out)
{
    m4_matlab_schema_t schema;
    uint32_t hash, i;
    uint16_t crc;
    if (category == NULL || source_file == NULL || out == NULL || state_index == 0U)
        return WRJ_ERR_ARGUMENT;
    schema = m4_matlab_protocol_schema(category);
    hash = m4_matlab_stable_string_hash(source_file);
    memset(out, 0, sizeof(*out));
    out->count = schema.length;
    memset(out->labels, M4_MATLAB_PAYLOAD, schema.length);
    out->bytes[0] = schema.header0;
    out->bytes[1] = schema.header1;
    out->bytes[2] = schema.version;
    out->labels[0] = M4_MATLAB_HEADER;
    out->labels[1] = M4_MATLAB_HEADER;
    out->labels[2] = M4_MATLAB_VERSION;
    out->labels[3] = M4_MATLAB_RESERVED;
    out->bytes[schema.length_position] = schema.length;
    out->labels[schema.length_position] = M4_MATLAB_LENGTH;
    out->bytes[schema.type_position] = message_type;
    out->labels[schema.type_position] = M4_MATLAB_TYPE;
    out->bytes[6] = sequence;
    out->labels[6] = M4_MATLAB_SEQUENCE;
    out->bytes[7] = (uint8_t)(state_index - 1U);
    out->labels[7] = M4_MATLAB_STATUS;
    if (schema.has_timestamp != 0U) {
        out->bytes[8] = (uint8_t)(timestamp >> 24U);
        out->bytes[9] = (uint8_t)(timestamp >> 16U);
        out->bytes[10] = (uint8_t)(timestamp >> 8U);
        out->bytes[11] = (uint8_t)timestamp;
        for (i = 8U; i < 12U; ++i) out->labels[i] = M4_MATLAB_TIMESTAMP;
    }
    if (schema.has_identifier != 0U) {
        for (i = 0U; i < 8U; ++i) {
            out->bytes[12U + i] = (uint8_t)(hash + i * 31U);
            out->labels[12U + i] = M4_MATLAB_IDENTIFIER;
        }
    }
    for (i = schema.payload_start; i < schema.payload_end; ++i) {
        const uint32_t p = i - schema.payload_start;
        const uint8_t base = raw_count != 0U && raw != NULL ? raw[p % raw_count] : 0U;
        const uint8_t mask = (uint8_t)((p + 1U) * 19U + observation_index * 23U +
                                       hash % 251U);
        out->bytes[i] = base ^ mask;
    }
    crc = m4_crc16_ccitt(out->bytes, (uint32_t)schema.length - 2U);
    out->bytes[schema.length - 2U] = (uint8_t)(crc >> 8U);
    out->bytes[schema.length - 1U] = (uint8_t)crc;
    out->labels[schema.length - 2U] = M4_MATLAB_CHECKSUM;
    out->labels[schema.length - 1U] = M4_MATLAB_CHECKSUM;
    if (strcmp(category, "Unknown_UAV_Link") == 0) {
        static const uint8_t renamed[] = {
            M4_MATLAB_OPAQUE_PAYLOAD, M4_MATLAB_CANDIDATE_HEADER,
            M4_MATLAB_PROTOCOL_FINGERPRINT, M4_MATLAB_CANDIDATE_UNKNOWN,
            M4_MATLAB_CANDIDATE_LENGTH, M4_MATLAB_CANDIDATE_TYPE,
            M4_MATLAB_CANDIDATE_SEQUENCE, M4_MATLAB_CANDIDATE_STATE,
            M4_MATLAB_CANDIDATE_TIME, M4_MATLAB_IDENTIFIER,
            M4_MATLAB_CANDIDATE_CHECKSUM
        };
        for (i = 0U; i < schema.length; ++i)
            if (out->labels[i] <= M4_MATLAB_CHECKSUM)
                out->labels[i] = renamed[out->labels[i]];
    }
    return WRJ_OK;
}

static void m4_matlab_fit_ascii(char *dst, size_t width, const char *src)
{
    const size_t n = src != NULL ? strlen(src) : 0U;
    if (n < width) {
        memset(dst, '0', width - n);
        if (n != 0U) memcpy(dst + width - n, src, n);
    } else memcpy(dst, src, width);
}

wrj_status_t m4_matlab_build_remoteid_frame(const m4_result_t *remote,
                                             m4_matlab_frame_t *out)
{
    char id[32], lat[32], lon[32], alt[32], speed[32], heading[32];
    uint16_t crc;
    if (remote == NULL || out == NULL) return WRJ_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->count = 55U;
    memcpy(out->bytes, "RID|", 4U);
    memset(out->labels, M4_MATLAB_DELIMITER, 55U);
    for (size_t i = 0U; i < 3U; ++i) out->labels[i] = M4_MATLAB_RID_PREFIX;
    for (size_t i = 4U; i < 13U; ++i) out->labels[i] = M4_MATLAB_UAS_ID;
    for (size_t i = 14U; i < 23U; ++i) out->labels[i] = M4_MATLAB_LATITUDE;
    for (size_t i = 24U; i < 34U; ++i) out->labels[i] = M4_MATLAB_LONGITUDE;
    for (size_t i = 35U; i < 41U; ++i) out->labels[i] = M4_MATLAB_ALTITUDE;
    for (size_t i = 42U; i < 47U; ++i) out->labels[i] = M4_MATLAB_SPEED;
    for (size_t i = 48U; i < 53U; ++i) out->labels[i] = M4_MATLAB_HEADING;
    out->labels[53] = M4_MATLAB_CHECKSUM;
    out->labels[54] = M4_MATLAB_CHECKSUM;
    snprintf(id, sizeof(id), "%s", remote->parse_complete != 0U ? remote->uas_id : "UAS000000");
    snprintf(lat, sizeof(lat), "%+09.5f", remote->parse_complete != 0U ? remote->latitude_deg : 0.0);
    snprintf(lon, sizeof(lon), "%+010.5f", remote->parse_complete != 0U ? remote->longitude_deg : 0.0);
    snprintf(alt, sizeof(alt), "%06.1f", remote->parse_complete != 0U ? remote->altitude_m : 0.0);
    snprintf(speed, sizeof(speed), "%05.1f", remote->parse_complete != 0U ? remote->speed_mps : 0.0);
    snprintf(heading, sizeof(heading), "%05.1f", remote->parse_complete != 0U ? remote->heading_deg : 0.0);
    m4_matlab_fit_ascii((char *)out->bytes + 4U, 9U, id);
    out->bytes[13] = '|';
    m4_matlab_fit_ascii((char *)out->bytes + 14U, 9U, lat);
    out->bytes[23] = '|';
    m4_matlab_fit_ascii((char *)out->bytes + 24U, 10U, lon);
    out->bytes[34] = '|';
    m4_matlab_fit_ascii((char *)out->bytes + 35U, 6U, alt);
    out->bytes[41] = '|';
    m4_matlab_fit_ascii((char *)out->bytes + 42U, 5U, speed);
    out->bytes[47] = '|';
    m4_matlab_fit_ascii((char *)out->bytes + 48U, 5U, heading);
    crc = m4_crc16_ccitt(out->bytes, 53U);
    out->bytes[53] = (uint8_t)(crc >> 8U);
    out->bytes[54] = (uint8_t)crc;
    return WRJ_OK;
}

const char *m4_matlab_label_name(uint8_t label)
{
    static const char *const names[] = {
        "Payload", "Header", "Version_Fingerprint", "Reserved", "Length",
        "Type", "Sequence", "Status", "Timestamp", "Identifier_Fingerprint",
        "Checksum", "RID_Prefix", "Delimiter", "UAS_ID", "Latitude",
        "Longitude", "Altitude", "Speed", "Heading", "Candidate_Header",
        "Protocol_Fingerprint", "Candidate_Length", "Candidate_Type",
        "Candidate_Sequence", "Candidate_State", "Candidate_Time", "Opaque_Payload",
        "Candidate_Checksum", "Candidate_Unknown"
    };
    return label < sizeof(names) / sizeof(names[0]) ? names[label] : "Unknown";
}

int m4_matlab_crc16_recover_unique_single_bit(uint8_t *bytes, uint16_t count)
{
    uint16_t position, best_position = 0U;
    uint8_t bit, best_bit = 0U;
    unsigned matches = 0U;
    if (bytes == NULL || count < 3U) return 0;
    if (m4_crc16_ccitt(bytes, count - 2U) ==
        (uint16_t)(((uint16_t)bytes[count - 2U] << 8U) | bytes[count - 1U])) return 0;
    for (position = 0U; position < count; ++position) {
        for (bit = 0U; bit < 8U; ++bit) {
            bytes[position] ^= (uint8_t)(1U << bit);
            if (m4_crc16_ccitt(bytes, count - 2U) ==
                (uint16_t)(((uint16_t)bytes[count - 2U] << 8U) | bytes[count - 1U])) {
                ++matches; best_position = position; best_bit = bit;
            }
            bytes[position] ^= (uint8_t)(1U << bit);
            if (matches > 1U) return 0;
        }
    }
    if (matches == 1U) bytes[best_position] ^= (uint8_t)(1U << best_bit);
    return matches == 1U;
}

void m4_matlab_rng_seed(m4_matlab_rng_t *rng, uint32_t seed)
{
    uint32_t i;
    if (rng == NULL) return;
    rng->state[0] = seed;
    for (i = 1U; i < 624U; ++i)
        rng->state[i] = UINT32_C(1812433253) *
            (rng->state[i - 1U] ^ (rng->state[i - 1U] >> 30U)) + i;
    rng->index = 624U;
}

static uint32_t m4_matlab_rng_uint32(m4_matlab_rng_t *rng)
{
    uint32_t y;
    if (rng->index >= 624U) {
        uint32_t i;
        for (i = 0U; i < 624U; ++i) {
            const uint32_t merged = (rng->state[i] & UINT32_C(0x80000000)) |
                                    (rng->state[(i + 1U) % 624U] & UINT32_C(0x7fffffff));
            rng->state[i] = rng->state[(i + 397U) % 624U] ^ (merged >> 1U) ^
                            ((merged & 1U) ? UINT32_C(0x9908b0df) : 0U);
        }
        rng->index = 0U;
    }
    y = rng->state[rng->index++];
    y ^= y >> 11U;
    y ^= (y << 7U) & UINT32_C(0x9d2c5680);
    y ^= (y << 15U) & UINT32_C(0xefc60000);
    y ^= y >> 18U;
    return y;
}

double m4_matlab_rand(m4_matlab_rng_t *rng)
{
    const uint32_t a = m4_matlab_rng_uint32(rng) >> 5U;
    const uint32_t b = m4_matlab_rng_uint32(rng) >> 6U;
    return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}

void m4_matlab_apply_validation_channel(m4_matlab_frame_t *frame,
    double sync_confidence, const char *category, m4_matlab_rng_t *rng)
{
    uint8_t flip[64];
    uint32_t i;
    double probability;
    if (frame == NULL || category == NULL || rng == NULL) return;
    if (!isfinite(sync_confidence)) sync_confidence = 0.50;
    probability = .0015 + .0030 * (1.0 - fmax(0.0, fmin(1.0, sync_confidence)));
    if (strcmp(category, "Unknown_UAV_Link") == 0) probability += .0005;
    for (i = 0U; i < frame->count; ++i)
        flip[i] = (uint8_t)(m4_matlab_rand(rng) < probability);
    for (i = 0U; i < frame->count; ++i)
        if (flip[i] != 0U) {
            const uint8_t bit = (uint8_t)fmin(7.0, floor(8.0 * m4_matlab_rand(rng)));
            frame->bytes[i] ^= (uint8_t)(1U << bit);
        }
}

const char *m4_matlab_category(const m3_result_t *m3)
{
    switch (m3->profile) {
        case WRJ_PROFILE_DRONEID_ZC: return "DJI_DroneID";
        case WRJ_PROFILE_DJI_WIDEBAND_CP: return "DJI_Wideband_Link";
        case WRJ_PROFILE_AUTEL_WIDEBAND_CP: return "Autel_Wideband_Link";
        case WRJ_PROFILE_AUTEL_CONTROL_CP: return "Autel_Control_Link";
        case WRJ_PROFILE_REMOTEID_BLE: return "RemoteID_BLE";
        case WRJ_PROFILE_DJI_CONTROL_BLIND:
            return strstr(m3->profile_name, "Template_Mismatch") != NULL ?
                "Unknown_UAV_Link" : "DJI_Control_Link";
        default: return "Unknown_UAV_Link";
    }
}

wrj_status_t m4_matlab_attach_observations(const wrj_candidate_t *candidate,
    const m3_result_t *m3, m4_result_t *m4, m4_matlab_rng_t *rng)
{
    static const uint8_t message_pattern[20] = {
        1U,3U,2U,1U,2U,3U,3U,1U,2U,2U,
        3U,1U,3U,2U,1U,1U,2U,3U,1U,2U
    };
    const char *category;
    uint32_t hash, physical_count, observations, k;
    if (candidate == NULL || m3 == NULL || m4 == NULL || rng == NULL)
        return WRJ_ERR_ARGUMENT;
    category = m4_matlab_category(m3);
    hash = m4_matlab_stable_string_hash(candidate->source_file);
    physical_count = m3->num_frames;
    if (physical_count == 0U) return WRJ_OK;
    observations = WRJ_MIN(WRJ_MATLAB_MAX_OBSERVATIONS, WRJ_MAX(16U, physical_count));
    m4->matlab_observation_count = (uint8_t)observations;
    for (k = 0U; k < observations; ++k) {
        const uint32_t physical = k % physical_count;
        const uint32_t byte_index = m4->packet_count == 0U ? 0U :
                                    physical % m4->packet_count;
        const uint8_t *raw = m4->packet_count == 0U ? NULL : m4->packet_bytes[byte_index];
        const uint16_t raw_count = m4->packet_count == 0U ? 0U : m4->packet_lengths[byte_index];
        const uint8_t state = (uint8_t)((k + hash % 6U) % 6U + 1U);
        const uint8_t type = message_pattern[(k + hash % 5U) % 20U];
        const uint8_t sequence = (uint8_t)(hash % 181U + k);
        const uint32_t timestamp = (hash % 100000U) * 1000U + (k + 1U) * 1000U;
        const float confidence = m3->frame_confidence[physical];
        m4_matlab_frame_t frame;
        wrj_status_t status;
        if (strcmp(category, "RemoteID_BLE") == 0)
            status = m4_matlab_build_remoteid_frame(m4, &frame);
        else
            status = m4_matlab_build_validation_frame(category, raw, raw_count,
                candidate->source_file, sequence, type, state, timestamp, k + 1U, &frame);
        if (status != WRJ_OK) return status;
        m4_matlab_apply_validation_channel(&frame, confidence, category, rng);
        m4_matlab_crc16_recover_unique_single_bit(frame.bytes, frame.count);
        m4->matlab_observation_length[k] = frame.count;
        m4->matlab_observation_physical_index[k] = (uint8_t)(physical + 1U);
        m4->matlab_observation_state_index[k] = state;
        m4->matlab_observation_message_type[k] = type;
        m4->matlab_observation_sequence[k] = sequence;
        m4->matlab_observation_timestamp[k] = timestamp;
        memcpy(m4->matlab_observation_bytes[k], frame.bytes, frame.count);
        memcpy(m4->matlab_observation_labels[k], frame.labels, frame.count);
    }
    return WRJ_OK;
}
