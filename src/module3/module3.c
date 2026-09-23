#include "m3_module3.h"
#include "wrj_io.h"

#include <stdlib.h>

static float m3_local_cp_score(const wrj_cf32_t *iq, uint32_t count, uint32_t start,
                               uint32_t nfft, uint32_t cp_samples)
{
    uint32_t index;
    double sr = 0.0;
    double si = 0.0;
    double ea = 0.0;
    double eb = 0.0;
    if (start + nfft + cp_samples > count) {
        return -1.0f;
    }
    for (index = 0U; index < cp_samples; ++index) {
        const wrj_cf32_t a = iq[start + index];
        const wrj_cf32_t b = iq[start + index + nfft];
        sr += (double)a.re * b.re + (double)a.im * b.im;
        si += (double)a.re * b.im - (double)a.im * b.re;
        ea += wrj_complex_abs2(a.re, a.im);
        eb += wrj_complex_abs2(b.re, b.im);
    }
    return (float)(sqrt(sr * sr + si * si) / (sqrt(ea * eb) + 1.0e-20));
}

static void m3_refine_cp_starts(const wrj_cf32_t *iq, uint32_t count,
                                uint32_t nfft, uint32_t cp_samples,
                                m3_result_t *result)
{
    uint32_t frame;
    for (frame = 0U; frame < result->num_frames; ++frame) {
        const uint32_t original = result->frame_start_samples_0based[frame];
        uint32_t best = original;
        float best_score;
        int32_t delta;
        if (result->frame_confidence[frame] < 0.98f) {
            continue;
        }
        best_score = m3_local_cp_score(iq, count, original, nfft, cp_samples);
        for (delta = -3; delta <= 3; ++delta) {
            const int64_t trial = (int64_t)original + delta;
            float score;
            if (trial < 0 || trial >= (int64_t)count) {
                continue;
            }
            score = m3_local_cp_score(iq, count, (uint32_t)trial, nfft, cp_samples);
            if (score > best_score) {
                best_score = score;
                best = (uint32_t)trial;
            }
        }
        result->frame_start_samples_0based[frame] = best;
    }
}

static void m3_set_status(m3_result_t *result, const char *status)
{
    snprintf(result->status, sizeof(result->status), "%s", status);
}

static float m3_wrap_fractional_cfo(float cfo_hz, float subcarrier_spacing_hz)
{
    float wrapped;
    if (subcarrier_spacing_hz <= 0.0f || !isfinite(cfo_hz)) {
        return cfo_hz;
    }
    wrapped = fmodf(cfo_hz + 0.5f * subcarrier_spacing_hz, subcarrier_spacing_hz);
    if (wrapped < 0.0f) {
        wrapped += subcarrier_spacing_hz;
    }
    return wrapped - 0.5f * subcarrier_spacing_hz;
}

void m3_default_config(m3_config_t *config)
{
    if (config == NULL) {
        return;
    }
    config->max_samples = 917504U;
    config->fft_size = 65536U;
    config->max_frames = 20U;
    config->sync_accept_threshold = 0.80f;
    config->spectrum_search_fraction = 0.48f;
    config->enable_integer_cfo_search = 1U;
}

