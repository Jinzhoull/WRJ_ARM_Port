#ifndef WRJ_COMMON_PERF_TIMER_H
#define WRJ_COMMON_PERF_TIMER_H

#include "wrj_common.h"

typedef enum {
    M3_PERF_M3_TOTAL = 0,
    M3_PERF_IQ_PREPROCESS,
    M3_PERF_CENTER_SHIFT,
    M3_PERF_SPECTRUM_ANALYSIS,
    M3_PERF_PROFILE_SELECTION,
    M3_PERF_COARSE_CFO,
    M3_PERF_INTEGER_CFO,
    M3_PERF_FRACTIONAL_CFO,
    M3_PERF_CFO_COMPENSATION,
    M3_PERF_BANDLIMIT_FIR,
    M3_PERF_WIDEBAND_SYNC,
    M3_PERF_DRONEID_SYNC,
    M3_PERF_DRONEID_ZC,
    M3_PERF_DRONEID_CP_FUSION,
    M3_PERF_SFO_ESTIMATION,
    M3_PERF_BLE_SYNC,
    M3_PERF_BLE_PREAMBLE_AA,
    M3_PERF_BLE_PHASE_SEARCH,
    M3_PERF_BLE_RESIDUAL_CFO,
    M3_PERF_BLE_LOW_SNR_RECOVERY,
    M3_PERF_FRAME_ALIGNMENT,
    M3_PERF_UNKNOWN_CONTROL_SYNC,
    M3_PERF_OTHER,
    M3_PERF_STAGE_COUNT
} m3_perf_stage_t;

typedef enum {
    M3_PERF_OP_FFT_CALLS = 0,
    M3_PERF_OP_DFT_CALLS,
    M3_PERF_OP_CORRELATION_CALLS,
    M3_PERF_OP_CORRELATION_EVALUATIONS,
    M3_PERF_OP_COMPLEX_ROTATION_CALLS,
    M3_PERF_OP_COMPLEX_ROTATION_SAMPLES,
    M3_PERF_OP_CFO_CANDIDATES,
    M3_PERF_OP_INTEGER_CFO_CANDIDATES,
    M3_PERF_OP_BLE_SAMPLING_PHASES,
    M3_PERF_OP_BLE_RESIDUAL_CFO_CANDIDATES,
    M3_PERF_OP_BLE_HYPOTHESES,
    M3_PERF_OP_CRC_CHECKS,
    M3_PERF_OP_FRAME_CANDIDATES,
    M3_PERF_OP_MALLOC_CALLS,
    M3_PERF_OP_CALLOC_CALLS,
    M3_PERF_OP_FREE_CALLS,
    M3_PERF_OP_COUNT
} m3_perf_operation_t;

#ifdef WRJ_ENABLE_PROFILING
typedef uint64_t m3_perf_tick_t;

void m3_perf_reset(void);
m3_perf_tick_t m3_perf_now(void);
void m3_perf_record(m3_perf_stage_t stage, m3_perf_tick_t start, m3_perf_tick_t end);
void m3_perf_count(m3_perf_operation_t operation, uint64_t amount);
wrj_status_t m3_perf_write_csv(const char *output_dir, const char *candidate_id);

#define M3_PERF_TIMER(name) m3_perf_tick_t name = 0U
#define M3_PERF_START(name) ((name) = m3_perf_now())
#define M3_PERF_STOP(stage, name) m3_perf_record((stage), (name), m3_perf_now())
#define M3_PERF_COUNT(operation, amount) m3_perf_count((operation), (uint64_t)(amount))
#else
#define M3_PERF_TIMER(name)
#define M3_PERF_START(name) ((void)0)
#define M3_PERF_STOP(stage, name) ((void)0)
#define M3_PERF_COUNT(operation, amount) ((void)0)
#endif

#endif
