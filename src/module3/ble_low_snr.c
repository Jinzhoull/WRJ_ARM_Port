#include "m3_module3.h"

#include <stdlib.h>

#define M3_BLE_BANK_TAPS 49U
#define M3_BLE_BANK_PHASES 256U
#define M3_BLE_BANK_GROUPS 64U
#define M3_BLE_BANK_VIEWS 8U

typedef struct {
    float bits[336];
    float score;
    uint32_t start;
} m3_ble_bank_view_t;

typedef struct {
    uint32_t start;
    uint8_t count;
    m3_ble_bank_view_t views[M3_BLE_BANK_VIEWS];
} m3_ble_bank_group_t;

static int m3_ble_bank_float_compare(const void *left, const void *right)
{
    const float a = *(const float *)left, b = *(const float *)right;
    return (a > b) - (a < b);
}

static void m3_ble_bank_add_soft(m3_ble_bank_group_t *groups, uint32_t *group_count,
                                  uint32_t start, float score, const float soft[336],
                                  uint32_t tolerance)
{
    uint32_t g, i, slot;
    m3_ble_bank_group_t *group;
    float magnitudes[336];
    float scale;
    for (g = 0U; g < *group_count; ++g) {
        const uint32_t distance = groups[g].start > start ?
            groups[g].start - start : start - groups[g].start;
        if (distance <= tolerance) break;
    }
    if (g == *group_count) {
        if (*group_count >= M3_BLE_BANK_GROUPS) return;
        groups[g].start = start;
        ++*group_count;
    }
    group = &groups[g];
    if (group->count < M3_BLE_BANK_VIEWS) slot = group->count++;
    else {
        slot = 0U;
        for (i = 1U; i < M3_BLE_BANK_VIEWS; ++i)
            if (group->views[i].score < group->views[slot].score) slot = i;
        if (score <= group->views[slot].score) return;
    }
    for (i = 0U; i < 336U; ++i) magnitudes[i] = fabsf(soft[i]);
    qsort(magnitudes, 336U, sizeof(*magnitudes), m3_ble_bank_float_compare);
    scale = WRJ_MAX(1.0e-6f, 0.5f * (magnitudes[167] + magnitudes[168]));
    group->views[slot].start = start;
    group->views[slot].score = score;
    for (i = 0U; i < 336U; ++i)
        group->views[slot].bits[i] = soft[i] / scale;
}

static int m3_ble_bank_infer_counter(const m3_result_t *best, uint32_t start,
                                      uint8_t *counter)
{
    uint32_t before = UINT32_MAX, after = UINT32_MAX, i;
    uint32_t before_start = 0U, after_start = UINT32_MAX;
    for (i = 0U; i < best->ble_verified_packet_count; ++i) {
        const uint32_t s = best->ble_verified_start[i];
        if (s < start && s > before_start) { before = i; before_start = s; }
        if (s > start && s < after_start) { after = i; after_start = s; }
    }
    if (before == UINT32_MAX || after == UINT32_MAX) return 0;
    {
        const uint8_t a = best->ble_verified_pdu[before][13];
        const uint8_t b = best->ble_verified_pdu[after][13];
        const uint32_t step = (uint8_t)(b - a);
        uint32_t q;
        double period;
        if (step < 2U || step > 4U) return 0;
        period = (double)(after_start - before_start) / step;
        for (q = 1U; q < step; ++q) {
            const double predicted = before_start + q * period;
            if (fabs((double)start - predicted) <= 0.45 * period) {
                *counter = (uint8_t)(a + q);
                return 1;
            }
        }
    }
    return 0;
}

