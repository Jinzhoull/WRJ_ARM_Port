#include "m3_module3.h"

#include <stdlib.h>

static int m3_float_compare(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    return (a > b) - (a < b);
}

static int m3_peak_value_desc(const void *left, const void *right)
{
    const m3_peak_t *a = left;
    const m3_peak_t *b = right;
    return (a->value < b->value) - (a->value > b->value);
}

static int m3_peak_index_asc(const void *left, const void *right)
{
    const m3_peak_t *a = left;
    const m3_peak_t *b = right;
    return (a->index > b->index) - (a->index < b->index);
}

static float m3_quantile(float *scratch, const float *values, uint32_t count, float probability)
{
    const float position = probability * (float)(count - 1U);
    const uint32_t lower = (uint32_t)floorf(position);
    const uint32_t upper = (uint32_t)ceilf(position);
    if (scratch != values) {
        memcpy(scratch, values, sizeof(float) * (size_t)count);
    }
    qsort(scratch, count, sizeof(float), m3_float_compare);
    return scratch[lower] + (position - (float)lower) * (scratch[upper] - scratch[lower]);
}

static uint32_t m3_ble_crc24(const uint8_t *bits, uint32_t count)
{
    uint32_t crc = 0x555555U;
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        const uint32_t feedback = ((crc >> 23U) & 1U) ^ (uint32_t)(bits[index] != 0U);
        crc = (crc << 1U) & 0xFFFFFFU;
        if (feedback != 0U) {
            crc ^= 0x00065BU;
        }
    }
    return crc;
}

static void m3_ble_dewhiten(uint8_t *bits, uint32_t count)
{
    uint8_t state[7];
    uint32_t index;
    const uint8_t channel = 38U;
    state[0] = 1U;
    for (index = 0U; index < 6U; ++index) {
        state[index + 1U] = (uint8_t)((channel >> (5U - index)) & 1U);
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t whitening = state[6];
        uint32_t q;
        bits[index] ^= whitening;
        for (q = 6U; q > 0U; --q) {
            state[q] = state[q - 1U];
        }
        state[0] = whitening;
        state[4] ^= whitening;
    }
}

static int m3_ble_crc_attempt(const wrj_cf32_t *iq, uint32_t count, uint32_t start,
                              uint32_t sps, int32_t shift, uint32_t half_window,
                              const int8_t *expected, int32_t *timing_offset)
{
    enum { SYNC_BITS = 40, DATA_BITS = 336 };
    float soft[SYNC_BITS + DATA_BITS];
    uint8_t raw[DATA_BITS];
    double expected_mean = 0.0;
    double soft_mean = 0.0;
    double covariance = 0.0;
    double expected_variance = 0.0;
    double slope;
    double intercept;
    uint32_t bit;
    uint32_t received_crc = 0U;
    uint32_t pdu_bits;
    const uint32_t lag = WRJ_MAX(1U, sps / 4U);

    for (bit = 0U; bit < SYNC_BITS + DATA_BITS; ++bit) {
        const int64_t center = (int64_t)start + (int64_t)shift + (int64_t)(sps / 2U) +
            (int64_t)bit * (int64_t)sps;
        const int64_t low = center - (int64_t)half_window;
        const int64_t high = center + (int64_t)half_window;
        double phase_sum = 0.0;
        int64_t sample;
        if (low < 0 || high + (int64_t)lag >= (int64_t)count) {
            return 0;
        }
        for (sample = low; sample <= high; ++sample) {
            const wrj_cf32_t a = iq[sample];
            const wrj_cf32_t b = iq[sample + lag];
            phase_sum += atan2((double)a.re * b.im - (double)a.im * b.re,
                               (double)a.re * b.re + (double)a.im * b.im) / (double)lag;
        }
        soft[bit] = (float)(phase_sum / (double)(2U * half_window + 1U));
    }
    for (bit = 0U; bit < SYNC_BITS; ++bit) {
        expected_mean += expected[bit];
        soft_mean += soft[bit];
    }
    expected_mean /= SYNC_BITS;
    soft_mean /= SYNC_BITS;
    for (bit = 0U; bit < SYNC_BITS; ++bit) {
        const double centered_expected = expected[bit] - expected_mean;
        covariance += centered_expected * ((double)soft[bit] - soft_mean);
        expected_variance += centered_expected * centered_expected;
    }
    if (expected_variance < 1.0e-9) {
        return 0;
    }
    slope = covariance / expected_variance;
    if (fabs(slope) < 1.0e-9) {
        return 0;
    }
    intercept = soft_mean - slope * expected_mean;
    for (bit = 0U; bit < DATA_BITS; ++bit) {
        raw[bit] = (uint8_t)((((double)soft[SYNC_BITS + bit] - intercept) / slope) > 0.0);
    }
    m3_ble_dewhiten(raw, DATA_BITS);
    {
        uint32_t length = 0U;
        for (bit = 0U; bit < 8U; ++bit) {
            length |= (uint32_t)raw[8U + bit] << bit;
        }
        length &= 63U;
        if (length < 6U || length > 37U) {
            return 0;
        }
        pdu_bits = (2U + length) * 8U;
    }
    if (pdu_bits + 24U > DATA_BITS) {
        return 0;
    }
    for (bit = 0U; bit < 24U; ++bit) {
        received_crc = (received_crc << 1U) | (uint32_t)raw[pdu_bits + bit];
    }
    if (m3_ble_crc24(raw, pdu_bits) == received_crc) {
        if (timing_offset != NULL) {
            *timing_offset = shift;
        }
        return 1;
    }
    return 0;
}