wrj_status_t m3_workspace_init(m3_workspace_t *workspace, const m3_config_t *config)
{
    const size_t samples = (config == NULL) ? 0U : (size_t)config->max_samples;
    const size_t fft_size = (config == NULL) ? 0U : (size_t)config->fft_size;
    if (workspace == NULL || samples == 0U || fft_size == 0U) {
        return WRJ_ERR_ARGUMENT;
    }
    memset(workspace, 0, sizeof(*workspace));
    workspace->max_samples = config->max_samples;
    workspace->fft_size = config->fft_size;
    workspace->spectrum_length = WRJ_MIN(config->fft_size, 32768U);
    workspace->spectrum_blocks = 20U;
    /* A strict local maximum needs at least one neighbour on each side, so
     * at most roughly N/2 candidates can exist. */
    workspace->peak_capacity = config->max_samples / 2U + 1U;
    workspace->selected_peak_capacity = 8192U;
    workspace->baseband = calloc(samples, sizeof(*workspace->baseband));
    workspace->compensated = calloc(samples, sizeof(*workspace->compensated));
    workspace->fft_re = calloc(fft_size, sizeof(*workspace->fft_re));
    workspace->fft_im = calloc(fft_size, sizeof(*workspace->fft_im));
    workspace->power = calloc(fft_size, sizeof(*workspace->power));
    workspace->metric = calloc(samples, sizeof(*workspace->metric));
    workspace->scratch = calloc(samples, sizeof(*workspace->scratch));
    workspace->peak_candidates = calloc(workspace->peak_capacity, sizeof(*workspace->peak_candidates));
    workspace->peak_selected = calloc(workspace->selected_peak_capacity, sizeof(*workspace->peak_selected));
    workspace->flags = calloc(samples, sizeof(*workspace->flags));
    workspace->spectrum_block_power = calloc((size_t)workspace->spectrum_length * workspace->spectrum_blocks,
                                              sizeof(*workspace->spectrum_block_power));
    workspace->spectrum_block_energy = calloc(workspace->spectrum_blocks,
                                               sizeof(*workspace->spectrum_block_energy));
    workspace->spectrum_smooth = calloc(workspace->spectrum_length, sizeof(*workspace->spectrum_smooth));
    workspace->spectrum_weight = calloc(workspace->spectrum_length, sizeof(*workspace->spectrum_weight));
    workspace->spectrum_aux = calloc(workspace->spectrum_length, sizeof(*workspace->spectrum_aux));
    if (workspace->baseband == NULL || workspace->compensated == NULL ||
        workspace->fft_re == NULL || workspace->fft_im == NULL ||
        workspace->power == NULL || workspace->metric == NULL || workspace->scratch == NULL ||
        workspace->peak_candidates == NULL || workspace->peak_selected == NULL || workspace->flags == NULL ||
        workspace->spectrum_block_power == NULL || workspace->spectrum_block_energy == NULL ||
        workspace->spectrum_smooth == NULL || workspace->spectrum_weight == NULL ||
        workspace->spectrum_aux == NULL) {
        m3_workspace_release(workspace);
        return WRJ_ERR_MEMORY;
    }
    return WRJ_OK;
}

void m3_workspace_release(m3_workspace_t *workspace)
{
    if (workspace == NULL) {
        return;
    }
    free(workspace->baseband);
    free(workspace->compensated);
    free(workspace->fft_re);
    free(workspace->fft_im);
    free(workspace->power);
    free(workspace->metric);
    free(workspace->scratch);
    free(workspace->peak_candidates);
    free(workspace->peak_selected);
    free(workspace->flags);
    free(workspace->spectrum_block_power);
    free(workspace->spectrum_block_energy);
    free(workspace->spectrum_smooth);
    free(workspace->spectrum_weight);
    free(workspace->spectrum_aux);
    memset(workspace, 0, sizeof(*workspace));
}

size_t m3_workspace_bytes(const m3_workspace_t *workspace)
{
    if (workspace == NULL) {
        return 0U;
    }
    return (size_t)workspace->max_samples *
        (sizeof(*workspace->baseband) + sizeof(*workspace->compensated) +
         2U * sizeof(float) + sizeof(uint8_t)) +
        (size_t)workspace->peak_capacity * sizeof(m3_peak_t) +
        (size_t)workspace->selected_peak_capacity * sizeof(m3_peak_t) +
        (size_t)workspace->fft_size * 3U * sizeof(float) +
        (size_t)workspace->spectrum_length * workspace->spectrum_blocks * sizeof(float) +
        (size_t)workspace->spectrum_length * 3U * sizeof(float) +
        (size_t)workspace->spectrum_blocks * sizeof(float);
}

