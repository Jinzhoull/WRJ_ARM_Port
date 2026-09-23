#include "m4_module4.h"

static void m4_ble_filter(const wrj_cf32_t *iq, wrj_cf32_t *output,
                          uint32_t count, uint32_t sps)
{
    const uint32_t radius = WRJ_MAX(1U, sps / 7U);
    double sum_re = 0.0, sum_im = 0.0;
    uint32_t left = 0U, right = 0U, i;
    for (i = 0U; i < count; ++i) {
        const uint32_t wanted_right = WRJ_MIN(count - 1U, i + radius);
        const uint32_t wanted_left = i > radius ? i - radius : 0U;
        while (right <= wanted_right) {
            sum_re += iq[right].re; sum_im += iq[right].im; ++right;
        }
        while (left < wanted_left) {
            sum_re -= iq[left].re; sum_im -= iq[left].im; ++left;
        }
        output[i].re = (float)(sum_re / (double)(right - left));
        output[i].im = (float)(sum_im / (double)(right - left));
    }
}

static int m4_ble_try_packet(const wrj_cf32_t *iq, uint32_t count,
                             uint32_t start, uint32_t sps, int32_t shift,
                             uint32_t half_window, m4_ble_packet_t *packet)
{
    static const uint8_t access[4] = {0xD6U, 0xBEU, 0x89U, 0x8EU};
    float soft[376];
    uint8_t bits[336];
    int8_t expected[40];
    double mean_e = 0.0, mean_s = 0.0, covariance = 0.0, variance = 0.0, slope, intercept;
    uint32_t bit, crc_received = 0U, length, pdu_bits;
    const uint32_t lag = WRJ_MAX(1U, sps / 4U);
    for (bit = 0U; bit < 40U; ++bit) {
        expected[bit] = bit < 8U ? (int8_t)((bit & 1U) != 0U ? 1 : -1) :
            (int8_t)(((access[(bit - 8U) / 8U] >> ((bit - 8U) & 7U)) & 1U) != 0U ? 1 : -1);
    }
    for (bit = 0U; bit < 376U; ++bit) {
        const int64_t center = (int64_t)start + shift + (int64_t)(sps / 2U) +
                               (int64_t)bit * (int64_t)sps;
        const int64_t low = center - (int64_t)half_window;
        const int64_t high = center + (int64_t)half_window;
        double phase = 0.0;
        int64_t j;
        if (low < 0 || high + (int64_t)lag >= (int64_t)count) return 0;
        for (j = low; j <= high; ++j) {
            const wrj_cf32_t a = iq[j], b = iq[j + lag];
            phase += atan2((double)a.re * b.im - (double)a.im * b.re,
                           (double)a.re * b.re + (double)a.im * b.im) / (double)lag;
        }
        soft[bit] = (float)(phase / (double)(2U * half_window + 1U));
    }
    for (bit = 0U; bit < 40U; ++bit) { mean_e += expected[bit]; mean_s += soft[bit]; }
    mean_e /= 40.0; mean_s /= 40.0;
    for (bit = 0U; bit < 40U; ++bit) {
        const double e = expected[bit] - mean_e;
        covariance += e * ((double)soft[bit] - mean_s);
        variance += e * e;
    }
    if (variance < 1.0e-9) return 0;
    slope = covariance / variance;
    if (fabs(slope) < 1.0e-9) return 0;
    intercept = mean_s - slope * mean_e;
    for (bit = 0U; bit < 336U; ++bit)
        bits[bit] = (uint8_t)((((double)soft[40U + bit] - intercept) / slope) > 0.0);
    m4_ble_whiten(bits, 336U, 38U);
    length = 0U;
    for (bit = 0U; bit < 8U; ++bit) length |= (uint32_t)bits[8U + bit] << bit;
    length &= 63U;
    if (length < 6U || length > 37U) return 0;
    pdu_bits = (2U + length) * 8U;
    for (bit = 0U; bit < 24U; ++bit)
        crc_received = (crc_received << 1U) | bits[pdu_bits + bit];
    if (m4_crc24_ble(bits, pdu_bits) != crc_received) return 0;
    memset(packet, 0, sizeof(*packet));
    packet->start_sample = start;
    packet->confidence = wrj_clip01((float)(fabs(slope) / (fabs(slope) + fabs(intercept) + 0.001)));
    for (bit = 0U; bit < pdu_bits / 8U; ++bit) {
        uint32_t b;
        for (b = 0U; b < 8U; ++b)
            packet->pdu[bit] |= (uint8_t)(bits[bit * 8U + b] << b);
    }
    /* Manufacturer AD type 0xFF, ODID service marker 0xFFFA in the
     * receiver's legacy advertising payload. */
    if (pdu_bits / 8U >= 39U && packet->pdu[8] == 30U &&
        packet->pdu[9] == 0x16U && packet->pdu[10] == 0xFAU && packet->pdu[11] == 0xFFU) {
        memcpy(packet->message, &packet->pdu[14], 25U);
        packet->message_type = (uint8_t)(packet->message[0] >> 4U);
    }
    return 1;
}