static void m3_ble_lagged_discriminator(const wrj_cf32_t *iq, uint32_t count,
                                        uint32_t sps, float *output)
{
    static const float scale[4] = {0.65f, 1.0f, 1.45f, 1.90f};
    static const float weight[4] = {0.16f, 0.34f, 0.30f, 0.20f};
    const uint32_t base_lag = WRJ_MAX(1U, (uint32_t)lroundf(0.25f * (float)sps));
    uint32_t branch;
    memset(output, 0, sizeof(*output) * (size_t)count);
    for (branch = 0U; branch < 4U; ++branch) {
        const uint32_t lag = WRJ_MAX(1U, (uint32_t)lroundf((float)base_lag * scale[branch]));
        uint32_t index;
        for (index = 0U; index + lag < count; ++index) {
            const wrj_cf32_t a = iq[index];
            const wrj_cf32_t b = iq[index + lag];
            const float value = atan2f(a.re * b.im - a.im * b.re,
                                       a.re * b.re + a.im * b.im) / (float)lag;
            const uint32_t location = WRJ_MIN(count - 1U, index + (lag - 1U) / 2U);
            output[location] += weight[branch] * value;
        }
    }
}

static void m3_inverse_fft(float *re, float *im, uint32_t length)
{
    uint32_t index;
    for (index = 0U; index < length; ++index) {
        im[index] = -im[index];
    }
    (void)m3_fft_forward_radix2(re, im, length);
    for (index = 0U; index < length; ++index) {
        re[index] /= (float)length;
        im[index] = -im[index] / (float)length;
    }
}

static void m3_generate_droneid_template(uint32_t nfft, uint32_t active,
                                         uint32_t root, float *template_re,
                                         float *template_im, m3_workspace_t *workspace)
{
    uint32_t index;
    const uint32_t first_shifted = nfft / 2U - active / 2U;
    memset(workspace->fft_re, 0, sizeof(float) * (size_t)nfft);
    memset(workspace->fft_im, 0, sizeof(float) * (size_t)nfft);
    for (index = 0U; index < active; ++index) {
        const double phase = -WRJ_PI * (double)root * (double)index *
            (double)(index + 1U) / (double)active;
        const uint32_t shifted_bin = first_shifted + index;
        const uint32_t raw_bin = (shifted_bin + nfft / 2U) % nfft;
        workspace->fft_re[raw_bin] = (float)cos(phase);
        workspace->fft_im[raw_bin] = (float)sin(phase);
    }
    m3_inverse_fft(workspace->fft_re, workspace->fft_im, nfft);
    for (index = 0U; index < nfft; ++index) {
        template_re[index] = workspace->fft_re[index] * sqrtf((float)nfft);
        template_im[index] = workspace->fft_im[index] * sqrtf((float)nfft);
    }
}

