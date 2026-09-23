#include "m4_module4.h"

void m4_default_config(m4_config_t *config)
{
    if (config != NULL) {
        config->max_bytes = 256U;
        config->minimum_symbol_confidence = 0.35f;
    }
}

wrj_status_t m4_workspace_bind(m4_workspace_t *workspace, wrj_cf32_t *filtered,
                               float *metric, float *scratch,
                               float *fft_re, float *fft_im, uint32_t max_samples,
                               uint32_t fft_capacity)
{
    if (workspace == NULL || filtered == NULL || metric == NULL || scratch == NULL || fft_re == NULL ||
        fft_im == NULL || max_samples == 0U || fft_capacity < 2048U) {
        return WRJ_ERR_ARGUMENT;
    }
    memset(workspace, 0, sizeof(*workspace));
    workspace->filtered = filtered;
    workspace->metric = metric;
    workspace->scratch = scratch;
    workspace->fft_re = fft_re;
    workspace->fft_im = fft_im;
    workspace->max_samples = max_samples;
    workspace->fft_capacity = fft_capacity;
    return WRJ_OK;
}

wrj_status_t m4_run(const wrj_candidate_t *candidate, const m3_result_t *m3_result,
                    const wrj_cf32_t *compensated_iq, uint32_t sample_count,
                    const m4_config_t *config, m4_workspace_t *workspace,
                    m4_result_t *result)
{
    wrj_status_t status = WRJ_ERR_UNSUPPORTED;
    if (candidate == NULL || m3_result == NULL || compensated_iq == NULL ||
        config == NULL || workspace == NULL || result == NULL) {
        return WRJ_ERR_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->bit_error_estimate = 0.5f;
    result->latitude_deg = NAN;
    result->longitude_deg = NAN;
    result->altitude_m = NAN;
    result->speed_mps = NAN;
    result->heading_deg = NAN;
    result->byte_source = M4_BYTE_SOURCE_NONE;
    snprintf(result->protocol_type, sizeof(result->protocol_type), "%s",
             m3_result->profile_name);
    snprintf(result->byte_recovery_status, sizeof(result->byte_recovery_status),
             "byte_recovery_failed");
    snprintf(result->parse_status, sizeof(result->parse_status), "not_parsed");
    if (sample_count > workspace->max_samples || m3_result->num_frames == 0U) {
        result->status = M4_STATUS_BYTE_RECOVERY_FAILED;
        snprintf(result->diagnostics, sizeof(result->diagnostics),
                 "no_synced_frames_or_capacity_exceeded");
        return WRJ_OK;
    }
    switch (m3_result->profile) {
        case WRJ_PROFILE_REMOTEID_BLE:
            status = m4_recover_remoteid(compensated_iq, sample_count,
                                         candidate->sample_rate_hz, m3_result,
                                         workspace, result);
            break;
        case WRJ_PROFILE_DJI_WIDEBAND_CP:
        case WRJ_PROFILE_AUTEL_WIDEBAND_CP:
        case WRJ_PROFILE_AUTEL_CONTROL_CP:
        case WRJ_PROFILE_DRONEID_ZC:
            status = m4_recover_profile_bytes(compensated_iq, sample_count,
                                              candidate->sample_rate_hz, m3_result,
                                              config, workspace, result);
            break;
        default:
            status = m4_recover_blind_bytes(compensated_iq, sample_count,
                                            m3_result, config, workspace, result);
            break;
    }
    if (status != WRJ_OK || result->byte_count == 0U) {
        if (result->status == M4_STATUS_NOT_RUN)
            result->status = M4_STATUS_BYTE_RECOVERY_FAILED;
        if (result->diagnostics[0] == '\0') {
            snprintf(result->diagnostics, sizeof(result->diagnostics),
                     "no_verified_symbol_to_byte_hypothesis");
        }
        return WRJ_OK;
    }
    result->byte_source = M4_BYTE_SOURCE_REAL_IQ;
    snprintf(result->byte_recovery_status, sizeof(result->byte_recovery_status),
             "recovered_from_real_iq");
    if (m3_result->profile != WRJ_PROFILE_REMOTEID_BLE) {
        m4_parse_structural_fields(result);
        result->status = M4_STATUS_PARTIAL;
        snprintf(result->parse_status, sizeof(result->parse_status), "partial_parse");
    }
    return WRJ_OK;
}
