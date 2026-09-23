#include "m3_module3.h"

#include <stdlib.h>

static int m3_compare_float(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    return (a > b) - (a < b);
}

static float m3_quantile(float *scratch, const float *values, uint32_t count, float probability)
{
    float position;
    uint32_t lower;
    uint32_t upper;
    if (scratch != values) {
        memcpy(scratch, values, sizeof(float) * (size_t)count);
    }
    qsort(scratch, count, sizeof(float), m3_compare_float);
    position = probability * (float)(count - 1U);
    lower = (uint32_t)floorf(position);
    upper = (uint32_t)ceilf(position);
    return scratch[lower] + (position - (float)lower) * (scratch[upper] - scratch[lower]);
}

static uint32_t m3_make_odd(uint32_t value)
{
    value = WRJ_MAX(1U, value);
    return (value & 1U) == 0U ? value + 1U : value;
}

static uint32_t m3_next_power_of_two(uint32_t value)
{
    uint32_t result = 1U;
    while (result < value && result <= UINT32_MAX / 2U) {
        result <<= 1U;
    }
    return result;
}

static void m3_boxcar_same(const float *input, uint32_t count, uint32_t width, float *output)
{
    uint32_t index;
    const int32_t half = (int32_t)(width / 2U);
    double rolling = 0.0;
    for (index = 0U; index < count; ++index) {
        const int32_t add = (int32_t)index + half;
        const int32_t remove = (int32_t)index - half - 1;
        if (add >= 0 && add < (int32_t)count) {
            rolling += input[add];
        }
        if (remove >= 0 && remove < (int32_t)count) {
            rolling -= input[remove];
        }
        output[index] = (float)(rolling / (double)width);
    }
}

static float m3_weighted_frequency_quantile(const float *weights, uint32_t count,
                                            uint32_t first_bin, uint32_t fft_size,
                                            float sample_rate_hz, float probability)
{
    uint32_t index;
    double total = 0.0;
    double cumulative = 0.0;
    for (index = 0U; index < count; ++index) {
        total += weights[index];
    }
    if (total <= 1.0e-20) {
        return 0.0f;
    }
    for (index = 0U; index < count; ++index) {
        cumulative += weights[index];
        if (cumulative >= probability * total) {
            return ((float)(first_bin + index) - (float)(fft_size / 2U)) *
                sample_rate_hz / (float)fft_size;
        }
    }
    return ((float)(first_bin + count - 1U) - (float)(fft_size / 2U)) *
        sample_rate_hz / (float)fft_size;
}

void m3_cfo_compensate(const wrj_cf32_t *input, wrj_cf32_t *output, uint32_t count,
                       float sample_rate_hz, float correction_hz)
{
    uint32_t index;
    const double phase_step = -2.0 * WRJ_PI * (double)correction_hz / (double)sample_rate_hz;

    if (input == NULL || output == NULL || sample_rate_hz <= 0.0f) {
        return;
    }
    for (index = 0U; index < count; ++index) {
        const double angle = phase_step * (double)index;
        const float cr = (float)cos(angle);
        const float ci = (float)sin(angle);
        const float re = input[index].re;
        const float im = input[index].im;
        output[index].re = re * cr - im * ci;
        output[index].im = re * ci + im * cr;
    }
}