static float m3_template_score(const wrj_cf32_t *iq, uint32_t count, int64_t start,
                               const float *template_re, const float *template_im,
                               uint32_t length)
{
    uint32_t index;
    double correlation_re = 0.0;
    double correlation_im = 0.0;
    double signal_energy = 0.0;
    double template_energy = 0.0;
    if (start < 0 || start + (int64_t)length > (int64_t)count) {
        return 0.0f;
    }
    for (index = 0U; index < length; ++index) {
        const wrj_cf32_t sample = iq[start + index];
        const float tr = template_re[index];
        const float ti = template_im[index];
        correlation_re += (double)tr * sample.re + (double)ti * sample.im;
        correlation_im += (double)tr * sample.im - (double)ti * sample.re;
        signal_energy += wrj_complex_abs2(sample.re, sample.im);
        template_energy += (double)tr * tr + (double)ti * ti;
    }
    return (float)(sqrt(correlation_re * correlation_re + correlation_im * correlation_im) /
        (sqrt(signal_energy * template_energy) + 1.0e-20));
}

static float m3_droneid_candidate_score(const wrj_cf32_t *iq, uint32_t count,
                                        int64_t frame_start, uint32_t offset600,
                                        uint32_t offset147, uint32_t nfft,
                                        m3_workspace_t *workspace)
{
    const float score600 = m3_template_score(iq, count, frame_start + offset600,
        workspace->spectrum_smooth, workspace->spectrum_weight, nfft);
    const float score147 = m3_template_score(iq, count, frame_start + offset147,
        workspace->spectrum_aux, workspace->power, nfft);
    const float balance = 1.0f - fabsf(score600 - score147) /
        WRJ_MAX(WRJ_MAX(score600, score147), 1.0e-6f);
    return wrj_clip01(0.43f * score600 + 0.43f * score147 + 0.14f * balance);
}

wrj_status_t m3_droneid_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                    float sample_rate_hz, uint32_t max_frames,
                                    m3_workspace_t *workspace, m3_result_t *result)
{
    const float scale = sample_rate_hz / 15360000.0f;
    const uint32_t nfft = (uint32_t)lroundf(1024.0f * scale);
    const uint32_t cp = (uint32_t)lroundf(72.0f * scale);
    const uint32_t frame_length = (uint32_t)lroundf((9.0f * 1024.0f + 2.0f * 80.0f +
                                                     7.0f * 72.0f) * scale);
    const uint32_t offset600 = (uint32_t)lroundf((1104.0f + 1096.0f + 1096.0f + 72.0f) * scale);
    const uint32_t offset147 = (uint32_t)lroundf((1104.0f + 4.0f * 1096.0f + 72.0f) * scale);
    const uint32_t boundaries[9] = {0U, 2208U, 4400U, 6592U, 8784U, 10976U, 13168U, 15360U, 17552U};
    m3_result_t cp_result;
    int64_t best_start = -1;
    float best_score = -1.0f;
    uint32_t frame;
    uint32_t boundary;
    uint32_t output = 0U;
    if (nfft > workspace->spectrum_length || count <= frame_length) {
        return WRJ_ERR_CAPACITY;
    }
    memset(&cp_result, 0, sizeof(cp_result));
    (void)m3_cp_synchronize(iq, count, nfft, cp, max_frames, workspace, &cp_result);
    m3_generate_droneid_template(nfft, 601U, 600U, workspace->spectrum_smooth,
                                 workspace->spectrum_weight, workspace);
    m3_generate_droneid_template(nfft, 601U, 147U, workspace->spectrum_aux,
                                 workspace->power, workspace);
    for (frame = 0U; frame < cp_result.num_frames; ++frame) {
        for (boundary = 0U; boundary < WRJ_ARRAY_COUNT(boundaries); ++boundary) {
            const int64_t candidate = (int64_t)cp_result.frame_start_samples_0based[frame] -
                (int64_t)lroundf((float)boundaries[boundary] * scale / 2.0f);
            const float score = m3_droneid_candidate_score(iq, count, candidate, offset600,
                                                           offset147, nfft, workspace);
            if (score > best_score) {
                best_score = score;
                best_start = candidate;
            }
        }
    }
    if (best_start < 0) {
        return WRJ_ERR_DATA;
    }
    result->original_frame_start = (float)best_start;
    {
        const int32_t radius = WRJ_MIN(3000, (int32_t)(nfft / 4U));
        int32_t offset;
        int32_t best_offset = 0;
        for (offset = -radius; offset <= radius; offset += 8) {
            const float score = m3_droneid_candidate_score(iq, count, best_start + offset,
                                                           offset600, offset147, nfft, workspace);
            if (score > best_score) {
                best_score = score;
                best_offset = offset;
            }
        }
        for (offset = best_offset - 8; offset <= best_offset + 8; ++offset) {
            const float score = m3_droneid_candidate_score(iq, count, best_start + offset,
                                                           offset600, offset147, nfft, workspace);
            if (score > best_score) {
                best_score = score;
                best_offset = offset;
            }
        }
        best_start += best_offset;
        result->frame_offset_correction = (float)best_offset;
        result->timing_alignment_improved = (uint8_t)(best_offset != 0);
    }
    while (best_start < 0) {
        best_start += frame_length;
    }
    while (best_start >= (int64_t)frame_length) {
        best_start -= frame_length;
    }
    for (; best_start < (int64_t)count && output < WRJ_MIN(max_frames, WRJ_MAX_FRAMES);
         best_start += frame_length) {
        if (best_start + (int64_t)frame_length <= (int64_t)count) {
            result->frame_start_samples_0based[output] = (uint32_t)best_start;
            result->frame_confidence[output] = best_score;
            ++output;
        }
    }
    result->num_frames = output;
    result->frame_length_samples = frame_length;
    result->corrected_frame_start = output > 0U ? (float)result->frame_start_samples_0based[0] : NAN;
    result->droneid_zc_peak_position = result->corrected_frame_start + (float)offset600;
    result->offset_search_score = best_score;
    result->peak_metric = best_score;
    result->sync_confidence = wrj_clip01(0.75f /
        (1.0f + expf(-((best_score - 0.08f) / 0.04f))) + 0.25f * cp_result.sync_confidence);
    result->fractional_cfo_hz = cp_result.fractional_cfo_hz;
    snprintf(result->sync_method, sizeof(result->sync_method),
             "DJI_DroneID_ZC600_ZC147_plus_CP");
    return output > 0U ? WRJ_OK : WRJ_ERR_DATA;
}