wrj_profile_kind_t m3_select_profile(const wrj_candidate_t *candidate,
                                     uint32_t *nfft, uint32_t *cp_samples)
{
    const int is_wideband = candidate != NULL && strstr(candidate->predicted_link_type, "Wideband") != NULL;
    const int is_autel = candidate != NULL && strstr(candidate->predicted_protocol_family, "Autel") != NULL;
    if (nfft != NULL) {
        *nfft = 0U;
    }
    if (cp_samples != NULL) {
        *cp_samples = 0U;
    }
    if (candidate == NULL) {
        return WRJ_PROFILE_UNKNOWN;
    }
    if (strstr(candidate->predicted_protocol_family, "RemoteID") != NULL) {
        return WRJ_PROFILE_REMOTEID_BLE;
    }
    if (strstr(candidate->predicted_protocol_family, "DroneID") != NULL) {
        if (nfft != NULL) {
            *nfft = (uint32_t)lroundf(1024.0f * candidate->sample_rate_hz / 15360000.0f);
        }
        if (cp_samples != NULL) {
            *cp_samples = (uint32_t)lroundf(72.0f * candidate->sample_rate_hz / 15360000.0f);
        }
        return WRJ_PROFILE_DRONEID_ZC;
    }
    if (is_wideband != 0) {
        if (nfft != NULL) {
            *nfft = is_autel != 0 ? 1024U : 2048U;
        }
        if (cp_samples != NULL) {
            /* The established OcuSync-like CP profile uses 160 samples.
             * Autel-like links retain the 1024/128 numerology. */
            *cp_samples = is_autel != 0 ? 128U : 160U;
        }
        return is_autel != 0 ? WRJ_PROFILE_AUTEL_WIDEBAND_CP : WRJ_PROFILE_DJI_WIDEBAND_CP;
    }
    if (strstr(candidate->predicted_link_type, "Control") != NULL) {
        if (is_autel != 0) {
            if (nfft != NULL) {
                *nfft = 1024U;
            }
            if (cp_samples != NULL) {
                *cp_samples = 128U;
            }
            return WRJ_PROFILE_AUTEL_CONTROL_CP;
        }
        return WRJ_PROFILE_CONTROL_BURST;
    }
    return WRJ_PROFILE_UNKNOWN;
}

static void m3_init_result(m3_result_t *result, wrj_profile_kind_t profile)
{
    memset(result, 0, sizeof(*result));
    result->estimated_cfo_hz = NAN;
    result->spectral_correction_hz = NAN;
    result->fractional_cfo_hz = NAN;
    result->residual_cfo_hz = NAN;
    result->droneid_zc_peak_position = NAN;
    result->original_frame_start = NAN;
    result->corrected_frame_start = NAN;
    result->offset_search_score = NAN;
    result->estimated_sfo_ppm = NAN;
    result->symbol_timing_offset = NAN;
    result->profile = profile;
    m3_set_status(result, "not_run");
}