static void m3_ble_bank_fuse(m3_ble_bank_group_t *groups, uint32_t group_count,
                              m3_result_t *best)
{
    uint32_t g;
    for (g = 0U; g < group_count; ++g) {
        m3_ble_bank_group_t *group = &groups[g];
        static const uint8_t counts[] = {3U,4U,6U,8U};
        uint32_t n, i, j;
        if (group->count < 3U) continue;
        for (i = 0U; i < group->count; ++i) {
            for (j = i + 1U; j < group->count; ++j) {
                if (group->views[j].score > group->views[i].score) {
                    const m3_ble_bank_view_t temp = group->views[i];
                    group->views[i] = group->views[j];
                    group->views[j] = temp;
                }
            }
        }
        for (n = 0U; n < WRJ_ARRAY_COUNT(counts); ++n) {
            const uint32_t use = WRJ_MIN((uint32_t)counts[n], group->count);
            float fused[336] = {0.0f}, total_weight = 0.0f;
            uint8_t pdu[39];
            uint32_t crc;
            int recovered;
            for (i = 0U; i < use; ++i) {
                const float weight = WRJ_MAX(0.08f, group->views[i].score);
                total_weight += weight;
                for (j = 0U; j < 336U; ++j)
                    fused[j] += weight * group->views[i].bits[j];
            }
            for (j = 0U; j < 336U; ++j) fused[j] /= total_weight;
            recovered = m3_ble_recover_soft_pdu(fused, pdu, &crc);
            if (!recovered) {
                uint8_t counter;
                if (m3_ble_bank_infer_counter(best, group->start, &counter))
                    recovered = m3_ble_recover_basic_id(fused, counter, pdu, &crc);
            }
            if (!recovered) continue;
            for (j = 0U; j < best->ble_verified_packet_count; ++j)
                if (memcmp(best->ble_verified_pdu[j], pdu, 39U) == 0) break;
            if (j == best->ble_verified_packet_count && j < WRJ_MAX_PACKETS) {
                memcpy(best->ble_verified_pdu[j], pdu, 39U);
                best->ble_verified_crc[j] = crc;
                best->ble_verified_start[j] = group->start;
                best->ble_verified_confidence[j] =
                    wrj_clip01(group->views[0].score);
                ++best->ble_verified_packet_count;
                ++best->ble_soft_recovered_count;
            }
            break;
        }
    }
    best->crc_success_count = WRJ_MAX(best->crc_success_count,
                                      best->ble_verified_packet_count);
}

static uint32_t m3_ble_bank_type_score(uint32_t mask)
{
    uint32_t count = 0U, bits = mask;
    while (bits != 0U) { bits &= bits - 1U; ++count; }
    return count + (((mask & 3U) == 3U) ? 4U : 0U);
}

/* The MATLAB bank uses a flat-passband eighth-power spectral filter followed
 * by resample's separate anti-aliasing filter. Do not apply a Gaussian
 * low-pass twice: that attenuates the GFSK symbols before soft decisions. */
static void m3_ble_bank_coefficients(float sample_rate_hz, float target_rate_hz,
                                     float coeff[M3_BLE_BANK_PHASES][M3_BLE_BANK_TAPS])
{
    const double cutoff = 0.45 * WRJ_MIN(target_rate_hz, sample_rate_hz) /
        sample_rate_hz;
    uint32_t phase;
    for (phase = 0U; phase < M3_BLE_BANK_PHASES; ++phase) {
        const double fraction = (double)phase / (double)(M3_BLE_BANK_PHASES - 1U);
        double sum = 0.0;
        uint32_t tap;
        for (tap = 0U; tap < M3_BLE_BANK_TAPS; ++tap) {
            const double offset = (double)((int32_t)tap - 24) - fraction;
            const double angle = 2.0 * WRJ_PI * cutoff * offset;
            const double sinc = fabs(angle) < 1.0e-10 ? 1.0 : sin(angle) / angle;
            const double window = 0.54 + 0.46 * cos(WRJ_PI * offset / 24.0);
            coeff[phase][tap] = (float)(2.0 * cutoff * sinc * window);
            sum += coeff[phase][tap];
        }
        for (tap = 0U; tap < M3_BLE_BANK_TAPS; ++tap)
            coeff[phase][tap] = (float)(coeff[phase][tap] / sum);
    }
}

static wrj_status_t m3_ble_bank_spectral_filter(const wrj_cf32_t *input,
                                                 uint32_t count, uint32_t fft_size,
                                                 float sample_rate_hz, float width_hz,
                                                 float *re, float *im,
                                                 wrj_cf32_t *output)
{
    uint32_t i;
    memset(re, 0, (size_t)fft_size * sizeof(*re));
    memset(im, 0, (size_t)fft_size * sizeof(*im));
    for (i = 0U; i < count; ++i) { re[i] = input[i].re; im[i] = input[i].im; }
    if (m3_fft_forward_radix2(re, im, fft_size) != WRJ_OK) return WRJ_ERR_DATA;
    for (i = 0U; i < fft_size; ++i) {
        const int32_t signed_bin = i <= fft_size / 2U ? (int32_t)i :
            (int32_t)i - (int32_t)fft_size;
        const double frequency = (double)signed_bin * sample_rate_hz / fft_size;
        const double ratio = fabs(frequency) / width_hz;
        const float gain = (float)exp(-0.5 * pow(ratio, 8.0));
        re[i] *= gain; im[i] *= gain;
    }
    for (i = 0U; i < fft_size; ++i) im[i] = -im[i];
    if (m3_fft_forward_radix2(re, im, fft_size) != WRJ_OK) return WRJ_ERR_DATA;
    for (i = 0U; i < count; ++i) {
        output[i].re = re[i] / (float)fft_size;
        output[i].im = -im[i] / (float)fft_size;
    }
    return WRJ_OK;
}

