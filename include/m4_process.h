#ifndef M4_PROCESS_H
#define M4_PROCESS_H

#include "m4_module4.h"
#include "m4_matlab_statistics.h"

typedef struct {
    m4_matlab_rng_t rng;
    m4_matlab_record_store_t observations;
} module4_process_t;

#define MODULE4_CATEGORY_COUNT 7U
typedef struct {
    uint8_t present[MODULE4_CATEGORY_COUNT];
    m4_matlab_category_analysis_t categories[MODULE4_CATEGORY_COUNT];
} module4_process_analysis_t;

wrj_status_t module4_process_init(module4_process_t *process,
    uint32_t max_candidates);
void module4_process_release(module4_process_t *process);
wrj_status_t module4_process_candidate(module4_process_t *process,
    const wrj_candidate_t *candidate, const m3_result_t *m3,
    const wrj_cf32_t *compensated_iq, uint32_t sample_count,
    const m4_config_t *config, m4_workspace_t *workspace, m4_result_t *result);
wrj_status_t module4_process_analyze(const module4_process_t *process,
    const char *category, m4_matlab_category_analysis_t *out);
wrj_status_t module4_process_finalize(const module4_process_t *process,
    module4_process_analysis_t *out);

#endif