wrj_status_t m3_bandlimit_fir(wrj_cf32_t *iq, uint32_t count, float sample_rate_hz,
                              float bandwidth_hz, m3_workspace_t *workspace)
{
    enum { TAPS = 289, HALF = 144 };
    float taps[TAPS];
    double sum = 0.0;
    uint32_t index;
    const float pass = WRJ_MIN(0.44f * sample_rate_hz, WRJ_MAX(600000.0f, 0.62f * bandwidth_hz));
    const float stop = WRJ_MIN(0.48f * sample_rate_hz, WRJ_MAX(pass + 200000.0f, 0.78f * bandwidth_hz));
    const float cutoff = 0.5f * (pass + stop) / sample_rate_hz;
    if (iq == NULL || workspace == NULL || count > workspace->max_samples || sample_rate_hz <= 0.0f) {
        return WRJ_ERR_ARGUMENT;
    }
    for (index = 0U; index < TAPS; ++index) {
        const int32_t k = (int32_t)index - HALF;
        const double ideal = k == 0 ? 2.0 * cutoff :
            sin(2.0 * WRJ_PI * cutoff * (double)k) / (WRJ_PI * (double)k);
        const double window = 0.5 - 0.5 * cos(2.0 * WRJ_PI * (double)index / (double)(TAPS - 1));
        taps[index] = (float)(ideal * window);
        sum += taps[index];
    }
    for (index = 0U; index < TAPS; ++index) {
        taps[index] = (float)(taps[index] / sum);
    }
    for (index = 0U; index < count; ++index) {
        workspace->metric[index] = iq[index].re;
        workspace->scratch[index] = iq[index].im;
    }
    for (index = 0U; index < count; ++index) {
        int32_t tap;
        double re = 0.0;
        double im = 0.0;
        for (tap = -HALF; tap <= HALF; ++tap) {
            const int64_t source = (int64_t)index + tap;
            if (source >= 0 && source < (int64_t)count) {
                const float coefficient = taps[tap + HALF];
                re += coefficient * workspace->metric[source];
                im += coefficient * workspace->scratch[source];
            }
        }
        iq[index].re = (float)re;
        iq[index].im = (float)im;
    }
    return WRJ_OK;
}

