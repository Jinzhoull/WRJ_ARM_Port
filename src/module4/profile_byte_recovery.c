#include "m4_module4.h"
#include "m3_module3.h"

#include <stdlib.h>

#define M4_FRAME_BYTES 96U
#define M4_FRAME_BITS (M4_FRAME_BYTES * 8U)

typedef struct {
    uint8_t bytes[M4_FRAME_BYTES];
    float byte_confidence[M4_FRAME_BYTES];
    uint16_t count;
    float confidence;
    float timing;
    float score;
    uint8_t crc_pass;
    uint32_t symbol_step;
    uint32_t symbol_offset;
    float phase_rad;
    uint32_t active_count;
    uint32_t rotation;
    char method[64];
} m4_hypothesis_t;

static int m4_float_ascending(const void *left, const void *right)
{
    const float a = *(const float *)left, b = *(const float *)right;
    return (a > b) - (a < b);
}

static float m4_byte_balance(const uint8_t *bytes, uint32_t count)
{
    uint32_t i, ones = 0U;
    for (i = 0U; i < count; ++i) {
        uint8_t value = bytes[i];
        while (value != 0U) { ones += value & 1U; value >>= 1U; }
    }
    if (count == 0U) return 0.0f;
    return wrj_clip01(1.0f - 2.0f * fabsf((float)ones / (8.0f * (float)count) - .5f));
}

static uint8_t m4_crc16_candidate(const uint8_t *bytes, uint32_t count)
{
    uint32_t stops[2], n = 1U, i;
    if (count < 3U) return 0U;
    stops[0] = count;
    if (count >= 13U) {
        const uint32_t end = 11U + (uint32_t)bytes[4] * 256U + bytes[5] + 2U;
        if (end >= 3U && end < count) stops[n++] = end;
    }
    for (i = 0U; i < n; ++i) {
        const uint32_t end = stops[i];
        const uint16_t received = (uint16_t)(((uint16_t)bytes[end - 2U] << 8U) | bytes[end - 1U]);
        if (m4_crc16_ccitt(bytes, end - 2U) == received) return 1U;
    }
    return 0U;
}

static uint32_t m4_active_carriers(m4_workspace_t *ws, uint32_t nfft,
                                    int droneid, uint16_t *indices)
{
    uint32_t k, count = 0U;
    if (droneid != 0) {
        /* MATLAB m12_centered_bins(2048,601) minus its centre bin. */
        const uint32_t first = nfft / 2U - 300U;
        for (k = first; k <= first + 600U; ++k)
            if (k != nfft / 2U) indices[count++] = (uint16_t)k;
        return count;
    }
    {
        const uint32_t edge = (uint32_t)lroundf(.03f * (float)nfft);
        float peak = 0.0f, noise, threshold;
        uint32_t positive = 0U;
        float span = 0.0f;
        const float center = ((float)nfft - 1.0f) / 2.0f;
        for (k = 0U; k < nfft; ++k) {
            const uint32_t bin = (k + nfft / 2U) % nfft;
            float p = ws->fft_re[bin] * ws->fft_re[bin] +
                      ws->fft_im[bin] * ws->fft_im[bin];
            if (k < edge || k >= nfft - edge) p = 0.0f;
            ws->metric[k] = p;
            if (p > 0.0f) ws->scratch[positive++] = p;
            if (p > peak) peak = p;
        }
        if (positive < 16U || peak <= 0.0f) return 0U;
        qsort(ws->scratch, positive, sizeof(float), m4_float_ascending);
        noise = positive & 1U ? ws->scratch[positive / 2U] :
            .5f * (ws->scratch[positive / 2U - 1U] + ws->scratch[positive / 2U]);
        threshold = WRJ_MAX(3.0f * noise, .025f * peak);
        for (k = 0U; k < nfft; ++k) {
            if (ws->metric[k] >= threshold)
                span = WRJ_MAX(span, fabsf((float)k - center));
        }
        if (span < 1.0f) return 0U;
        for (k = 0U; k < nfft; ++k) {
            if (fabsf((float)k - center) <= span && ws->metric[k] >= .35f * threshold)
                indices[count++] = (uint16_t)k;
        }
    }
    return count;
}

