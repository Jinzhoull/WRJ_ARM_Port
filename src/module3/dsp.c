#include "m3_module3.h"
#include "common/perf_timer.h"

#include <stdlib.h>

static int m3_float_compare(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    return (a > b) - (a < b);
}

static float m3_sorted_quantile(const float *sorted, uint32_t count, float probability)
{
    const float position = probability * (float)(count - 1U);
    const uint32_t lower = (uint32_t)floorf(position);
    const uint32_t upper = (uint32_t)ceilf(position);
    const float fraction = position - (float)lower;
    return sorted[lower] + fraction * (sorted[upper] - sorted[lower]);
}

static float m3_quantile(float *scratch, const float *values, uint32_t count, float probability)
{
    if (scratch != values) {
        memcpy(scratch, values, sizeof(float) * (size_t)count);
    }
    qsort(scratch, count, sizeof(scratch[0]), m3_float_compare);
    return m3_sorted_quantile(scratch, count, probability);
}

wrj_status_t m3_preprocess_iq(wrj_cf32_t *iq, uint32_t count,
                              m3_workspace_t *workspace)
{
    uint32_t index;
    double dc_re = 0.0;
    double dc_im = 0.0;
    double energy = 0.0;
    float median;
    float mad;
    float limit;
    float scale;
    if (iq == NULL || workspace == NULL || count == 0U || count > workspace->max_samples) {
        return WRJ_ERR_ARGUMENT;
    }
    for (index = 0U; index < count; ++index) {
        if (!wrj_isfinitef(iq[index].re) || !wrj_isfinitef(iq[index].im)) {
            iq[index].re = 0.0f;
            iq[index].im = 0.0f;
        }
        dc_re += iq[index].re;
        dc_im += iq[index].im;
    }
    dc_re /= (double)count;
    dc_im /= (double)count;
    for (index = 0U; index < count; ++index) {
        iq[index].re -= (float)dc_re;
        iq[index].im -= (float)dc_im;
        workspace->metric[index] = wrj_complex_abs(iq[index].re, iq[index].im);
    }
    median = m3_quantile(workspace->scratch, workspace->metric, count, 0.5f);
    for (index = 0U; index < count; ++index) {
        workspace->metric[index] = fabsf(workspace->metric[index] - median);
    }
    mad = m3_quantile(workspace->scratch, workspace->metric, count, 0.5f);
    limit = median + 8.0f * mad;
    for (index = 0U; index < count; ++index) {
        const float magnitude = wrj_complex_abs(iq[index].re, iq[index].im);
        if (magnitude > limit && magnitude > 1.0e-12f) {
            const float factor = limit / magnitude;
            iq[index].re *= factor;
            iq[index].im *= factor;
        }
        energy += (double)wrj_complex_abs2(iq[index].re, iq[index].im);
    }
    scale = (float)sqrt(energy / (double)count + 1.0e-20);
    if (scale <= 0.0f) {
        return WRJ_ERR_DATA;
    }
    for (index = 0U; index < count; ++index) {
        iq[index].re /= scale;
        iq[index].im /= scale;
    }
    return WRJ_OK;
}

