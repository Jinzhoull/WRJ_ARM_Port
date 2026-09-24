#include "m3_module3.h"
#include "common/perf_timer.h"

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
    float position;
    uint32_t lower;
    uint32_t upper;
    if (count == 0U) {
        return 0.0f;
    }
    if (scratch != values) {
        memcpy(scratch, values, sizeof(float) * (size_t)count);
    }
    qsort(scratch, count, sizeof(float), m3_float_compare);
    position = probability * (float)(count - 1U);
    lower = (uint32_t)floorf(position);
    upper = (uint32_t)ceilf(position);
    return scratch[lower] + (position - (float)lower) * (scratch[upper] - scratch[lower]);
}

static wrj_status_t m3_cp_metric(const wrj_cf32_t *iq, uint32_t count,
                                 uint32_t nfft, uint32_t cp_samples,
                                 float *metric, uint32_t *metric_count)
{
    uint32_t index;
    double sum_re = 0.0;
    double sum_im = 0.0;
    double energy_a = 0.0;
    double energy_b = 0.0;
    if (count <= nfft + cp_samples) {
        return WRJ_ERR_ARGUMENT;
    }
    *metric_count = count - nfft - cp_samples + 1U;
    M3_PERF_COUNT(M3_PERF_OP_CORRELATION_CALLS, 1U);
    M3_PERF_COUNT(M3_PERF_OP_CORRELATION_EVALUATIONS, *metric_count);
    M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES, *metric_count);
    for (index = 0U; index < cp_samples; ++index) {
        const wrj_cf32_t a = iq[index];
        const wrj_cf32_t b = iq[index + nfft];
        sum_re += (double)a.re * b.re + (double)a.im * b.im;
        sum_im += (double)a.re * b.im - (double)a.im * b.re;
        energy_a += wrj_complex_abs2(a.re, a.im);
        energy_b += wrj_complex_abs2(b.re, b.im);
    }
    for (index = 0U; index < *metric_count; ++index) {
        metric[index] = (float)(sqrt(sum_re * sum_re + sum_im * sum_im) /
            (sqrt(energy_a * energy_b) + 1.0e-20));
        if (index + 1U < *metric_count) {
            const wrj_cf32_t old_a = iq[index];
            const wrj_cf32_t old_b = iq[index + nfft];
            const wrj_cf32_t new_a = iq[index + cp_samples];
            const wrj_cf32_t new_b = iq[index + cp_samples + nfft];
            sum_re += (double)new_a.re * new_b.re + (double)new_a.im * new_b.im -
                (double)old_a.re * old_b.re - (double)old_a.im * old_b.im;
            sum_im += (double)new_a.re * new_b.im - (double)new_a.im * new_b.re -
                (double)old_a.re * old_b.im + (double)old_a.im * old_b.re;
            energy_a += wrj_complex_abs2(new_a.re, new_a.im) - wrj_complex_abs2(old_a.re, old_a.im);
            energy_b += wrj_complex_abs2(new_b.re, new_b.im) - wrj_complex_abs2(old_b.re, old_b.im);
        }
    }
    return WRJ_OK;
}

static int m3_has_periodic_neighbour(const m3_peak_t *peaks, uint32_t count,
                                     uint32_t index, uint32_t symbol_length,
                                     uint32_t tolerance)
{
    uint32_t other;
    for (other = 0U; other < count; ++other) {
        uint32_t distance;
        uint32_t multiple;
        if (other == index) {
            continue;
        }
        distance = peaks[other].index > peaks[index].index ?
            peaks[other].index - peaks[index].index : peaks[index].index - peaks[other].index;
        for (multiple = 1U; multiple <= 3U; ++multiple) {
            const uint32_t expected = multiple * symbol_length;
            if (distance >= expected - tolerance && distance <= expected + tolerance) {
                return 1;
            }
        }
    }
    return 0;
}