static void m4_ofdm_frame(const wrj_cf32_t *frame, uint32_t length,
                          uint32_t nfft, uint32_t cp, int droneid,
                          m4_workspace_t *ws, m4_hypothesis_t *hypothesis)
{
    uint16_t active[2048];
    uint8_t bits[M4_FRAME_BITS], candidate[M4_FRAME_BYTES], selected[M4_FRAME_BYTES];
    float bit_quality[M4_FRAME_BITS];
    uint32_t cursor = 0U, symbol, bit_count = 0U;
    uint32_t all_bit_count = 0U, ones[4] = {0U, 0U, 0U, 0U};
    double quality_sum = 0.0, timing_sum = 0.0;
    uint32_t timing_count = 0U;
    memset(hypothesis, 0, sizeof(*hypothesis));
    if (nfft > ws->fft_capacity || nfft < 32U || cp == 0U) return;
    for (symbol = 0U; symbol < (droneid != 0 ? 9U : 16U); ++symbol) {
        const uint32_t cpk = droneid != 0 && (symbol == 0U || symbol == 8U) ?
            WRJ_MAX(cp, (uint32_t)lroundf((float)cp * 80.0f / 72.0f)) : cp;
        uint32_t k, active_count;
        double cp_re = 0.0, cp_im = 0.0, cp_a = 0.0, cp_b = 0.0;
        double moment_re = 0.0, moment_im = 0.0;
        if (cursor + cpk + nfft > length) break;
        for (k = 0U; k < cpk; ++k) {
            const wrj_cf32_t a = frame[cursor + k], b = frame[cursor + nfft + k];
            cp_re += (double)a.re * b.re + (double)a.im * b.im;
            cp_im += (double)a.re * b.im - (double)a.im * b.re;
            cp_a += (double)a.re * a.re + (double)a.im * a.im;
            cp_b += (double)b.re * b.re + (double)b.im * b.im;
        }
        timing_sum += hypot(cp_re, cp_im) / sqrt(cp_a * cp_b + 1.0e-12);
        ++timing_count;
        for (k = 0U; k < nfft; ++k) {
            ws->fft_re[k] = frame[cursor + cpk + k].re;
            ws->fft_im[k] = frame[cursor + cpk + k].im;
        }
        if (m3_fft_forward_radix2(ws->fft_re, ws->fft_im, nfft) != WRJ_OK) break;
        active_count = m4_active_carriers(ws, nfft, droneid, active);
        hypothesis->active_count = active_count;
        if (active_count < 16U) { cursor += cpk + nfft; continue; }
        for (k = 0U; k < active_count; ++k) {
            const uint32_t bin = ((uint32_t)active[k] + nfft / 2U) % nfft;
            const double re = ws->fft_re[bin], im = ws->fft_im[bin];
            const double p = re * re + im * im;
            double r2, i2;
            if (p < 1.0e-18) continue;
            r2 = (re * re - im * im) / p;
            i2 = 2.0 * re * im / p;
            moment_re += r2 * r2 - i2 * i2;
            moment_im += 2.0 * r2 * i2;
        }
        if (hypot(moment_re, moment_im) / (double)active_count >= .18) {
            const float phase = .25f * (float)atan2(-moment_im, -moment_re);
            hypothesis->phase_rad = phase;
            const float c = cosf(phase), s = sinf(phase);
            for (k = 0U; k < active_count; ++k) {
                const uint32_t bin = ((uint32_t)active[k] + nfft / 2U) % nfft;
                const float re = ws->fft_re[bin], im = ws->fft_im[bin];
                uint8_t bi, bq;
                float ci, cq;
                const float rotated_re = re * c + im * s;
                const float rotated_im = im * c - re * s;
                const float magnitude = hypotf(rotated_re, rotated_im) + 1.0e-9f;
                m4_qpsk_decide(rotated_re, rotated_im, &bi, &bq, &ci, &cq);
                ci = wrj_clip01(fabsf(rotated_re) / magnitude * 1.41421356f);
                cq = wrj_clip01(fabsf(rotated_im) / magnitude * 1.41421356f);
                quality_sum += (double)ci + cq;
                all_bit_count += 2U;
                ones[0] += bi + bq;
                ones[1] += bq + (1U - bi);
                ones[2] += (1U - bi) + (1U - bq);
                ones[3] += (1U - bq) + bi;
                if (bit_count + 2U <= M4_FRAME_BITS) {
                    bits[bit_count] = bi;
                    bit_quality[bit_count++] = ci;
                    bits[bit_count] = bq;
                    bit_quality[bit_count++] = cq;
                }
            }
        }
        cursor += cpk + nfft;
    }
    if (bit_count < 128U || all_bit_count == 0U) return;
    {
        float best_rotation_score = -1.0f;
        uint32_t rotation;
        for (rotation = 0U; rotation < 4U; ++rotation) {
            uint32_t pair;
            uint8_t trial_bits[M4_FRAME_BITS];
            uint16_t trial_count;
            float balance, rotation_score;
            uint8_t crc;
            for (pair = 0U; pair + 1U < bit_count; pair += 2U) {
                const uint8_t bi = bits[pair], bq = bits[pair + 1U];
                trial_bits[pair] = rotation == 0U ? bi : rotation == 1U ? bq :
                                   rotation == 2U ? (uint8_t)(1U - bi) : (uint8_t)(1U - bq);
                trial_bits[pair + 1U] = rotation == 0U ? bq : rotation == 1U ?
                         (uint8_t)(1U - bi) : rotation == 2U ?
                         (uint8_t)(1U - bq) : bi;
            }
            trial_count = m4_pack_bits_msb(trial_bits, bit_count, candidate, M4_FRAME_BYTES);
            crc = m4_crc16_candidate(candidate, trial_count);
            balance = wrj_clip01(1.0f - 2.0f * fabsf((float)ones[rotation] /
                                            (float)all_bit_count - .5f));
            rotation_score = .70f * (float)(quality_sum / all_bit_count) +
                             .18f * balance + (crc != 0U ? .12f : 0.0f);
            /* Constellation rotations 180 degrees apart have identical
             * balance/soft quality. Keep the first in numerical ties. */
            if (rotation_score > best_rotation_score + 1.0e-6f) {
                best_rotation_score = rotation_score;
                hypothesis->rotation = rotation;
                hypothesis->count = trial_count;
                hypothesis->crc_pass = crc;
                memcpy(selected, candidate, trial_count);
            }
        }
    }
    hypothesis->timing = timing_count != 0U ? (float)(timing_sum / timing_count) : 0.0f;
    hypothesis->confidence = wrj_clip01(.58f * (float)(quality_sum / all_bit_count) +
                             .24f * hypothesis->timing +
                             .18f * m4_byte_balance(selected, hypothesis->count));
    hypothesis->score = .78f * hypothesis->confidence + .12f * hypothesis->timing;
    if (hypothesis->crc_pass != 0U) hypothesis->score += .10f;
    memcpy(hypothesis->bytes, selected, hypothesis->count);
    for (symbol = 0U; symbol < hypothesis->count; ++symbol) {
        uint32_t b;
        float q = 0.0f;
        for (b = 0U; b < 8U; ++b) q += bit_quality[symbol * 8U + b];
        hypothesis->byte_confidence[symbol] = q / 8.0f;
    }
    snprintf(hypothesis->method, sizeof(hypothesis->method), "%s",
             droneid != 0 ? "DRONEID_OFDM_ZC_CP_QPSK" : "OFDM_CP_QPSK_SOFT");
}

