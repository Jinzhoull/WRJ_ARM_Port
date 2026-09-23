#include "m3_module3.h"
#include "m4_module4.h"
#include "wrj_io.h"
#include "common/perf_timer.h"

#include <stdlib.h>

static void wrj_print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s --handoff <module12_to_module3_handoff.csv> --candidate <id> --iq <candidate.cf32> [--out <directory>]\n", program);
}

int main(int argc, char **argv)
{
    const char *handoff_path = NULL;
    const char *candidate_id = NULL;
    const char *iq_path = NULL;
    const char *output_dir = "results";
    wrj_candidate_t candidate;
    m3_config_t m3_config;
    m4_config_t m4_config;
    m4_workspace_t m4_workspace;
    m3_workspace_t workspace;
    m3_result_t m3_result;
    m4_result_t m4_result;
    wrj_status_t status;
    uint32_t sample_count = 0U;
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--handoff") == 0 && index + 1 < argc) {
            handoff_path = argv[++index];
        } else if (strcmp(argv[index], "--candidate") == 0 && index + 1 < argc) {
            candidate_id = argv[++index];
        } else if (strcmp(argv[index], "--iq") == 0 && index + 1 < argc) {
            iq_path = argv[++index];
        } else if (strcmp(argv[index], "--out") == 0 && index + 1 < argc) {
            output_dir = argv[++index];
        } else {
            wrj_print_usage(argv[0]);
            return 2;
        }
    }
    if (handoff_path == NULL || candidate_id == NULL || iq_path == NULL) {
        wrj_print_usage(argv[0]);
        return 2;
    }
    status = wrj_load_handoff_candidate(handoff_path, candidate_id, &candidate);
    if (status != WRJ_OK) {
        fprintf(stderr, "handoff read failed: %d\n", status);
        return 1;
    }
#ifdef WRJ_ENABLE_PROFILING
    m3_perf_reset();
#endif
    m3_default_config(&m3_config);
    status = m3_workspace_init(&workspace, &m3_config);
    if (status != WRJ_OK) {
        fprintf(stderr, "workspace allocation failed: %d\n", status);
        return 1;
    }
    status = wrj_read_cf32(iq_path, workspace.compensated, workspace.max_samples, &sample_count);
    if (status != WRJ_OK) {
        fprintf(stderr, "IQ read failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    status = m3_run(&candidate, workspace.compensated, sample_count, &m3_config, &workspace, &m3_result);
    if (status != WRJ_OK && status != WRJ_ERR_UNSUPPORTED) {
        fprintf(stderr, "Module3 failed: %d (%s)\n", status, m3_result.status);
        m3_workspace_release(&workspace);
        return 1;
    }
    m4_default_config(&m4_config);
    status = m4_workspace_bind(&m4_workspace, workspace.baseband, workspace.metric,
                               workspace.scratch, workspace.fft_re, workspace.fft_im,
                               workspace.max_samples, workspace.fft_size);
    if (status != WRJ_OK) {
        fprintf(stderr, "Module4 workspace bind failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    status = m4_run(&candidate, &m3_result, workspace.compensated, sample_count,
                    &m4_config, &m4_workspace, &m4_result);
    if (status != WRJ_OK) {
        fprintf(stderr, "Module4 failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    status = wrj_write_results(output_dir, &candidate, &m3_result, &m4_result);
    if (status != WRJ_OK) {
        fprintf(stderr, "result write failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    printf("candidate=%s profile=%s status=%s cfo_hz=%.3f sync_confidence=%.4f frame_start0=%u frame_length=%u frames=%u\n",
           candidate.candidate_id, m3_result.profile_name, m3_result.status,
           m3_result.estimated_cfo_hz, m3_result.sync_confidence,
           m3_result.num_frames == 0U ? 0U : m3_result.frame_start_samples_0based[0],
           m3_result.frame_length_samples, m3_result.num_frames);
    printf("module4=%s source=%s bytes=%u packets=%u crc=%s fields=%u workspace_mib=%.2f\n",
           wrj_m4_status_name(m4_result.status),
           m4_result.byte_source == M4_BYTE_SOURCE_REAL_IQ ? "REAL_IQ" : "NONE",
           m4_result.byte_count, m4_result.packet_count,
           m4_result.crc_passed != 0U ? "PASS" : "NOT_VERIFIED",
           m4_result.field_count,
           (double)m3_workspace_bytes(&workspace) / (1024.0 * 1024.0));
    if (m3_result.profile == WRJ_PROFILE_REMOTEID_BLE && m4_result.parse_complete != 0U) {
        printf("RemoteID Basic ID=%s latitude=%.7f longitude=%.7f altitude_m=%.1f speed_mps=%.2f heading_deg=%.1f\n",
               m4_result.uas_id, m4_result.latitude_deg, m4_result.longitude_deg,
               m4_result.altitude_m, m4_result.speed_mps, m4_result.heading_deg);
    }
    m3_workspace_release(&workspace);
#ifdef WRJ_ENABLE_PROFILING
    status = m3_perf_write_csv(output_dir, candidate.candidate_id);
    if (status != WRJ_OK) {
        fprintf(stderr, "Module3 profiling CSV write failed: %d\n", status);
    }
#endif
    return 0;
}
