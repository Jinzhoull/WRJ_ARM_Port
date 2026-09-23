#include "m4_module4.h"

uint32_t m4_crc24_ble(const uint8_t *bits, uint32_t bit_count)
{
    uint32_t crc = 0x555555U, i;
    for (i = 0U; i < bit_count; ++i) {
        const uint32_t feedback = ((crc >> 23U) & 1U) ^ (uint32_t)(bits[i] != 0U);
        crc = (crc << 1U) & 0xFFFFFFU;
        if (feedback != 0U) crc ^= 0x00065BU;
    }
    return crc;
}

void m4_ble_whiten(uint8_t *bits, uint32_t bit_count, uint8_t channel)
{
    uint8_t state[7];
    uint32_t i;
    state[0] = 1U;
    for (i = 0U; i < 6U; ++i) state[i + 1U] = (uint8_t)((channel >> (5U - i)) & 1U);
    for (i = 0U; i < bit_count; ++i) {
        const uint8_t mask = state[6];
        uint32_t q;
        bits[i] ^= mask;
        for (q = 6U; q > 0U; --q) state[q] = state[q - 1U];
        state[0] = mask;
        state[4] ^= mask;
    }
}

uint16_t m4_crc16_ccitt(const uint8_t *bytes, uint32_t count)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    for (i = 0U; i < count; ++i) {
        uint32_t b;
        crc ^= (uint16_t)((uint16_t)bytes[i] << 8U);
        for (b = 0U; b < 8U; ++b)
            crc = (uint16_t)((crc & 0x8000U) != 0U ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U));
    }
    return crc;
}

uint16_t m4_pack_bits_msb(const uint8_t *bits, uint32_t bit_count,
                          uint8_t *bytes, uint16_t capacity)
{
    uint32_t i;
    const uint32_t usable = WRJ_MIN(bit_count / 8U, (uint32_t)capacity);
    for (i = 0U; i < usable; ++i) {
        uint32_t b;
        bytes[i] = 0U;
        for (b = 0U; b < 8U; ++b) bytes[i] = (uint8_t)((bytes[i] << 1U) | (bits[i * 8U + b] & 1U));
    }
    return (uint16_t)usable;
}

void m4_qpsk_decide(float re, float im, uint8_t *bit_i, uint8_t *bit_q,
                    float *confidence_i, float *confidence_q)
{
    const float scale = fabsf(re) + fabsf(im) + 1.0e-9f;
    *bit_i = (uint8_t)(re < 0.0f);
    *bit_q = (uint8_t)(im < 0.0f);
    *confidence_i = wrj_clip01(2.0f * fabsf(re) / scale);
    *confidence_q = wrj_clip01(2.0f * fabsf(im) / scale);
}

wrj_status_t m4_recover_blind_bytes(const wrj_cf32_t *iq, uint32_t count,
                                    const m3_result_t *m3, const m4_config_t *config,
                                    m4_workspace_t *workspace, m4_result_t *result)
{
    uint32_t f, j, out = 0U;
    double score = 0.0;
    (void)workspace;
    for (f = 0U; f < m3->num_frames && f < 8U && out < config->max_bytes; ++f) {
        const uint32_t start = m3->frame_start_samples_0based[f];
        const uint32_t span = WRJ_MIN(256U, m3->frame_length_samples);
        const uint32_t frame_begin = out;
        if (start + span >= count || span < 16U) continue;
        for (j = 8U; j < span && out < WRJ_MIN(config->max_bytes, WRJ_MAX_BYTES); j += 8U) {
            uint32_t b;
            uint8_t value = 0U;
            float q = 0.0f;
            for (b = 0U; b < 8U; ++b) {
                const wrj_cf32_t a = iq[start + j + b - 1U], z = iq[start + j + b];
                const float dot = a.re * z.re + a.im * z.im;
                const float cross = a.re * z.im - a.im * z.re;
                const float magnitude = sqrtf(dot * dot + cross * cross) + 1.0e-9f;
                value = (uint8_t)((value << 1U) | (cross < 0.0f));
                q += fabsf(cross) / magnitude;
            }
            result->bytes[out] = value;
            result->byte_confidence[out] = q / 8.0f;
            score += result->byte_confidence[out];
            ++out;
        }
        if (out > frame_begin && result->packet_count < WRJ_MAX_PACKETS) {
            const uint16_t packet_index = result->packet_count++;
            const uint16_t packet_length = (uint16_t)(out - frame_begin);
            result->packet_lengths[packet_index] = packet_length;
            memcpy(result->packet_bytes[packet_index], result->bytes + frame_begin,
                   packet_length);
        }
    }
    result->byte_count = (uint16_t)out;
    if (out == 0U) return WRJ_ERR_DATA;
    result->byte_recovery_confidence = (float)(score / (double)out);
    result->bit_error_estimate = 0.5f * (1.0f - result->byte_recovery_confidence);
    snprintf(result->decoder_method, sizeof(result->decoder_method), "blind_differential_phase_structural_only");
    snprintf(result->crc_candidate_status, sizeof(result->crc_candidate_status), "not_verified");
    snprintf(result->diagnostics, sizeof(result->diagnostics), "blind_bits_unframed;semantics_unknown");
    return WRJ_OK;
}