wrj_status_t m3_remoteid_ble_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                         float sample_rate_hz, uint32_t max_frames,
                                         m3_workspace_t *workspace, m3_result_t *result)
{
    static const uint8_t access_bytes[4] = {0xD6U, 0xBEU, 0x89U, 0x8EU};
    static const uint8_t fixed_bytes[7] = {0x42U, 37U, 30U, 0x16U, 0xFAU, 0xFFU, 0x0DU};
    static const uint8_t fixed_byte_positions[7] = {0U, 1U, 8U, 9U, 10U, 11U, 12U};
    int8_t expected[40];
    uint16_t known_positions[96];
    int8_t known_signs[96];
    uint8_t known_count = 0U;
    uint8_t whiten_mask[120] = {0U};
    const uint32_t sps = WRJ_MAX(4U, (uint32_t)lroundf(sample_rate_hz / 1000000.0f));
    uint32_t phase;
    uint32_t candidate_count = 0U;
    uint32_t selected = 0U;
    uint32_t index;
    uint32_t recovery_locations[96];
    uint32_t recovery_count = 0U;
    uint32_t score_count = 0U;
    float threshold;
    float peak = 0.0f;
    float repeat_score = 0.0f;
    const wrj_cf32_t *signal = workspace->baseband;
    const uint32_t min_separation = (uint32_t)lroundf(0.45e-3f * sample_rate_hz);
    if (count < 64U * sps) {
        return WRJ_ERR_DATA;
    }
    /* Fixed receiver-side anti-noise smoothing, equivalent to the bounded
     * BLE low-pass branch in MATLAB.  The window depends only on samples per
     * symbol and never on candidate identity or decoded content. */
    {
        const uint32_t radius = WRJ_MAX(1U, sps / 7U);
        double sum_re = 0.0;
        double sum_im = 0.0;
        uint32_t right = 0U;
        uint32_t left = 0U;
        for (index = 0U; index < count; ++index) {
            const uint32_t wanted_right = WRJ_MIN(count - 1U, index + radius);
            const uint32_t wanted_left = index > radius ? index - radius : 0U;
            while (right <= wanted_right) {
                sum_re += iq[right].re;
                sum_im += iq[right].im;
                ++right;
            }
            while (left < wanted_left) {
                sum_re -= iq[left].re;
                sum_im -= iq[left].im;
                ++left;
            }
            workspace->baseband[index].re = (float)(sum_re / (double)(right - left));
            workspace->baseband[index].im = (float)(sum_im / (double)(right - left));
        }
    }
    for (index = 0U; index < 8U; ++index) {
        expected[index] = (int8_t)((index & 1U) != 0U ? 1 : -1);
    }
    for (index = 0U; index < 32U; ++index) {
        expected[8U + index] = (int8_t)(((access_bytes[index / 8U] >> (index & 7U)) & 1U) != 0U ? 1 : -1);
    }
    for (index = 0U; index < 40U; ++index) {
        known_positions[known_count] = (uint16_t)index;
        known_signs[known_count++] = expected[index];
    }
    m3_ble_dewhiten(whiten_mask, 120U);
    for (index = 0U; index < 7U; ++index) {
        uint32_t bit;
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t raw_position = (uint32_t)fixed_byte_positions[index] * 8U + bit;
            const uint8_t raw_bit = (uint8_t)((fixed_bytes[index] >> bit) & 1U);
            const uint8_t whitened = raw_bit ^ whiten_mask[raw_position];
            known_positions[known_count] = (uint16_t)(40U + raw_position);
            known_signs[known_count++] = whitened != 0U ? 1 : -1;
        }
    }
    m3_ble_lagged_discriminator(signal, count, sps, workspace->metric);
    for (phase = 0U; phase < sps; ++phase) {
        const uint32_t symbols = (count - 1U - phase) / sps;
        uint32_t symbol;
        if (symbols < 160U) {
            continue;
        }
        for (symbol = 0U; symbol < symbols; ++symbol) {
            uint32_t q;
            double sum = 0.0;
            const uint32_t begin = phase + symbol * sps + sps / 4U;
            const uint32_t end = WRJ_MIN(count - 1U, begin + WRJ_MAX(1U, sps / 2U));
            for (q = begin; q < end; ++q) {
                sum += workspace->metric[q];
            }
            workspace->scratch[symbol] = (float)(sum / (double)WRJ_MAX(1U, end - begin));
        }
        for (symbol = 0U; symbol + 160U <= symbols; ++symbol) {
            uint32_t bit;
            double dot = 0.0;
            double energy = 0.0;
            for (bit = 0U; bit < known_count; ++bit) {
                const float value = workspace->scratch[symbol + known_positions[bit]];
                dot += (double)known_signs[bit] * value;
                energy += (double)value * value;
            }
            if (energy > 1.0e-12) {
                const float score = (float)(fabs(dot) / sqrt((double)known_count * energy));
                const uint32_t location = phase + symbol * sps;
                workspace->metric[location] = score;
                workspace->scratch[score_count++] = score;
                peak = WRJ_MAX(peak, score);
            }
        }
    }
    if (score_count == 0U) {
        return WRJ_ERR_DATA;
    }
    /* Match the MATLAB receiver's robust candidate policy: the absolute
     * floor rejects unstructured noise, while the upper-tail quantile keeps
     * weak but repeatable advertisements.  This is an internal acquisition
     * threshold; the public 0.80 acceptance threshold is unchanged. */
    threshold = WRJ_MAX(0.12f,
        m3_quantile(workspace->scratch, workspace->scratch, score_count, 0.995f));
    for (phase = 0U; phase < sps; ++phase) {
        const uint32_t symbols = (count - 1U - phase) / sps;
        uint32_t symbol;
        for (symbol = 0U; symbol + 160U <= symbols; ++symbol) {
            const uint32_t location = phase + symbol * sps;
            const float score = workspace->metric[location];
            if (score >= threshold && candidate_count < workspace->peak_capacity) {
                workspace->peak_candidates[candidate_count].index = location;
                workspace->peak_candidates[candidate_count].value = score;
                ++candidate_count;
            }
        }
    }
    if (candidate_count == 0U) {
        return WRJ_ERR_DATA;
    }
    qsort(workspace->peak_candidates, candidate_count, sizeof(m3_peak_t), m3_peak_value_desc);
    /* Packet recovery deliberately keeps a denser finite bank than the
     * frame-grid output.  CRC24, not correlation alone, decides which of
     * these low-SNR hypotheses is valid. */
    for (index = 0U; index < candidate_count && recovery_count < 96U; ++index) {
        uint32_t other;
        int separated = 1;
        for (other = 0U; other < recovery_count; ++other) {
            const uint32_t a = recovery_locations[other];
            const uint32_t b = workspace->peak_candidates[index].index;
            const uint32_t distance = a > b ? a - b : b - a;
            if (distance < 4U * sps) {
                separated = 0;
                break;
            }
        }
        if (separated != 0) {
            recovery_locations[recovery_count++] = workspace->peak_candidates[index].index;
        }
    }
    for (index = 0U; index < recovery_count; ++index) {
        static const float window_fractions[3] = {0.22f, 0.32f, 0.42f};
        const int32_t shift_step = (int32_t)WRJ_MAX(1U, sps / 8U);
        uint32_t window_index;
        int crc_ok = 0;
        int32_t best_shift = 0;
        for (window_index = 0U; window_index < 3U && crc_ok == 0; ++window_index) {
            const uint32_t half_window = WRJ_MAX(1U,
                (uint32_t)lroundf(window_fractions[window_index] * (float)sps));
            int32_t shift;
            for (shift = -(int32_t)sps; shift <= (int32_t)sps; shift += shift_step) {
                ++result->crc_attempt_count;
                if (m3_ble_crc_attempt(signal, count, recovery_locations[index], sps,
                                       shift, half_window, expected, &best_shift) != 0) {
                    ++result->crc_success_count;
                    crc_ok = 1;
                    break;
                }
            }
        }
        if (crc_ok != 0 && result->crc_success_count == 1U) {
            result->symbol_timing_offset = (float)best_shift;
        }
    }
    for (index = 0U; index < candidate_count && selected < WRJ_MIN(max_frames, WRJ_MAX_FRAMES); ++index) {
        uint32_t other;
        int separated = 1;
        for (other = 0U; other < selected; ++other) {
            const uint32_t distance = workspace->peak_selected[other].index > workspace->peak_candidates[index].index ?
                workspace->peak_selected[other].index - workspace->peak_candidates[index].index :
                workspace->peak_candidates[index].index - workspace->peak_selected[other].index;
            if (distance < min_separation) {
                separated = 0;
                break;
            }
        }
        if (separated != 0) {
            workspace->peak_selected[selected++] = workspace->peak_candidates[index];
        }
    }
    qsort(workspace->peak_selected, selected, sizeof(m3_peak_t), m3_peak_index_asc);
    for (index = 0U; index < selected; ++index) {
        result->frame_start_samples_0based[index] = workspace->peak_selected[index].index;
        result->frame_confidence[index] = workspace->peak_selected[index].value;
    }
    result->num_frames = selected;
    result->frame_length_samples = selected > 1U ?
        result->frame_start_samples_0based[1] - result->frame_start_samples_0based[0] : 376U * sps;
    result->peak_metric = workspace->peak_candidates[0].value;
    result->access_address_confidence = result->peak_metric;
    if (selected >= 3U) {
        double mean_gap = 0.0;
        double variance = 0.0;
        for (index = 1U; index < selected; ++index) {
            mean_gap += (double)(result->frame_start_samples_0based[index] -
                                 result->frame_start_samples_0based[index - 1U]);
        }
        mean_gap /= (double)(selected - 1U);
        for (index = 1U; index < selected; ++index) {
            const double gap = (double)(result->frame_start_samples_0based[index] -
                                        result->frame_start_samples_0based[index - 1U]);
            variance += (gap - mean_gap) * (gap - mean_gap);
        }
        variance /= (double)(selected - 1U);
        repeat_score = wrj_clip01(1.0f - (float)(sqrt(variance) / WRJ_MAX(mean_gap, 1.0)));
    }
    result->ble_sync_confidence = wrj_clip01(
        0.72f * wrj_clip01((result->peak_metric - 0.08f) / 0.42f) + 0.28f * repeat_score);
    if (result->crc_success_count > 0U) {
        const float crc_evidence = wrj_clip01((float)result->crc_success_count / 2.0f);
        result->ble_sync_confidence = WRJ_MAX(result->ble_sync_confidence,
            wrj_clip01(0.82f + 0.16f * crc_evidence));
    }
    result->sync_confidence = result->ble_sync_confidence;
    if (result->crc_success_count == 0U) {
        result->symbol_timing_offset = (float)(workspace->peak_candidates[0].index % sps);
    }
    snprintf(result->sync_method, sizeof(result->sync_method),
             "BLE_1M_preamble_access_address_multiphase");
    return WRJ_OK;
}

