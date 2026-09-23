#define _POSIX_C_SOURCE 200809L

#include "common/perf_timer.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct {
    uint64_t calls;
    uint64_t ticks;
} m3_perf_stage_data_t;

static m3_perf_stage_data_t m3_perf_stages[M3_PERF_STAGE_COUNT];
static uint64_t m3_perf_operations[M3_PERF_OP_COUNT];

static const char *const m3_perf_stage_names[M3_PERF_STAGE_COUNT] = {
    "M3_TOTAL", "IQ_PREPROCESS", "CENTER_SHIFT", "SPECTRUM_ANALYSIS",
    "PROFILE_SELECTION", "COARSE_CFO", "INTEGER_CFO", "FRACTIONAL_CFO",
    "CFO_COMPENSATION", "BANDLIMIT_FIR", "WIDEBAND_SYNC", "DRONEID_SYNC",
    "DRONEID_ZC", "DRONEID_CP_FUSION", "SFO_ESTIMATION", "BLE_SYNC",
    "BLE_PREAMBLE_AA", "BLE_PHASE_SEARCH", "BLE_RESIDUAL_CFO",
    "BLE_LOW_SNR_RECOVERY", "FRAME_ALIGNMENT", "UNKNOWN_CONTROL_SYNC", "OTHER"
};

static const char *const m3_perf_operation_names[M3_PERF_OP_COUNT] = {
    "FFT_CALLS", "DFT_CALLS", "CORRELATION_CALLS", "CORRELATION_EVALUATIONS",
    "COMPLEX_ROTATION_CALLS", "COMPLEX_ROTATION_SAMPLES", "CFO_CANDIDATES",
    "INTEGER_CFO_CANDIDATES", "BLE_SAMPLING_PHASES", "BLE_RESIDUAL_CFO_CANDIDATES",
    "BLE_HYPOTHESES", "CRC_CHECKS", "FRAME_CANDIDATES", "MALLOC_CALLS",
    "CALLOC_CALLS", "FREE_CALLS"
};

static double m3_perf_ticks_to_ms(uint64_t ticks)
{
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    static int frequency_ready = 0;
    if (frequency_ready == 0) {
        if (QueryPerformanceFrequency(&frequency) == 0 || frequency.QuadPart <= 0) {
            return 0.0;
        }
        frequency_ready = 1;
    }
    return 1000.0 * (double)ticks / (double)frequency.QuadPart;
#else
    return (double)ticks / 1000000.0;
#endif
}

static double m3_perf_stage_time_ms(m3_perf_stage_t stage)
{
    return m3_perf_ticks_to_ms(m3_perf_stages[stage].ticks);
}

static double m3_perf_unclassified_time_ms(double total_ms)
{
    double accounted_ms = 0.0;
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_IQ_PREPROCESS);
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_CENTER_SHIFT);
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_PROFILE_SELECTION);
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_COARSE_CFO);
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_BANDLIMIT_FIR);
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_INTEGER_CFO);
    accounted_ms += m3_perf_stage_time_ms(M3_PERF_SFO_ESTIMATION);

    /* Add exactly one top-level synchronization path. Its detailed child
     * stages are inclusive diagnostics and are deliberately not double-counted. */
    if (m3_perf_stages[M3_PERF_DRONEID_SYNC].calls > 0U) {
        accounted_ms += m3_perf_stage_time_ms(M3_PERF_DRONEID_SYNC);
    } else if (m3_perf_stages[M3_PERF_BLE_SYNC].calls > 0U) {
        accounted_ms += m3_perf_stage_time_ms(M3_PERF_BLE_SYNC);
    } else if (m3_perf_stages[M3_PERF_WIDEBAND_SYNC].calls > 0U) {
        accounted_ms += m3_perf_stage_time_ms(M3_PERF_WIDEBAND_SYNC);
        accounted_ms += m3_perf_stage_time_ms(M3_PERF_FRAME_ALIGNMENT);
    } else if (m3_perf_stages[M3_PERF_UNKNOWN_CONTROL_SYNC].calls > 0U) {
        accounted_ms += m3_perf_stage_time_ms(M3_PERF_UNKNOWN_CONTROL_SYNC);
    }
    return WRJ_MAX(total_ms - accounted_ms, 0.0);
}

void m3_perf_reset(void)
{
    memset(m3_perf_stages, 0, sizeof(m3_perf_stages));
    memset(m3_perf_operations, 0, sizeof(m3_perf_operations));
}

m3_perf_tick_t m3_perf_now(void)
{
#if defined(_WIN32)
    LARGE_INTEGER value;
    if (QueryPerformanceCounter(&value) == 0 || value.QuadPart < 0) {
        return 0U;
    }
    return (uint64_t)value.QuadPart;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0U;
    }
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#endif
}

void m3_perf_record(m3_perf_stage_t stage, m3_perf_tick_t start, m3_perf_tick_t end)
{
    if ((uint32_t)stage >= (uint32_t)M3_PERF_STAGE_COUNT || end < start) {
        return;
    }
    ++m3_perf_stages[stage].calls;
    m3_perf_stages[stage].ticks += end - start;
}

void m3_perf_count(m3_perf_operation_t operation, uint64_t amount)
{
    if ((uint32_t)operation < (uint32_t)M3_PERF_OP_COUNT) {
        m3_perf_operations[operation] += amount;
    }
}

wrj_status_t m3_perf_write_csv(const char *output_dir, const char *candidate_id)
{
    char path[1024];
    FILE *file;
    double total_ms;
    double other_ms;
    uint32_t index;
    if (output_dir == NULL || candidate_id == NULL) {
        return WRJ_ERR_ARGUMENT;
    }
    if (snprintf(path, sizeof(path), "%s/profiling_module3.csv", output_dir) >= (int)sizeof(path)) {
        return WRJ_ERR_CAPACITY;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return WRJ_ERR_IO;
    }
    total_ms = m3_perf_ticks_to_ms(m3_perf_stages[M3_PERF_M3_TOTAL].ticks);
    other_ms = m3_perf_unclassified_time_ms(total_ms);
    fprintf(file, "case,stage,calls,time_ms,percentage\n");
    for (index = 0U; index < (uint32_t)M3_PERF_STAGE_COUNT; ++index) {
        const double time_ms = index == (uint32_t)M3_PERF_OTHER ? other_ms :
            m3_perf_ticks_to_ms(m3_perf_stages[index].ticks);
        const double percentage = total_ms > 0.0 ? 100.0 * time_ms / total_ms : 0.0;
        const uint64_t calls = index == (uint32_t)M3_PERF_OTHER && other_ms > 0.0 ? 1U :
            m3_perf_stages[index].calls;
        fprintf(file, "%s,%s,%llu,%.6f,%.3f\n", candidate_id, m3_perf_stage_names[index],
                (unsigned long long)calls, time_ms, percentage);
    }
    for (index = 0U; index < (uint32_t)M3_PERF_OP_COUNT; ++index) {
        fprintf(file, "%s,%s,%llu,0.000000,0.000\n", candidate_id,
                m3_perf_operation_names[index],
                (unsigned long long)m3_perf_operations[index]);
    }
    if (fclose(file) != 0) {
        return WRJ_ERR_IO;
    }
    return WRJ_OK;
}
