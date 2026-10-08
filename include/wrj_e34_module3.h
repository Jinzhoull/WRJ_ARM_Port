#ifndef WRJ_E34_MODULE3_H
#define WRJ_E34_MODULE3_H
#include "m3_module3.h"

/* Transitional receiver contract. No Golden/expected fields are accepted. */
typedef struct {
    m3_result_t sync;
    uint8_t sync_accepted;
    double fs;
    double fc;
    uint32_t sample_count;
    wrj_cf32_t *input_baseband;       /* Caller-owned, distinct from IQ/workspace. */
    wrj_cf32_t *preprocessed_baseband;
    const wrj_cf32_t *compensated_iq; /* Workspace-owned until release/next call. */
    char receiver_family[16]; /* Actual M4 structure used by a bounded DEEP retry. */
} wrj_sync_result_t;

void wrj_module3_fast_config(m3_config_t *config, uint32_t samples);
wrj_status_t wrj_module3_fast(const wrj_candidate_t *candidate,
    const wrj_cf32_t *iq, uint32_t count, const m3_config_t *config,
    m3_workspace_t *workspace, wrj_sync_result_t *result);
/* Bounded actual-IQ alternate sync and BLE CFO/filter contexts. */
wrj_status_t wrj_module3_deep(const wrj_candidate_t *candidate,
    const wrj_cf32_t *iq, uint32_t count, uint32_t attempt,
    m3_workspace_t *workspace, wrj_sync_result_t *result);
#endif