wrj_status_t m3_estimate_spectral_center(const wrj_cf32_t *iq, uint32_t count,
                                         float sample_rate_hz, float bandwidth_hz,
                                         m3_workspace_t *workspace, float *offset_hz)
{
    const uint32_t fft_size = workspace == NULL ? 0U : workspace->spectrum_length;
    uint32_t available_blocks;
    uint32_t block_count;
    uint32_t block;
    uint32_t bin;
    uint32_t mask_first = 0U;
    uint32_t mask_count = 0U;
    uint32_t smooth_width;
    uint32_t gap_bins;
    float bin_hz;
    float search_half;
    float center_search_half;
    float noise;
    float floor_level;
    float high_level;
    float edge_center = NAN;
    float edge_quality = 0.0f;
    float symmetry_center = 0.0f;
    double best_run_score = -INFINITY;
    if (iq == NULL || workspace == NULL || offset_hz == NULL || count == 0U ||
        fft_size == 0U || sample_rate_hz <= 0.0f) {
        return WRJ_ERR_ARGUMENT;
    }
    memset(workspace->power, 0, sizeof(float) * (size_t)fft_size);
    bin_hz = sample_rate_hz / (float)fft_size;
    available_blocks = count >= fft_size ? (count - fft_size) / (fft_size / 2U) + 1U : 0U;
    block_count = WRJ_MIN(workspace->spectrum_blocks, available_blocks);
    if (block_count == 0U) {
        return WRJ_ERR_DATA;
    }
    for (block = 0U; block < block_count; ++block) {
        const uint32_t block_ordinal = available_blocks > block_count && block_count > 1U ?
            (uint32_t)llround(1.0 + (double)block * (double)(available_blocks - 1U) /
                              (double)(block_count - 1U)) : block + 1U;
        const uint32_t start = (block_ordinal - 1U) * (fft_size / 2U);
        double block_energy = 0.0;
        for (bin = 0U; bin < fft_size; ++bin) {
            const uint32_t sample = start + bin;
            const float window = 0.5f - 0.5f * cosf(2.0f * (float)WRJ_PI * (float)bin /
                                                     (float)WRJ_MAX(1U, fft_size - 1U));
            workspace->fft_re[bin] = (sample < count) ? iq[sample].re * window : 0.0f;
            workspace->fft_im[bin] = (sample < count) ? iq[sample].im * window : 0.0f;
            block_energy += (double)workspace->fft_re[bin] * workspace->fft_re[bin] +
                (double)workspace->fft_im[bin] * workspace->fft_im[bin];
        }
        if (m3_fft_forward_radix2(workspace->fft_re, workspace->fft_im, fft_size) != WRJ_OK) {
            return WRJ_ERR_DATA;
        }
        for (bin = 0U; bin < fft_size; ++bin) {
            const uint32_t raw_bin = (bin + fft_size / 2U) % fft_size;
            const float value = wrj_complex_abs2(workspace->fft_re[raw_bin], workspace->fft_im[raw_bin]);
            workspace->spectrum_block_power[(size_t)bin * workspace->spectrum_blocks + block] = value;
            workspace->power[bin] += value;
        }
        workspace->spectrum_block_energy[block] = (float)(block_energy / (double)fft_size);
    }
    for (bin = 0U; bin < fft_size; ++bin) {
        workspace->power[bin] /= (float)block_count;
    }

    search_half = WRJ_MIN(0.46f * sample_rate_hz, WRJ_MAX(1200000.0f, 1.20f * bandwidth_hz));
    center_search_half = WRJ_MIN(0.92f * search_half, WRJ_MAX(3000000.0f, 0.55f * bandwidth_hz));
    for (bin = 0U; bin < fft_size; ++bin) {
        const float frequency = ((float)bin - (float)(fft_size / 2U)) * bin_hz;
        if (fabsf(frequency) <= search_half) {
            if (mask_count == 0U) {
                mask_first = bin;
            }
            workspace->spectrum_aux[mask_count++] = workspace->power[bin];
        }
    }
    if (mask_count < 8U) {
        *offset_hz = 0.0f;
        return WRJ_OK;
    }
    noise = m3_quantile(workspace->scratch, workspace->spectrum_aux, mask_count, 0.25f);
    for (bin = 0U; bin < mask_count; ++bin) {
        workspace->spectrum_weight[bin] = WRJ_MAX(workspace->spectrum_aux[bin] - 1.10f * noise, 0.0f);
    }
    smooth_width = WRJ_MAX(5U, m3_make_odd((uint32_t)lroundf((float)mask_count / 700.0f)));
    m3_boxcar_same(workspace->spectrum_aux, mask_count, smooth_width, workspace->spectrum_smooth);
    floor_level = m3_quantile(workspace->scratch, workspace->spectrum_smooth, mask_count, 0.18f);
    high_level = m3_quantile(workspace->scratch, workspace->spectrum_smooth, mask_count, 0.85f);
    for (bin = 0U; bin < mask_count; ++bin) {
        workspace->flags[bin] = (uint8_t)(workspace->spectrum_smooth[bin] >
            floor_level + 0.07f * WRJ_MAX(high_level - floor_level, 1.0e-20f));
    }
    gap_bins = WRJ_MAX(2U, (uint32_t)lroundf(0.22f * bandwidth_hz / bin_hz));
    for (bin = 0U; bin < mask_count;) {
        if (workspace->flags[bin] == 0U) {
            const uint32_t begin = bin;
            while (bin < mask_count && workspace->flags[bin] == 0U) {
                ++bin;
            }
            if (begin > 0U && bin < mask_count && bin - begin <= gap_bins) {
                uint32_t fill;
                for (fill = begin; fill < bin; ++fill) {
                    workspace->flags[fill] = 1U;
                }
            }
        } else {
            ++bin;
        }
    }
    for (bin = 0U; bin < mask_count;) {
        uint32_t begin;
        uint32_t end;
        float first_frequency;
        float last_frequency;
        float width_hz;
        float center_hz;
        double run_excess = 0.0;
        double mean_power = 0.0;
        double width_penalty;
        double run_score;
        uint32_t q;
        if (workspace->flags[bin] == 0U) {
            ++bin;
            continue;
        }
        begin = bin;
        while (bin < mask_count && workspace->flags[bin] != 0U) {
            ++bin;
        }
        end = bin - 1U;
        first_frequency = ((float)(mask_first + begin) - (float)(fft_size / 2U)) * bin_hz;
        last_frequency = ((float)(mask_first + end) - (float)(fft_size / 2U)) * bin_hz;
        width_hz = last_frequency - first_frequency + bin_hz;
        center_hz = 0.5f * (first_frequency + last_frequency);
        if (width_hz < WRJ_MAX(350000.0f, 0.12f * bandwidth_hz) ||
            width_hz > WRJ_MIN(1.8f * bandwidth_hz, 0.95f * sample_rate_hz) ||
            fabsf(center_hz) > center_search_half) {
            continue;
        }
        for (q = begin; q <= end; ++q) {
            run_excess += WRJ_MAX(workspace->spectrum_smooth[q] - floor_level, 0.0f);
            mean_power += workspace->spectrum_smooth[q];
        }
        mean_power /= (double)(end - begin + 1U);
        width_penalty = exp(-0.5 * pow(log(WRJ_MAX(width_hz, 1.0f) /
            WRJ_MAX(bandwidth_hz, 1.0f)) / 0.95, 2.0));
        run_score = run_excess * width_penalty;
        if (run_score > best_run_score) {
            best_run_score = run_score;
            edge_center = center_hz;
            edge_quality = wrj_clip01((float)((mean_power - floor_level) /
                WRJ_MAX(high_level - floor_level, 1.0e-20f) * width_penalty));
        }
    }
    {
        const float low = m3_weighted_frequency_quantile(workspace->spectrum_weight, mask_count,
            mask_first, fft_size, sample_rate_hz, 0.05f);
        const float high = m3_weighted_frequency_quantile(workspace->spectrum_weight, mask_count,
            mask_first, fft_size, sample_rate_hz, 0.95f);
        const float mid_edge = 0.5f * (low + high);
        if (!(isfinite(edge_center) && edge_quality >= 0.12f)) {
            const uint32_t convolution_length = m3_next_power_of_two(2U * mask_count - 1U);
            uint32_t low_index = 0U;
            uint32_t high_index = mask_count - 1U;
            uint32_t best_index = 0U;
            float best_value = -INFINITY;
            m3_boxcar_same(workspace->spectrum_weight, mask_count, smooth_width,
                           workspace->spectrum_smooth);
            if (convolution_length <= workspace->fft_size) {
                for (bin = 0U; bin < convolution_length; ++bin) {
                    workspace->fft_re[bin] = bin < mask_count ? workspace->spectrum_smooth[bin] : 0.0f;
                    workspace->fft_im[bin] = 0.0f;
                }
                if (m3_fft_forward_radix2(workspace->fft_re, workspace->fft_im,
                                          convolution_length) == WRJ_OK) {
                    for (bin = 0U; bin < convolution_length; ++bin) {
                        const float re = workspace->fft_re[bin];
                        const float im = workspace->fft_im[bin];
                        workspace->fft_re[bin] = re * re - im * im;
                        workspace->fft_im[bin] = 2.0f * re * im;
                    }
                    for (bin = 0U; bin < convolution_length; ++bin) {
                        workspace->fft_im[bin] = -workspace->fft_im[bin];
                    }
                    (void)m3_fft_forward_radix2(workspace->fft_re, workspace->fft_im,
                                                convolution_length);
                    for (bin = 0U; bin < convolution_length; ++bin) {
                        workspace->fft_re[bin] /= (float)convolution_length;
                    }
                    for (bin = 0U; bin < mask_count; ++bin) {
                        const float frequency = ((float)(mask_first + bin) -
                            (float)(fft_size / 2U)) * bin_hz;
                        if (fabsf(frequency) <= center_search_half) {
                            low_index = bin;
                            break;
                        }
                    }
                    for (bin = mask_count; bin-- > 0U;) {
                        const float frequency = ((float)(mask_first + bin) -
                            (float)(fft_size / 2U)) * bin_hz;
                        if (fabsf(frequency) <= center_search_half) {
                            high_index = bin;
                            break;
                        }
                    }
                    for (bin = low_index; bin <= high_index; ++bin) {
                        const uint32_t lag = 2U * bin;
                        if (lag < convolution_length && workspace->fft_re[lag] > best_value) {
                            best_value = workspace->fft_re[lag];
                            best_index = bin;
                        }
                    }
                    symmetry_center = ((float)(mask_first + best_index) -
                        (float)(fft_size / 2U)) * bin_hz;
                    if (best_index > low_index && best_index < high_index) {
                        const float y1 = workspace->fft_re[2U * (best_index - 1U)];
                        const float y2 = workspace->fft_re[2U * best_index];
                        const float y3 = workspace->fft_re[2U * (best_index + 1U)];
                        const float denominator = y1 - 2.0f * y2 + y3;
                        if (fabsf(denominator) > 1.0e-20f) {
                            const float fraction = WRJ_CLAMP(0.5f * (y1 - y3) / denominator,
                                                             -0.5f, 0.5f);
                            symmetry_center += fraction * bin_hz;
                        }
                    }
                }
            }
            *offset_hz = 0.85f * symmetry_center + 0.15f * mid_edge;
        } else {
            *offset_hz = edge_center;
        }
    }
    {
        const float energy_cut = m3_quantile(workspace->scratch, workspace->spectrum_block_energy,
                                             block_count, 0.45f);
        uint32_t kept = 0U;
        for (block = 0U; block < block_count; ++block) {
            kept += (uint32_t)(workspace->spectrum_block_energy[block] >= energy_cut);
        }
        if (kept > 0U) {
            float active_noise;
            float active_low;
            float active_high;
            float active_center;
            for (bin = 0U; bin < mask_count; ++bin) {
                double sum = 0.0;
                for (block = 0U; block < block_count; ++block) {
                    if (workspace->spectrum_block_energy[block] >= energy_cut) {
                        sum += workspace->spectrum_block_power[
                            (size_t)(mask_first + bin) * workspace->spectrum_blocks + block];
                    }
                }
                workspace->spectrum_smooth[bin] = (float)(sum / (double)kept);
            }
            active_noise = m3_quantile(workspace->scratch, workspace->spectrum_smooth, mask_count, 0.20f);
            for (bin = 0U; bin < mask_count; ++bin) {
                workspace->spectrum_smooth[bin] = WRJ_MAX(workspace->spectrum_smooth[bin] - active_noise, 0.0f);
            }
            active_low = m3_weighted_frequency_quantile(workspace->spectrum_smooth, mask_count,
                mask_first, fft_size, sample_rate_hz, 0.04f);
            active_high = m3_weighted_frequency_quantile(workspace->spectrum_smooth, mask_count,
                mask_first, fft_size, sample_rate_hz, 0.96f);
            active_center = 0.5f * (active_low + active_high);
            if (fabsf(active_center) > 1500000.0f && fabsf(active_center - *offset_hz) > 1000000.0f) {
                *offset_hz = active_center;
            }
        }
    }
    *offset_hz = WRJ_CLAMP(*offset_hz, -0.45f * sample_rate_hz, 0.45f * sample_rate_hz);
    return WRJ_OK;
}