wrj_status_t m3_cp_synchronize(const wrj_cf32_t *iq, uint32_t count,
                               uint32_t nfft, uint32_t cp_samples,
                               uint32_t max_frames, m3_workspace_t *workspace,
                               m3_result_t *result)
{
    uint32_t metric_count = 0U;
    uint32_t candidate_count = 0U;
    uint32_t selected_count = 0U;
    uint32_t kept_count = 0U;
    uint32_t index;
    uint32_t output_count;
    const uint32_t symbol_length = nfft + cp_samples;
    const uint32_t min_distance = WRJ_MAX(4U, (6U * symbol_length) / 10U);
    const uint32_t tolerance = WRJ_MAX(4U, (8U * symbol_length) / 100U);
    float base_level;
    float mad_level;
    float threshold;
    float strong_cut;
    float peak_metric = 0.0f;
    float support;
    wrj_status_t status;
    M3_PERF_TIMER(fractional_timer);
    M3_PERF_TIMER(alignment_timer);

    if (iq == NULL || workspace == NULL || result == NULL || nfft < 8U || cp_samples < 4U ||
        max_frames == 0U || count > workspace->max_samples) {
        return WRJ_ERR_ARGUMENT;
    }
    status = m3_cp_metric(iq, count, nfft, cp_samples, workspace->metric, &metric_count);
    if (status != WRJ_OK) {
        return status;
    }
    base_level = m3_quantile(workspace->scratch, workspace->metric, metric_count, 0.5f);
    for (index = 0U; index < metric_count; ++index) {
        workspace->scratch[index] = fabsf(workspace->metric[index] - base_level);
    }
    mad_level = m3_quantile(workspace->scratch, workspace->scratch, metric_count, 0.5f) + 1.0e-7f;
    threshold = WRJ_MAX(base_level + 3.0f * mad_level,
                        m3_quantile(workspace->scratch, workspace->metric, metric_count, 0.90f));

    for (index = 1U; index + 1U < metric_count && candidate_count < workspace->peak_capacity; ++index) {
        if (workspace->metric[index] >= threshold &&
            workspace->metric[index] >= workspace->metric[index - 1U] &&
            workspace->metric[index] >= workspace->metric[index + 1U]) {
            workspace->peak_candidates[candidate_count].index = index;
            workspace->peak_candidates[candidate_count].value = workspace->metric[index];
            ++candidate_count;
        }
    }
    if (candidate_count == 0U) {
        uint32_t best = 0U;
        for (index = 1U; index < metric_count; ++index) {
            if (workspace->metric[index] > workspace->metric[best]) {
                best = index;
            }
        }
        workspace->peak_candidates[0].index = best;
        workspace->peak_candidates[0].value = workspace->metric[best];
        candidate_count = 1U;
    }
    M3_PERF_START(alignment_timer);
    qsort(workspace->peak_candidates, candidate_count, sizeof(m3_peak_t), m3_peak_value_desc);
    for (index = 0U; index < candidate_count && selected_count < workspace->selected_peak_capacity; ++index) {
        uint32_t other;
        int separated = 1;
        for (other = 0U; other < selected_count; ++other) {
            const uint32_t distance = workspace->peak_selected[other].index > workspace->peak_candidates[index].index ?
                workspace->peak_selected[other].index - workspace->peak_candidates[index].index :
                workspace->peak_candidates[index].index - workspace->peak_selected[other].index;
            if (distance < min_distance) {
                separated = 0;
                break;
            }
        }
        if (separated != 0) {
            workspace->peak_selected[selected_count++] = workspace->peak_candidates[index];
        }
    }
    qsort(workspace->peak_selected, selected_count, sizeof(m3_peak_t), m3_peak_index_asc);
    M3_PERF_STOP(M3_PERF_FRAME_ALIGNMENT, alignment_timer);
    for (index = 0U; index < selected_count; ++index) {
        workspace->scratch[index] = workspace->peak_selected[index].value;
    }
    strong_cut = m3_quantile(workspace->metric, workspace->scratch, selected_count, 0.85f);
    for (index = 0U; index < selected_count; ++index) {
        const int periodic = m3_has_periodic_neighbour(workspace->peak_selected, selected_count,
                                                       index, symbol_length, tolerance);
        workspace->flags[index] = (uint8_t)(periodic != 0 ||
            workspace->peak_selected[index].value >= strong_cut);
        if (workspace->flags[index] != 0U) {
            workspace->peak_selected[kept_count++] = workspace->peak_selected[index];
        }
    }
    if (kept_count == 0U) {
        return WRJ_ERR_DATA;
    }
    support = (float)kept_count / (float)selected_count;
    M3_PERF_START(fractional_timer);
    for (index = 0U; index < kept_count; ++index) {
        peak_metric = WRJ_MAX(peak_metric, workspace->peak_selected[index].value);
    }
    result->sync_confidence = wrj_clip01(
        0.65f / (1.0f + expf(-(((peak_metric - base_level) / mad_level - 3.0f) / 2.0f))) +
        0.35f * support);
    result->peak_metric = peak_metric;
    result->frame_length_samples = symbol_length;
    snprintf(result->sync_method, sizeof(result->sync_method), "local_CP_peaks_with_periodic_support");

    for (index = 0U; index < kept_count; ++index) {
        uint32_t j;
        const uint32_t start = workspace->peak_selected[index].index;
        double sr = 0.0;
        double si = 0.0;
        double ea = 0.0;
        double eb = 0.0;
        for (j = 0U; j < cp_samples; ++j) {
            const wrj_cf32_t a = iq[start + j];
            const wrj_cf32_t b = iq[start + j + nfft];
            sr += (double)a.re * b.re + (double)a.im * b.im;
            si += (double)a.re * b.im - (double)a.im * b.re;
            ea += wrj_complex_abs2(a.re, a.im);
            eb += wrj_complex_abs2(b.re, b.im);
        }
        workspace->scratch[index] = (float)(sqrt(sr * sr + si * si) / (sqrt(ea * eb) + 1.0e-20));
    }
    {
        const float coherence_cut = m3_quantile(workspace->metric, workspace->scratch, kept_count, 0.25f);
        double phase_re = 0.0;
        double phase_im = 0.0;
        for (index = 0U; index < kept_count; ++index) {
            uint32_t j;
            const uint32_t start = workspace->peak_selected[index].index;
            const float coherence = workspace->scratch[index];
            double sr = 0.0;
            double si = 0.0;
            double magnitude;
            if (coherence < coherence_cut) {
                continue;
            }
            for (j = 0U; j < cp_samples; ++j) {
                const wrj_cf32_t a = iq[start + j];
                const wrj_cf32_t b = iq[start + j + nfft];
                sr += (double)a.re * b.re + (double)a.im * b.im;
                si += (double)a.re * b.im - (double)a.im * b.re;
            }
            magnitude = sqrt(sr * sr + si * si) + 1.0e-20;
            phase_re += (double)(coherence * coherence) * sr / magnitude;
            phase_im += (double)(coherence * coherence) * si / magnitude;
        }
        result->fractional_cfo_hz = (float)(atan2(phase_im, phase_re) /
            (2.0 * WRJ_PI * (double)nfft));
    }
    M3_PERF_STOP(M3_PERF_FRACTIONAL_CFO, fractional_timer);

    for (index = 0U; index < kept_count; ++index) {
        workspace->peak_candidates[index].index = workspace->peak_selected[index].index;
        workspace->peak_candidates[index].value = wrj_clip01(
            (workspace->peak_selected[index].value - base_level) / WRJ_MAX(1.0f - base_level, 0.05f));
    }
    M3_PERF_START(alignment_timer);
    qsort(workspace->peak_candidates, kept_count, sizeof(m3_peak_t), m3_peak_value_desc);
    output_count = WRJ_MIN(WRJ_MIN(max_frames, WRJ_MAX_FRAMES), kept_count);
    memcpy(workspace->peak_selected, workspace->peak_candidates,
           sizeof(m3_peak_t) * (size_t)output_count);
    qsort(workspace->peak_selected, output_count, sizeof(m3_peak_t), m3_peak_index_asc);
    M3_PERF_STOP(M3_PERF_FRAME_ALIGNMENT, alignment_timer);
    for (index = 0U; index < output_count; ++index) {
        result->frame_start_samples_0based[index] = workspace->peak_selected[index].index;
        result->frame_confidence[index] = workspace->peak_selected[index].value;
    }
    result->num_frames = output_count;
    return WRJ_OK;
}