static void m4_fourth_unit(float re, float im, double *out_re, double *out_im)
{
    const double p = (double)re * re + (double)im * im + 1.0e-20;
    const double a = ((double)re * re - (double)im * im) / p;
    const double b = 2.0 * (double)re * im / p;
    *out_re = a * a - b * b;
    *out_im = 2.0 * a * b;
}

static void m4_generic_qpsk_frame(const wrj_cf32_t *frame, uint32_t length,
                                  float sample_rate_hz, m4_workspace_t *ws,
                                  m4_hypothesis_t *hypothesis)
{
    static const float rates[] = {0.8e6f, 1.0e6f, 1.2e6f, 1.6e6f, 2.0e6f, 2.8e6f};
    uint32_t sps_candidates[16] = {1U, 2U, 4U, 8U, 16U, 24U, 32U};
    uint32_t sps_count = 7U, k, count = WRJ_MIN(length, 32768U);
    double slope_re = 0.0, slope_im = 0.0, previous_re = 0.0, previous_im = 0.0;
    float best_score = -1.0f, best_timing = 0.0f, best_phase = 0.0f;
    uint32_t best_sps = 0U, best_offset = 0U;
    uint8_t bits[M4_FRAME_BITS];
    float bit_quality[M4_FRAME_BITS];
    uint32_t recovered_bits = 0U;
    double total_confidence = 0.0;
    memset(hypothesis, 0, sizeof(*hypothesis));
    if (count < 16U) return;
    for (k = 0U; k < count; ++k) {
        double now_re, now_im;
        m4_fourth_unit(frame[k].re, frame[k].im, &now_re, &now_im);
        if (k != 0U) {
            slope_re += previous_re * now_re + previous_im * now_im;
            slope_im += previous_re * now_im - previous_im * now_re;
        }
        previous_re = now_re; previous_im = now_im;
    }
    {
        const float slope = .25f * (float)atan2(slope_im, slope_re);
        for (k = 0U; k < count; ++k) {
            const float angle = slope * (float)k;
            const float c = cosf(angle), s = sinf(angle);
            const float re = frame[k].re, im = frame[k].im;
            ws->filtered[k].re = re * c + im * s;
            ws->filtered[k].im = im * c - re * s;
        }
    }
    for (k = 0U; k < WRJ_ARRAY_COUNT(rates); ++k) {
        const uint32_t candidate = (uint32_t)lroundf(sample_rate_hz / rates[k]);
        uint32_t j;
        if (candidate < 1U || candidate > 64U) continue;
        for (j = 0U; j < sps_count; ++j) if (sps_candidates[j] == candidate) break;
        if (j == sps_count) sps_candidates[sps_count++] = candidate;
    }
    for (k = 0U; k < sps_count; ++k) {
        const uint32_t sps = sps_candidates[k];
        const uint32_t offset_count = WRJ_MIN(sps, 8U);
        uint32_t offset_index;
        for (offset_index = 0U; offset_index < offset_count; ++offset_index) {
            const uint32_t offset = offset_count == 1U ? 0U :
                (uint32_t)lroundf((float)offset_index * (float)(sps - 1U) /
                                  (float)(offset_count - 1U));
            const uint32_t symbols = (count - offset) / sps;
            double moment_re = 0.0, moment_im = 0.0, coherence = 0.0, margin = 0.0;
            uint32_t j;
            float phase, score;
            if (symbols < 8U) continue;
            for (j = 0U; j < symbols; ++j) {
                uint32_t q;
                double re = 0.0, im = 0.0, energy = 0.0, u_re, u_im;
                for (q = 0U; q < sps; ++q) {
                    const wrj_cf32_t sample = ws->filtered[offset + j * sps + q];
                    re += sample.re; im += sample.im;
                    energy += (double)sample.re * sample.re + (double)sample.im * sample.im;
                }
                re /= sps; im /= sps; energy /= sps;
                ws->metric[j] = (float)re;
                ws->scratch[j] = (float)im;
                coherence += (re * re + im * im) / (energy + 1.0e-12);
                m4_fourth_unit((float)re, (float)im, &u_re, &u_im);
                moment_re += u_re; moment_im += u_im;
            }
            phase = .25f * (float)atan2(-moment_im, -moment_re);
            for (j = 0U; j < symbols; ++j) {
                const float re = ws->metric[j], im = ws->scratch[j];
                const float rotated_re = re * cosf(phase) + im * sinf(phase);
                const float rotated_im = im * cosf(phase) - re * sinf(phase);
                margin += wrj_clip01(1.41421356f * WRJ_MIN(fabsf(rotated_re),
                          fabsf(rotated_im)) / (hypotf(re, im) + 1.0e-9f));
            }
            score = .62f * (float)(margin / symbols) +
                    .38f * (float)(coherence / symbols);
            if (score > best_score) {
                best_score = score;
                best_sps = sps;
                best_offset = offset;
                best_phase = phase;
                best_timing = (float)(coherence / symbols);
            }
        }
    }
    if (best_sps == 0U) return;
    for (k = 0U; best_offset + (k + 1U) * best_sps <= count &&
                 recovered_bits + 2U <= M4_FRAME_BITS; ++k) {
        uint32_t q;
        double re = 0.0, im = 0.0;
        const float c = cosf(best_phase), s = sinf(best_phase);
        float rotated_re, rotated_im, magnitude;
        for (q = 0U; q < best_sps; ++q) {
            re += ws->filtered[best_offset + k * best_sps + q].re;
            im += ws->filtered[best_offset + k * best_sps + q].im;
        }
        re /= best_sps; im /= best_sps;
        rotated_re = (float)re * c + (float)im * s;
        rotated_im = (float)im * c - (float)re * s;
        magnitude = hypotf(rotated_re, rotated_im) + 1.0e-9f;
        bits[recovered_bits] = (uint8_t)(rotated_re < 0.0f);
        bit_quality[recovered_bits++] = wrj_clip01(1.41421356f * fabsf(rotated_re) / magnitude);
        bits[recovered_bits] = (uint8_t)(rotated_im < 0.0f);
        bit_quality[recovered_bits++] = wrj_clip01(1.41421356f * fabsf(rotated_im) / magnitude);
    }
    hypothesis->count = m4_pack_bits_msb(bits, recovered_bits, hypothesis->bytes,
                                          M4_FRAME_BYTES);
    if (hypothesis->count == 0U) return;
    for (k = 0U; k < recovered_bits; ++k) total_confidence += bit_quality[k];
    hypothesis->timing = best_timing;
    hypothesis->symbol_step = best_sps;
    hypothesis->symbol_offset = best_offset;
    hypothesis->phase_rad = best_phase;
    hypothesis->confidence = wrj_clip01(.62f * (float)(total_confidence / recovered_bits) +
                             .23f * best_timing +
                             .15f * m4_byte_balance(hypothesis->bytes, hypothesis->count));
    hypothesis->crc_pass = m4_crc16_candidate(hypothesis->bytes, hypothesis->count);
    hypothesis->score = .82f * hypothesis->confidence +
                        .08f * m4_byte_balance(hypothesis->bytes, hypothesis->count) +
                        (hypothesis->crc_pass != 0U ? .10f : 0.0f);
    for (k = 0U; k < hypothesis->count; ++k) {
        uint32_t b;
        float confidence = 0.0f;
        for (b = 0U; b < 8U; ++b) confidence += bit_quality[k * 8U + b];
        hypothesis->byte_confidence[k] = confidence / 8.0f;
    }
    snprintf(hypothesis->method, sizeof(hypothesis->method), "TIMING_PHASE_QPSK_SOFT");
}