static void m3_ble_bank_resample(const wrj_cf32_t *input, uint32_t count,
                                 uint32_t input_sps, uint32_t output_sps,
                                 float coeff[][M3_BLE_BANK_TAPS],
                                 wrj_cf32_t *output, uint32_t output_count)
{
    const double step = (double)input_sps / output_sps;
    uint32_t index;
    for (index = 0U; index < output_count; ++index) {
        const double position = (double)index * step;
        const int64_t center = (int64_t)floor(position);
        const uint32_t phase = WRJ_MIN(M3_BLE_BANK_PHASES - 1U,
            (uint32_t)lround((position - (double)center) * (M3_BLE_BANK_PHASES - 1U)));
        double re = 0.0, im = 0.0, sum = 0.0;
        uint32_t tap;
        for (tap = 0U; tap < M3_BLE_BANK_TAPS; ++tap) {
            const int64_t source = center + (int64_t)tap - 24;
            const double weight = coeff[phase][tap];
            if (source < 0 || source >= (int64_t)count) continue;
            re += weight * input[source].re;
            im += weight * input[source].im;
            sum += weight;
        }
        output[index].re = (float)(re / WRJ_MAX(sum, 1.0e-12));
        output[index].im = (float)(im / WRJ_MAX(sum, 1.0e-12));
    }
}