wrj_status_t m3_integer_cfo_search(const wrj_cf32_t *iq, uint32_t count,
                                   float sample_rate_hz, float subcarrier_spacing_hz,
                                   m3_workspace_t *workspace, int32_t *selected_index,
                                   float *selected_offset_hz)
{
    int32_t candidate;
    float baseline_score = 0.0f;
    float best_score = -1.0f;
    int32_t best_index = 0;
    (void)workspace;
    if (selected_index == NULL || selected_offset_hz == NULL) {
        return WRJ_ERR_ARGUMENT;
    }
    if (iq == NULL || count < 64U || sample_rate_hz <= 0.0f || subcarrier_spacing_hz <= 0.0f) {
        return WRJ_ERR_ARGUMENT;
    }
    for (candidate = -3; candidate <= 3; ++candidate) {
        const double rotation = -2.0 * WRJ_PI * (double)candidate *
            (double)subcarrier_spacing_hz / (double)sample_rate_hz;
        double sum_re = 0.0;
        double sum_im = 0.0;
        double weight = 0.0;
        uint32_t index;
        for (index = 0U; index + 4U < count; index += 4U) {
            const wrj_cf32_t a = iq[index];
            const wrj_cf32_t b = iq[index + 4U];
            const double magnitude_a = sqrt((double)a.re * a.re + (double)a.im * a.im) + 1.0e-12;
            const double magnitude_b = sqrt((double)b.re * b.re + (double)b.im * b.im) + 1.0e-12;
            const double phase_a = 4.0 * atan2((double)a.im, (double)a.re);
            const double phase_b = 4.0 * atan2((double)b.im, (double)b.re);
            const double delta = phase_b - phase_a + 16.0 * rotation;
            const double w = fmin(magnitude_a, magnitude_b);
            sum_re += w * cos(delta);
            sum_im += w * sin(delta);
            weight += w;
        }
        if (weight > 0.0) {
            const float coherence = (float)(sqrt(sum_re * sum_re + sum_im * sum_im) / weight);
            const float residual = (float)fabs(atan2(sum_im, sum_re));
            const float score = coherence * expf(-residual * residual / 0.25f);
            if (candidate == 0) {
                baseline_score = score;
            }
            if (score > best_score) {
                best_score = score;
                best_index = candidate;
            }
        }
    }
    /* Integer translation is accepted only with a clear modulation-evidence
     * gain; otherwise CP/OFDM ambiguity is explicitly left at k=0. */
    if (best_index != 0 && best_score >= baseline_score + 0.08f) {
        *selected_index = best_index;
        *selected_offset_hz = (float)best_index * subcarrier_spacing_hz;
    } else {
        *selected_index = 0;
        *selected_offset_hz = 0.0f;
    }
    return WRJ_OK;
}