wrj_status_t m3_burst_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                  float sample_rate_hz, uint32_t max_frames,
                                  m3_workspace_t *workspace, m3_result_t *result)
{
    const uint32_t length = 256U;
    const uint32_t hop = 128U;
    const uint32_t blocks = count > length ? (count - length) / hop + 1U : 0U;
    uint32_t block;
    uint32_t selected = 0U;
    float q20;
    float q80;
    float threshold;
    uint32_t previous = UINT32_MAX;
    if (blocks < 6U) {
        return WRJ_ERR_DATA;
    }
    for (block = 0U; block < blocks; ++block) {
        uint32_t sample;
        double energy = 0.0;
        for (sample = 0U; sample < length; ++sample) {
            energy += wrj_complex_abs2(iq[block * hop + sample].re, iq[block * hop + sample].im);
        }
        workspace->metric[block] = 10.0f * log10f((float)(energy / length) + 1.0e-20f);
    }
    q20 = m3_quantile(workspace->scratch, workspace->metric, blocks, 0.20f);
    q80 = m3_quantile(workspace->scratch, workspace->metric, blocks, 0.80f);
    threshold = q20 + 0.35f * (q80 - q20);
    for (block = 1U; block < blocks && selected < WRJ_MIN(max_frames, WRJ_MAX_FRAMES); ++block) {
        if (workspace->metric[block] > threshold && workspace->metric[block - 1U] <= threshold) {
            const uint32_t start = block * hop;
            if (previous == UINT32_MAX || start - previous >= (uint32_t)lroundf(0.0004f * sample_rate_hz)) {
                result->frame_start_samples_0based[selected] = start;
                result->frame_confidence[selected] = wrj_clip01((q80 - q20) / 8.0f);
                previous = start;
                ++selected;
            }
        }
    }
    result->num_frames = selected;
    result->frame_length_samples = selected > 1U ?
        result->frame_start_samples_0based[1] - result->frame_start_samples_0based[0] : length;
    result->peak_metric = q80 - q20;
    result->sync_confidence = wrj_clip01((q80 - q20) / 8.0f);
    snprintf(result->sync_method, sizeof(result->sync_method), "envelope_activity_edges");
    return selected > 0U ? WRJ_OK : WRJ_ERR_DATA;
}