wrj_status_t m4_recover_profile_bytes(const wrj_cf32_t *iq, uint32_t count,
                                      float sample_rate_hz, const m3_result_t *m3,
                                      const m4_config_t *config,
                                      m4_workspace_t *workspace, m4_result_t *result)
{
    m4_hypothesis_t ofdm, generic;
    uint32_t nfft = 256U, cp, frame, ofdm_selected = 0U, generic_selected = 0U;
    double confidence_sum = 0.0;
    const int droneid = m3->profile == WRJ_PROFILE_DRONEID_ZC;
    (void)config;
    if (droneid != 0) nfft = 2048U;
    else while (nfft < 2048U && (nfft << 1U) < m3->frame_length_samples) nfft <<= 1U;
    cp = droneid != 0 ? 144U :
         (m3->frame_length_samples > nfft ? m3->frame_length_samples - nfft : nfft / 16U);
    for (frame = 0U; frame < m3->num_frames && result->packet_count < WRJ_MAX_PACKETS; ++frame) {
        const uint32_t start = m3->frame_start_samples_0based[frame];
        const uint32_t length = WRJ_MIN(m3->frame_length_samples, count > start ? count - start : 0U);
        const m4_hypothesis_t *chosen;
        uint16_t p;
        uint32_t sample;
        double mean_re = 0.0, mean_im = 0.0, energy = 0.0;
        float scale;
        if (length < 64U || start >= count) continue;
        for (sample = 0U; sample < length; ++sample) {
            mean_re += iq[start + sample].re;
            mean_im += iq[start + sample].im;
        }
        mean_re /= length; mean_im /= length;
        for (sample = 0U; sample < length; ++sample) {
            const double re = (double)iq[start + sample].re - mean_re;
            const double im = (double)iq[start + sample].im - mean_im;
            energy += re * re + im * im;
        }
        scale = (float)sqrt(energy / length) + 1.0e-12f;
        for (sample = 0U; sample < length; ++sample) {
            workspace->filtered[sample].re = (float)(((double)iq[start + sample].re - mean_re) / scale);
            workspace->filtered[sample].im = (float)(((double)iq[start + sample].im - mean_im) / scale);
        }
        m4_ofdm_frame(workspace->filtered, length, nfft, cp, droneid, workspace, &ofdm);
        m4_generic_qpsk_frame(workspace->filtered, length, sample_rate_hz, workspace, &generic);
        chosen = ofdm.score >= generic.score ? &ofdm : &generic;
        if (chosen->count == 0U) continue;
        if (chosen == &ofdm) ++ofdm_selected; else ++generic_selected;
        p = result->packet_count++;
        result->packet_lengths[p] = chosen->count;
        memcpy(result->packet_bytes[p], chosen->bytes, chosen->count);
        confidence_sum += chosen->confidence;
        if (p == 0U) {
            result->byte_count = chosen->count;
            memcpy(result->bytes, chosen->bytes, chosen->count);
            memcpy(result->byte_confidence, chosen->byte_confidence,
                   (size_t)chosen->count * sizeof(float));
            snprintf(result->decoder_method, sizeof(result->decoder_method), "%s", chosen->method);
            result->crc_checked = chosen->count >= 3U;
            result->crc_passed = chosen->crc_pass;
            snprintf(result->status_text, sizeof(result->status_text),
                     "sps=%u;offset=%u;phase=%.7f;score=%.7f;ofdmScore=%.7f;genericScore=%.7f;active=%u;ofdmPhase=%.7f",
                     chosen->symbol_step, chosen->symbol_offset,
                     chosen->phase_rad, chosen->score, ofdm.score, generic.score,
                     ofdm.active_count, ofdm.phase_rad);
        }
    }
    if (result->packet_count == 0U) return WRJ_ERR_DATA;
    result->byte_recovery_confidence = (float)(confidence_sum / result->packet_count);
    result->bit_error_estimate = .5f * (1.0f - result->byte_recovery_confidence);
    snprintf(result->crc_candidate_status, sizeof(result->crc_candidate_status), "%s",
             result->crc_passed != 0U ? "crc16_candidate_pass" : "crc16_candidate_not_verified");
    snprintf(result->diagnostics, sizeof(result->diagnostics),
             "real_iq_frames=%u;ofdm_selected=%u;generic_selected=%u;qpsk_180_ambiguity=unresolved",
             result->packet_count, ofdm_selected, generic_selected);
    return WRJ_OK;
}