wrj_status_t m3_select_wideband_numerology(const wrj_cf32_t *iq, uint32_t count,
                                           float sample_rate_hz,
                                           m3_workspace_t *workspace,
                                           uint32_t *nfft, uint32_t *cp_samples)
{
    static const uint32_t base[5][2] = {
        {512U, 64U}, {1024U, 96U}, {1024U, 128U}, {2048U, 144U}, {2048U, 160U}
    };
    const uint32_t take = WRJ_MIN(count, 262144U);
    uint32_t start = 0U;
    uint32_t candidate;
    float best_score = -INFINITY;
    if (iq == NULL || workspace == NULL || nfft == NULL || cp_samples == NULL ||
        count < 8192U || sample_rate_hz <= 0.0f) {
        return WRJ_ERR_ARGUMENT;
    }
    if (count > take) {
        const uint32_t windows = WRJ_MIN(7U, WRJ_MAX(1U, count / take));
        uint32_t window;
        double best_energy = -1.0;
        for (window = 0U; window < windows; ++window) {
            const uint32_t candidate_start = windows > 1U ?
                (uint32_t)llround((double)(count - take) * (double)window /
                                  (double)(windows - 1U)) : 0U;
            uint32_t q;
            double energy = 0.0;
            for (q = 0U; q < take; ++q) {
                energy += wrj_complex_abs2(iq[candidate_start + q].re,
                                           iq[candidate_start + q].im);
            }
            if (energy > best_energy) {
                best_energy = energy;
                start = candidate_start;
            }
        }
    }
    for (candidate = 0U; candidate < WRJ_ARRAY_COUNT(base); ++candidate) {
        const uint32_t nf = (uint32_t)lroundf((float)base[candidate][0] * sample_rate_hz / 30720000.0f);
        const uint32_t cp = (uint32_t)lroundf((float)base[candidate][1] * sample_rate_hz / 30720000.0f);
        const uint32_t length = nf + cp;
        uint32_t i;
        uint32_t metric_count;
        float base_level;
        float mad;
        float q995;
        float q985;
        float q950;
        float periodicity = 0.0f;
        float score;
        if (take <= nf + cp) {
            continue;
        }
        metric_count = take - nf - cp + 1U;
        M3_PERF_COUNT(M3_PERF_OP_CORRELATION_CALLS, 1U);
        M3_PERF_COUNT(M3_PERF_OP_CORRELATION_EVALUATIONS, metric_count);
        M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES, metric_count);
        {
            uint32_t j;
            double sr = 0.0;
            double si = 0.0;
            double ea = 0.0;
            double eb = 0.0;
            for (j = 0U; j < cp; ++j) {
                const wrj_cf32_t a = iq[start + j];
                const wrj_cf32_t b = iq[start + j + nf];
                sr += (double)a.re * b.re + (double)a.im * b.im;
                si += (double)a.re * b.im - (double)a.im * b.re;
                ea += wrj_complex_abs2(a.re, a.im);
                eb += wrj_complex_abs2(b.re, b.im);
            }
            for (i = 0U; i < metric_count; ++i) {
                workspace->metric[i] = (float)(sqrt(sr * sr + si * si) /
                    (sqrt(ea * eb) + 1.0e-20));
                if (i + 1U < metric_count) {
                    const wrj_cf32_t old_a = iq[start + i];
                    const wrj_cf32_t old_b = iq[start + i + nf];
                    const wrj_cf32_t new_a = iq[start + i + cp];
                    const wrj_cf32_t new_b = iq[start + i + cp + nf];
                    sr += (double)new_a.re * new_b.re + (double)new_a.im * new_b.im -
                        (double)old_a.re * old_b.re - (double)old_a.im * old_b.im;
                    si += (double)new_a.re * new_b.im - (double)new_a.im * new_b.re -
                        (double)old_a.re * old_b.im + (double)old_a.im * old_b.re;
                    ea += wrj_complex_abs2(new_a.re, new_a.im) - wrj_complex_abs2(old_a.re, old_a.im);
                    eb += wrj_complex_abs2(new_b.re, new_b.im) - wrj_complex_abs2(old_b.re, old_b.im);
                }
            }
        }
        base_level = m3_quantile(workspace->scratch, workspace->metric, metric_count, 0.5f);
        for (i = 0U; i < metric_count; ++i) {
            workspace->scratch[i] = fabsf(workspace->metric[i] - base_level);
        }
        qsort(workspace->scratch, metric_count, sizeof(float), m3_float_compare);
        mad = m3_sorted_quantile(workspace->scratch, metric_count, 0.5f) + 1.0e-6f;
        q995 = m3_quantile(workspace->scratch, workspace->metric, metric_count, 0.995f);
        q985 = m3_quantile(workspace->scratch, workspace->metric, metric_count, 0.985f);
        q950 = m3_quantile(workspace->scratch, workspace->metric, metric_count, 0.950f);
        for (i = 0U; i < 4U; ++i) {
            const uint32_t lag = (i + 1U) * length;
            uint32_t q;
            double ab = 0.0;
            double aa = 0.0;
            double bb = 0.0;
            if (lag >= metric_count) {
                continue;
            }
            for (q = 0U; q + lag < metric_count; ++q) {
                const float a = WRJ_MAX(workspace->metric[q] - base_level, 0.0f);
                const float b = WRJ_MAX(workspace->metric[q + lag] - base_level, 0.0f);
                ab += (double)a * b;
                aa += (double)a * a;
                bb += (double)b * b;
            }
            periodicity += (float)(ab / (sqrt(aa * bb) + 1.0e-20));
        }
        periodicity *= 0.25f;
        score = 5.0f * q995 + 2.0f * q985 + q950 +
            0.15f * (q995 - base_level) / mad + 8.0f * periodicity;
        if (score > best_score) {
            best_score = score;
            *nfft = nf;
            *cp_samples = cp;
        }
    }
    return isfinite(best_score) ? WRJ_OK : WRJ_ERR_DATA;
}