wrj_status_t m3_blind_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                  float sample_rate_hz, uint32_t max_frames,
                                  m3_workspace_t *workspace, m3_result_t *result)
{
    static const uint32_t base_lags[] = {256U, 512U, 768U, 1024U, 1152U, 2048U, 2192U};
    uint32_t lag_index;
    float best_score = -1.0f;
    m3_result_t best;
    memset(&best, 0, sizeof(best));
    for (lag_index = 0U; lag_index < WRJ_ARRAY_COUNT(base_lags); ++lag_index) {
        const uint32_t nfft = (uint32_t)lroundf((float)base_lags[lag_index] *
                                                sample_rate_hz / 30720000.0f);
        const float ratios[] = {1.0f / 16.0f, 0.10f, 1.0f / 8.0f};
        uint32_t ratio;
        for (ratio = 0U; ratio < WRJ_ARRAY_COUNT(ratios); ++ratio) {
            const uint32_t cp = WRJ_MAX(16U, (uint32_t)lroundf((float)nfft * ratios[ratio]));
            m3_result_t trial;
            float score;
            memset(&trial, 0, sizeof(trial));
            if (m3_cp_synchronize(iq, count, nfft, cp, max_frames, workspace, &trial) != WRJ_OK) {
                continue;
            }
            score = trial.sync_confidence * trial.peak_metric;
            if (score > best_score) {
                best_score = score;
                best = trial;
            }
        }
    }
    if (best_score < 0.12f) {
        return WRJ_ERR_DATA;
    }
    *result = best;
    snprintf(result->sync_method, sizeof(result->sync_method), "blind_CP_repetition");
    return WRJ_OK;
}