wrj_status_t m3_ble_low_snr_receiver_bank(const wrj_cf32_t *iq, uint32_t count,
                                          float sample_rate_hz, uint32_t max_frames,
                                          m3_workspace_t *workspace, m3_result_t *result,
                                          float preferred_offset_hz,
                                          float *relative_cfo_hz)
{
    static const float relative_grid[] = {-50000.0f,-40000.0f,-30000.0f,
        -20000.0f,-10000.0f,0.0f,10000.0f,20000.0f,30000.0f,40000.0f,50000.0f};
    static const float widths[] = {900000.0f,650000.0f};
    static const float rates[] = {16000000.0f,4000000.0f};
    float coeff[M3_BLE_BANK_PHASES][M3_BLE_BANK_TAPS];
    const uint32_t input_sps = WRJ_MAX(4U,
        (uint32_t)lroundf(sample_rate_hz / 1000000.0f));
    const uint32_t capacity = (uint32_t)ceil((double)count * 16.0 / input_sps);
    wrj_cf32_t *shifted = NULL, *filtered = NULL, *downsampled = NULL;
    m3_ble_bank_group_t *groups = NULL;
    uint32_t group_count = 0U;
    float *fft_re = NULL, *fft_im = NULL;
    uint32_t fft_size = 1U;
    m3_result_t best;
    uint32_t rate_index, width_index, grid_index;
    float best_quality = -1.0f, best_offset = 0.0f;
    uint32_t best_type_mask = 0U;
    double best_scale = 1.0;
    if (iq == NULL || workspace == NULL || result == NULL || relative_cfo_hz == NULL ||
        sample_rate_hz <= rates[0] || capacity < 4096U) return WRJ_ERR_ARGUMENT;
    while (fft_size < count && fft_size < (1U << 22U)) fft_size <<= 1U;
    if (fft_size < count) return WRJ_ERR_ARGUMENT;
    shifted = malloc((size_t)count * sizeof(*shifted));
    filtered = malloc((size_t)count * sizeof(*filtered));
    downsampled = malloc((size_t)capacity * sizeof(*downsampled));
    fft_re = malloc((size_t)fft_size * sizeof(*fft_re));
    fft_im = malloc((size_t)fft_size * sizeof(*fft_im));
    groups = calloc(M3_BLE_BANK_GROUPS, sizeof(*groups));
    if (shifted == NULL || filtered == NULL || downsampled == NULL ||
        fft_re == NULL || fft_im == NULL || groups == NULL) {
        free(shifted); free(filtered); free(downsampled); free(fft_re); free(fft_im);
        free(groups);
        return WRJ_ERR_MEMORY;
    }
    memset(&best, 0, sizeof(best));
    for (rate_index = 0U; rate_index < WRJ_ARRAY_COUNT(rates); ++rate_index) {
      const float target_rate_hz = rates[rate_index];
      const uint32_t output_sps = (uint32_t)lroundf(target_rate_hz / 1000000.0f);
      const uint32_t output_count = (uint32_t)ceil((double)count * output_sps / input_sps);
      const double scale = (double)input_sps / output_sps;
      const float effective_rate_hz = sample_rate_hz *
          (float)output_sps / (float)input_sps;
      for (width_index = 0U; width_index < WRJ_ARRAY_COUNT(widths); ++width_index) {
        m3_ble_bank_coefficients(sample_rate_hz, effective_rate_hz, coeff);
        for (grid_index = 0U; grid_index < WRJ_ARRAY_COUNT(relative_grid); ++grid_index) {
            m3_result_t trial;
            const float offset = relative_grid[grid_index];
            float quality;
            uint32_t type_mask = 0U, packet;
            memset(&trial, 0, sizeof(trial));
            trial.ble_soft_recovery_enabled = 1U;
            m3_cfo_compensate(iq, shifted, count, sample_rate_hz, offset);
            if (m3_ble_bank_spectral_filter(shifted, count, fft_size, sample_rate_hz,
                    widths[width_index], fft_re, fft_im, filtered) != WRJ_OK) continue;
            m3_ble_bank_resample(filtered, count, input_sps, output_sps, coeff,
                                 downsampled, output_count);
            if (m3_remoteid_ble_synchronize(downsampled, output_count,
                    target_rate_hz, max_frames, workspace, &trial) != WRJ_OK) continue;
            for (packet = 0U; packet < workspace->ble_soft_observation_count; ++packet) {
                m3_ble_bank_add_soft(groups, &group_count,
                    (uint32_t)lround((double)workspace->ble_soft_start[packet] * scale),
                    workspace->ble_soft_score[packet],
                    workspace->ble_soft_bits[packet],
                    WRJ_MAX(2U, (uint32_t)lround(0.65 * input_sps)));
            }
            for (packet = 0U; packet < trial.ble_verified_packet_count; ++packet) {
                const uint8_t type = trial.ble_verified_pdu[packet][14] >> 4U;
                if (type < 8U) type_mask |= 1U << type;
            }
            if (trial.crc_success_count == 0U) continue;
            quality = trial.peak_metric;
            if (trial.ble_verified_packet_count > best.ble_verified_packet_count ||
                (trial.ble_verified_packet_count == best.ble_verified_packet_count &&
                 m3_ble_bank_type_score(type_mask) >
                    m3_ble_bank_type_score(best_type_mask)) ||
                (trial.ble_verified_packet_count == best.ble_verified_packet_count &&
                 type_mask == best_type_mask && isfinite(preferred_offset_hz) &&
                 fabsf(preferred_offset_hz) <= 50000.0f &&
                 quality + 0.05f >= best_quality &&
                 fabsf(offset - preferred_offset_hz) <
                    fabsf(best_offset - preferred_offset_hz)) ||
                (trial.ble_verified_packet_count == best.ble_verified_packet_count &&
                 type_mask == best_type_mask &&
                 (!isfinite(preferred_offset_hz) || fabsf(preferred_offset_hz) > 50000.0f) &&
                 quality > best_quality)) {
                best = trial;
                best_quality = quality;
                best_offset = offset;
                best_scale = scale;
                best_type_mask = type_mask;
            }
        }
      }
    }
    if (best.crc_success_count > 0U) {
        for (grid_index = 0U; grid_index < best.ble_verified_packet_count; ++grid_index)
            best.ble_verified_start[grid_index] = (uint32_t)lround(
                (double)best.ble_verified_start[grid_index] * best_scale);
        m3_ble_bank_fuse(groups, group_count, &best);
    }
    free(shifted); free(filtered); free(downsampled); free(fft_re); free(fft_im);
    free(groups);
    if (best.crc_success_count == 0U) {
        return WRJ_ERR_DATA;
    }
    for (grid_index = 0U; grid_index < best.num_frames; ++grid_index)
        best.frame_start_samples_0based[grid_index] =
            (uint32_t)lround((double)best.frame_start_samples_0based[grid_index] * best_scale);
    best.frame_length_samples = (uint32_t)lround((double)best.frame_length_samples * best_scale);
    best.symbol_timing_offset = (float)((double)best.symbol_timing_offset * best_scale);
    *result = best;
    *relative_cfo_hz = best_offset;
    return WRJ_OK;
}