wrj_status_t m3_run(const wrj_candidate_t *candidate, const wrj_cf32_t *iq,
                    uint32_t count, const m3_config_t *config,
                    m3_workspace_t *workspace, m3_result_t *result)
{
    uint32_t nfft = 0U;
    uint32_t cp_samples = 0U;
    wrj_profile_kind_t profile;
    float spectral = 0.0f;
    float fine_cfo_hz = 0.0f;
    int32_t integer_cfo_index = 0;
    float integer_cfo_offset_hz = 0.0f;
    m3_result_t sync_result;
    wrj_status_t status;

    if (candidate == NULL || iq == NULL || config == NULL || workspace == NULL || result == NULL ||
        count == 0U || count > workspace->max_samples) {
        return WRJ_ERR_ARGUMENT;
    }
    profile = m3_select_profile(candidate, &nfft, &cp_samples);
    m3_init_result(result, profile);
    snprintf(result->profile_name, sizeof(result->profile_name), "%s", wrj_profile_name(profile));
    snprintf(result->recommended_profile, sizeof(result->recommended_profile), "%s",
             wrj_profile_name(profile));
    result->profiles_tried = 1U;

    if (iq != workspace->compensated) {
        memcpy(workspace->compensated, iq, sizeof(*iq) * (size_t)count);
    }
    status = m3_preprocess_iq(workspace->compensated, count, workspace);
    if (status != WRJ_OK) {
        m3_set_status(result, "preprocess_failed");
        return status;
    }

    /* Module1/2 supplies the selected candidate centre as a normal front-end
     * acquisition hint.  It moves the extracted channel close to baseband;
     * the reported Module3 CFO below remains the residual correction only. */
    m3_cfo_compensate(workspace->compensated, workspace->baseband, count, candidate->sample_rate_hz,
                      candidate->candidate_center_offset_hz);
    memcpy(workspace->compensated, workspace->baseband,
           sizeof(*workspace->compensated) * (size_t)count);
    if (profile == WRJ_PROFILE_DJI_WIDEBAND_CP || profile == WRJ_PROFILE_AUTEL_WIDEBAND_CP) {
        status = m3_select_wideband_numerology(workspace->compensated, count,
                                                candidate->sample_rate_hz, workspace,
                                                &nfft, &cp_samples);
        if (status != WRJ_OK) {
            m3_set_status(result, "numerology_selection_failed");
            return status;
        }
    }
    status = m3_estimate_spectral_center(workspace->compensated, count, candidate->sample_rate_hz,
                                         candidate->bandwidth_hz, workspace, &spectral);
    if (status != WRJ_OK) {
        m3_set_status(result, "spectral_estimation_failed");
        return status;
    }
    m3_cfo_compensate(workspace->compensated, workspace->compensated, count,
                      candidate->sample_rate_hz, spectral);
    result->spectral_correction_hz = spectral;
    memcpy(workspace->baseband, workspace->compensated,
           sizeof(*workspace->baseband) * (size_t)count);
    status = m3_bandlimit_fir(workspace->compensated, count, candidate->sample_rate_hz,
                              candidate->bandwidth_hz, workspace);
    if (status != WRJ_OK) {
        m3_set_status(result, "bandlimit_failed");
        return status;
    }

    if (config->enable_integer_cfo_search != 0U && nfft > 0U) {
        (void)m3_integer_cfo_search(workspace->compensated, count, candidate->sample_rate_hz,
                                    candidate->sample_rate_hz / (float)nfft, workspace,
                                    &integer_cfo_index, &integer_cfo_offset_hz);
        if (integer_cfo_index != 0) {
            m3_cfo_compensate(workspace->compensated, workspace->compensated, count,
                              candidate->sample_rate_hz, integer_cfo_offset_hz);
        }
    }
    if (profile != WRJ_PROFILE_DJI_WIDEBAND_CP && profile != WRJ_PROFILE_AUTEL_WIDEBAND_CP &&
        profile != WRJ_PROFILE_AUTEL_CONTROL_CP) {
        m3_result_t primary;
        m3_result_t alternate;
        float primary_score;
        float alternate_score = -1.0f;
        memset(&primary, 0, sizeof(primary));
        memset(&alternate, 0, sizeof(alternate));
        switch (profile) {
            case WRJ_PROFILE_DRONEID_ZC:
                status = m3_droneid_synchronize(workspace->compensated, count,
                                                candidate->sample_rate_hz,
                                                config->max_frames, workspace, &primary);
                break;
            case WRJ_PROFILE_REMOTEID_BLE:
                status = m3_remoteid_ble_synchronize(workspace->compensated, count,
                                                     candidate->sample_rate_hz,
                                                     config->max_frames, workspace, &primary);
                if (status == WRJ_OK && primary.crc_success_count == 0U) {
                    static const float residual_grid_hz[9] = {
                        -250000.0f, -187500.0f, -156250.0f, -125000.0f, -62500.0f,
                          62500.0f,  125000.0f,  187500.0f,  250000.0f};
                    m3_result_t best = primary;
                    float best_offset_hz = 0.0f;
                    float current_offset_hz = 0.0f;
                    uint32_t residual_index;
                    for (residual_index = 0U; residual_index < 9U; ++residual_index) {
                        m3_result_t trial;
                        const float target_offset_hz = residual_grid_hz[residual_index];
                        const float delta_hz = target_offset_hz - current_offset_hz;
                        memset(&trial, 0, sizeof(trial));
                        m3_cfo_compensate(workspace->compensated, workspace->compensated,
                                          count, candidate->sample_rate_hz, delta_hz);
                        current_offset_hz = target_offset_hz;
                        if (m3_remoteid_ble_synchronize(workspace->compensated, count,
                                candidate->sample_rate_hz, config->max_frames,
                                workspace, &trial) == WRJ_OK) {
                            if (trial.crc_success_count > best.crc_success_count ||
                                (trial.crc_success_count == best.crc_success_count &&
                                 trial.sync_confidence > best.sync_confidence)) {
                                best = trial;
                                best_offset_hz = target_offset_hz;
                            }
                        }
                    }
                    m3_cfo_compensate(workspace->compensated, workspace->compensated, count,
                                      candidate->sample_rate_hz,
                                      best_offset_hz - current_offset_hz);
                    primary = best;
                    primary.residual_cfo_hz = best_offset_hz;
                    primary.fractional_cfo_hz = best_offset_hz / candidate->sample_rate_hz;
                    snprintf(primary.sync_method, sizeof(primary.sync_method),
                             "BLE_1M_AA_multiphase_CRC24_residual_CFO");
                }
                break;
            case WRJ_PROFILE_CONTROL_BURST:
                status = m3_burst_synchronize(workspace->compensated, count,
                                              candidate->sample_rate_hz,
                                              config->max_frames, workspace, &primary);
                break;
            default:
                status = m3_blind_synchronize(workspace->compensated, count,
                                              candidate->sample_rate_hz,
                                              config->max_frames, workspace, &primary);
                break;
        }
        if (status != WRJ_OK) {
            primary.sync_confidence = 0.0f;
        }
        primary_score = 0.72f * primary.sync_confidence +
            0.28f * wrj_clip01(primary.peak_metric);
        if (primary.sync_confidence < 0.88f && profile != WRJ_PROFILE_UNKNOWN) {
            result->profiles_tried = 2U;
            if (m3_blind_synchronize(workspace->compensated, count,
                                     candidate->sample_rate_hz, config->max_frames,
                                     workspace, &alternate) == WRJ_OK) {
                alternate_score = 0.72f * alternate.sync_confidence +
                    0.28f * wrj_clip01(alternate.peak_metric);
            }
        }
        if (alternate_score >= primary_score + 0.025f) {
            primary = alternate;
            profile = profile == WRJ_PROFILE_CONTROL_BURST ?
                WRJ_PROFILE_DJI_CONTROL_BLIND : WRJ_PROFILE_UNKNOWN;
            result->profile_changed_by_evidence = 1U;
        }
        *result = primary;
        result->profile = profile;
        snprintf(result->profile_name, sizeof(result->profile_name), "%s", wrj_profile_name(profile));
        snprintf(result->recommended_profile, sizeof(result->recommended_profile), "%s",
                 wrj_profile_name(m3_select_profile(candidate, NULL, NULL)));
        result->profiles_tried = primary.sync_confidence < 0.88f ? 2U : 1U;
        result->profile_changed_by_evidence = (uint8_t)(profile != m3_select_profile(candidate, NULL, NULL));
        if (result->profiles_tried > 1U) {
            snprintf(result->attempted_profiles, sizeof(result->attempted_profiles), "%s|Unknown_Blind_Repetition",
                     result->recommended_profile);
        } else {
            snprintf(result->attempted_profiles, sizeof(result->attempted_profiles), "%s",
                     result->recommended_profile);
        }
        result->spectral_correction_hz = spectral;
        result->fractional_cfo_hz *= candidate->sample_rate_hz;
        result->integer_cfo_index = integer_cfo_index;
        result->integer_cfo_offset_hz = integer_cfo_offset_hz;
        result->cfo_candidate_count = nfft > 0U ? 7U : 1U;
        result->selected_cfo_score = 0.72f * primary.sync_confidence +
            0.28f * wrj_clip01(primary.peak_metric);
        result->estimated_cfo_hz = spectral + integer_cfo_offset_hz + result->fractional_cfo_hz;
        m3_estimate_sfo(workspace->compensated, count, nfft, cp_samples, result);
        if (result->sync_confidence >= config->sync_accept_threshold) {
            m3_set_status(result, "ok");
            return WRJ_OK;
        }
        m3_set_status(result, "sync_below_threshold");
        return status == WRJ_OK ? WRJ_OK : status;
    }

    memset(&sync_result, 0, sizeof(sync_result));
    status = m3_cp_synchronize(workspace->compensated, count, nfft, cp_samples,
                               config->max_frames, workspace, &sync_result);
    if (status != WRJ_OK) {
        *result = sync_result;
        result->profile = profile;
        snprintf(result->profile_name, sizeof(result->profile_name), "%s", wrj_profile_name(profile));
        return status;
    }
    /* CP phase identifies fractional CFO modulo subcarrier spacing.  The
     * bounded integer-CFO stage above has already selected its hypothesis, so
     * this refinement remains in the principal subcarrier interval. */
    fine_cfo_hz = m3_wrap_fractional_cfo(sync_result.fractional_cfo_hz * candidate->sample_rate_hz,
                                         candidate->sample_rate_hz / (float)nfft);
    m3_cfo_compensate(workspace->compensated, workspace->compensated, count,
                      candidate->sample_rate_hz, fine_cfo_hz);
    memset(&sync_result, 0, sizeof(sync_result));
    status = m3_cp_synchronize(workspace->compensated, count, nfft, cp_samples,
                               config->max_frames, workspace, &sync_result);
    if (status != WRJ_OK) {
        *result = sync_result;
        result->profile = profile;
        snprintf(result->profile_name, sizeof(result->profile_name), "%s", wrj_profile_name(profile));
        return status;
    }
    *result = sync_result;
    result->profile = profile;
    snprintf(result->profile_name, sizeof(result->profile_name), "%s", wrj_profile_name(profile));
    result->spectral_correction_hz = spectral;
    result->fractional_cfo_hz = fine_cfo_hz +
        m3_wrap_fractional_cfo(sync_result.fractional_cfo_hz * candidate->sample_rate_hz,
                               candidate->sample_rate_hz / (float)nfft);
    result->estimated_cfo_hz = spectral + integer_cfo_offset_hz + result->fractional_cfo_hz;
    result->integer_cfo_index = integer_cfo_index;
    result->integer_cfo_offset_hz = integer_cfo_offset_hz;
    result->cfo_candidate_count = 7U;
    result->selected_cfo_score = result->sync_confidence;
    result->profiles_tried = 1U;
    snprintf(result->recommended_profile, sizeof(result->recommended_profile), "%s",
             wrj_profile_name(profile));
    snprintf(result->attempted_profiles, sizeof(result->attempted_profiles), "%s",
             wrj_profile_name(profile));
    m3_refine_cp_starts(workspace->baseband, count, nfft, cp_samples, result);
    m3_estimate_sfo(workspace->compensated, count, nfft, cp_samples, result);
    if (result->sync_confidence >= config->sync_accept_threshold) {
        m3_set_status(result, "ok");
    } else {
        m3_set_status(result, "sync_below_threshold");
    }
    return WRJ_OK;
}