wrj_status_t m4_recover_remoteid(const wrj_cf32_t *iq, uint32_t count, float sample_rate_hz,
                                 const m3_result_t *m3, m4_workspace_t *workspace,
                                 m4_result_t *result)
{
    m4_ble_packet_t packets[WRJ_MAX_PACKETS];
    const uint32_t sps = WRJ_MAX(4U, (uint32_t)lroundf(sample_rate_hz / 1000000.0f));
    uint32_t frame, packet_count = 0U, attempts = 0U;
    if (m3->crc_success_count == 0U) {
        snprintf(result->diagnostics, sizeof(result->diagnostics), "upstream_crc_candidate_missing");
        snprintf(result->crc_candidate_status, sizeof(result->crc_candidate_status), "no_valid_crc24_pdu");
        result->status = M4_STATUS_CRC_FAILED;
        return WRJ_ERR_DATA;
    }
    m4_ble_filter(iq, workspace->filtered, count, sps);
    for (frame = 0U; frame < m3->num_frames && packet_count < WRJ_MAX_PACKETS; ++frame) {
        const uint32_t center = m3->frame_start_samples_0based[frame];
        const int32_t step = (int32_t)WRJ_MAX(1U, sps / 8U);
        const uint32_t windows[3] = {
            WRJ_MAX(1U, (uint32_t)lroundf(.22f * (float)sps)),
            WRJ_MAX(1U, (uint32_t)lroundf(.32f * (float)sps)),
            WRJ_MAX(1U, (uint32_t)lroundf(.42f * (float)sps))};
        uint32_t w;
        int recovered = 0;
        for (w = 0U; w < 3U && !recovered; ++w) {
            int32_t shift;
            for (shift = -(int32_t)sps; shift <= (int32_t)sps; shift += step) {
                m4_ble_packet_t decoded;
                uint32_t old;
                ++attempts;
                if (!m4_ble_try_packet(workspace->filtered, count, center, sps,
                                       shift, windows[w], &decoded)) continue;
                recovered = 1;
                for (old = 0U; old < packet_count; ++old) {
                    if (memcmp(packets[old].pdu, decoded.pdu, 39U) == 0) break;
                }
                if (old == packet_count) packets[packet_count++] = decoded;
                break;
            }
        }
    }
    result->packet_count = (uint16_t)packet_count;
    result->crc_valid_count = (uint16_t)packet_count;
    result->crc_checked = 1U;
    result->crc_passed = (uint8_t)(packet_count != 0U);
    snprintf(result->decoder_method, sizeof(result->decoder_method), "gfsk_phase_ble_aa_dewhiten_crc24");
    snprintf(result->crc_candidate_status, sizeof(result->crc_candidate_status), "%s",
             packet_count != 0U ? "crc24_verified" : "crc24_failed");
    snprintf(result->diagnostics, sizeof(result->diagnostics), "frame_grid_crc_attempts=%u;unique_crc24_pdu=%u",
             attempts, packet_count);
    for (frame = 0U; frame < packet_count; ++frame) {
        result->packet_lengths[frame] = 39U;
        memcpy(result->packet_bytes[frame], packets[frame].pdu, 39U);
    }
    if (packet_count == 0U) {
        result->status = M4_STATUS_CRC_FAILED;
        return WRJ_ERR_DATA;
    }
    memcpy(result->bytes, packets[0].pdu, 39U);
    result->byte_count = 39U;
    for (frame = 0U; frame < 39U; ++frame) result->byte_confidence[frame] = packets[0].confidence;
    result->byte_recovery_confidence = packets[0].confidence;
    result->bit_error_estimate = .5f * (1.0f - packets[0].confidence);
    m4_parse_remoteid_messages(packets, packet_count, result);
    return WRJ_OK;
}