void m3_estimate_sfo(const wrj_cf32_t *iq, uint32_t count, uint32_t nfft,
                     uint32_t cp_samples, m3_result_t *result)
{
    float ppm[WRJ_MAX_FRAMES];
    uint32_t observations = 0U;
    uint32_t index;
    result->estimated_sfo_ppm = NAN;
    result->frame_drift_samples = 0.0f;
    result->timing_slope = 0.0f;
    result->sfo_correction_applied = 0U;
    if (result->num_frames < 4U || result->frame_length_samples < 8U) {
        return;
    }
    if (iq != NULL && nfft >= 8U && cp_samples >= 4U &&
        result->frame_length_samples > 5U * nfft) {
        float offsets[WRJ_MAX_FRAMES];
        float scores[WRJ_MAX_FRAMES];
        float base_scores[WRJ_MAX_FRAMES];
        double sx = 0.0;
        double sy = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        const uint32_t frames = result->num_frames;
        for (index = 0U; index < frames; ++index) {
            int32_t offset;
            float best = -1.0f;
            int32_t best_offset = 0;
            const int64_t start = result->frame_start_samples_0based[index];
            for (offset = -24; offset <= 24; ++offset) {
                uint32_t q;
                double sr = 0.0;
                double si = 0.0;
                double ea = 0.0;
                double eb = 0.0;
                const int64_t probe = start + offset;
                float coherence;
                if (probe < 0 || probe + (int64_t)nfft + cp_samples > (int64_t)count) {
                    continue;
                }
                for (q = 0U; q < cp_samples; ++q) {
                    const wrj_cf32_t a = iq[probe + q];
                    const wrj_cf32_t b = iq[probe + q + nfft];
                    sr += (double)a.re * b.re + (double)a.im * b.im;
                    si += (double)a.re * b.im - (double)a.im * b.re;
                    ea += wrj_complex_abs2(a.re, a.im);
                    eb += wrj_complex_abs2(b.re, b.im);
                }
                coherence = (float)(sqrt(sr * sr + si * si) / (sqrt(ea * eb) + 1.0e-20));
                if (offset == 0) {
                    base_scores[index] = coherence;
                }
                if (coherence - 0.0006f * (float)abs(offset) > best) {
                    best = coherence - 0.0006f * (float)abs(offset);
                    best_offset = offset;
                }
            }
            offsets[index] = (float)best_offset;
            scores[index] = best;
            sx += index;
            sy += offsets[index];
            sxx += (double)index * index;
            sxy += (double)index * offsets[index];
        }
        {
            const double denominator = (double)frames * sxx - sx * sx;
            const double slope = fabs(denominator) > 1.0e-12 ?
                ((double)frames * sxy - sx * sy) / denominator : 0.0;
            double residual_sum = 0.0;
            double best_sum = 0.0;
            double base_sum = 0.0;
            for (index = 0U; index < frames; ++index) {
                const double intercept = (sy - slope * sx) / (double)frames;
                residual_sum += fabs(offsets[index] - (float)(intercept + slope * index));
                best_sum += scores[index];
                base_sum += base_scores[index];
            }
            result->timing_slope = (float)slope;
            result->estimated_sfo_ppm = (float)(1.0e6 * slope /
                (double)result->frame_length_samples);
            result->frame_drift_samples = (float)(slope * (double)(frames - 1U));
            if (fabsf(result->estimated_sfo_ppm) >= 3.0f &&
                fabsf(result->estimated_sfo_ppm) <= 250.0f &&
                fabsf(result->frame_drift_samples) <= 24.0f &&
                residual_sum / frames <= 2.0 && best_sum >= 0.98 * base_sum) {
                for (index = 0U; index < frames; ++index) {
                    const int32_t correction = (int32_t)lround(slope * (double)index);
                    const int64_t corrected = (int64_t)result->frame_start_samples_0based[index] + correction;
                    if (corrected >= 0 && corrected < (int64_t)count) {
                        result->frame_start_samples_0based[index] = (uint32_t)corrected;
                    }
                }
                result->sfo_correction_applied = 1U;
            }
        }
        return;
    }
    for (index = 1U; index < result->num_frames; ++index) {
        const float gap = (float)(result->frame_start_samples_0based[index] -
                                  result->frame_start_samples_0based[index - 1U]);
        const float steps = WRJ_MAX(1.0f, roundf(gap / (float)result->frame_length_samples));
        const float value = 1.0e6f * (gap / (steps * (float)result->frame_length_samples) - 1.0f);
        if (fabsf(value) <= 250.0f) {
            ppm[observations++] = value;
        }
    }
    if (observations >= 3U) {
        qsort(ppm, observations, sizeof(float), m3_float_compare);
        result->estimated_sfo_ppm = ppm[observations / 2U];
        result->timing_slope = result->estimated_sfo_ppm *
            (float)result->frame_length_samples / 1.0e6f;
        result->frame_drift_samples = result->timing_slope * (float)(result->num_frames - 1U);
    }
}
