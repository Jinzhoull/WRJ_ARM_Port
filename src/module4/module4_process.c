#include "m4_process.h"

wrj_status_t module4_process_init(module4_process_t *process,
    uint32_t max_candidates)
{
    if (process == NULL || max_candidates == 0U ||
        max_candidates > UINT32_MAX / WRJ_MATLAB_MAX_OBSERVATIONS)
        return WRJ_ERR_ARGUMENT;
    memset(process, 0, sizeof(*process));
    m4_matlab_rng_seed(&process->rng, 20260724U);
    return m4_matlab_record_store_init(&process->observations,
        max_candidates * WRJ_MATLAB_MAX_OBSERVATIONS);
}

void module4_process_release(module4_process_t *process)
{
    if (process == NULL) return;
    m4_matlab_record_store_release(&process->observations);
    memset(process, 0, sizeof(*process));
}

wrj_status_t module4_process_candidate(module4_process_t *process,
    const wrj_candidate_t *candidate, const m3_result_t *m3,
    const wrj_cf32_t *compensated_iq, uint32_t sample_count,
    const m4_config_t *config, m4_workspace_t *workspace, m4_result_t *result)
{
    wrj_status_t status;
    if (process == NULL || process->observations.records == NULL)
        return WRJ_ERR_ARGUMENT;
    status = m4_run(candidate, m3, compensated_iq, sample_count,
                    config, workspace, result);
    if (status != WRJ_OK) return status;
    status = m4_matlab_attach_observations(candidate, m3, result, &process->rng);
    if (status != WRJ_OK) return status;
    return m4_matlab_record_store_append(&process->observations,
                                         candidate, m3, result);
}

wrj_status_t module4_process_analyze(const module4_process_t *process,
    const char *category, m4_matlab_category_analysis_t *out)
{
    if (process == NULL) return WRJ_ERR_ARGUMENT;
    return m4_matlab_record_store_analyze(&process->observations,
                                          category, .55, out);
}

wrj_status_t module4_process_finalize(const module4_process_t *process,
    module4_process_analysis_t *out)
{
    static const char *const categories[MODULE4_CATEGORY_COUNT] = {
        "DJI_Control_Link", "DJI_Wideband_Link", "DJI_DroneID",
        "Autel_Control_Link", "Autel_Wideband_Link", "RemoteID_BLE",
        "Unknown_UAV_Link"
    };
    uint32_t i;
    if (process == NULL || out == NULL) return WRJ_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    for (i = 0U; i < MODULE4_CATEGORY_COUNT; ++i) {
        wrj_status_t status = module4_process_analyze(process, categories[i],
                                                       &out->categories[i]);
        if (status == WRJ_OK) out->present[i] = 1U;
        else if (status != WRJ_ERR_DATA) return status;
    }
    return WRJ_OK;
}
