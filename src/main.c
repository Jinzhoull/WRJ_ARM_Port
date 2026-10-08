#include "m3_module3.h"
#include "m4_module4.h"
#include "m4_process.h"
#include "wrj_io.h"
#include "common/perf_timer.h"

#include <dirent.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define WRJ_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define WRJ_MKDIR(path) mkdir(path, 0755)
#endif

typedef struct {
    wrj_candidate_t candidate;
    char iq_path[WRJ_PATH_CAPACITY];
    char sort_key[WRJ_ID_CAPACITY * 2U + 8U];
} batch_item_t;

static void wrj_print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s --handoff <handoff.csv> (--candidate <id> --iq <candidate.cf32> | --iq-dir <directory>) [--out <directory>] [--dump-m3-iq <diagnostic.cf32>]\n", program);
}

static int compare_batch_items(const void *left, const void *right)
{
    const batch_item_t *a = (const batch_item_t *)left;
    const batch_item_t *b = (const batch_item_t *)right;
    return strcmp(a->sort_key, b->sort_key);
}

static int run_candidate(const wrj_candidate_t *candidate, const char *iq_path,
    const char *output_dir, const char *dump_m3_iq_path,
    module4_process_t *m4_process)
{
    m3_config_t m3_config;
    m4_config_t m4_config;
    m4_workspace_t m4_workspace;
    m3_workspace_t workspace;
    m3_result_t m3_result;
    m4_result_t m4_result;
    m4_to_m5_handoff_t m4_handoff;
    wrj_status_t status;
    uint32_t sample_count = 0U;
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
    status = m3_run(candidate, workspace.compensated, sample_count, &m3_config, &workspace, &m3_result);
    if (status != WRJ_OK && status != WRJ_ERR_UNSUPPORTED) {
        fprintf(stderr, "Module3 failed: %d (%s)\n", status, m3_result.status);
        m3_workspace_release(&workspace);
        return 1;
    }
    if (dump_m3_iq_path != NULL) {
        FILE *dump = fopen(dump_m3_iq_path, "wb");
        if (dump == NULL || fwrite(workspace.compensated, sizeof(wrj_cf32_t),
                                  sample_count, dump) != sample_count) {
            fprintf(stderr, "Module3 diagnostic IQ write failed\n");
            if (dump != NULL) fclose(dump);
            m3_workspace_release(&workspace);
            return 1;
        }
        if (fclose(dump) != 0) {
            fprintf(stderr, "Module3 diagnostic IQ close failed\n");
            m3_workspace_release(&workspace);
            return 1;
        }
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
    status = module4_process_candidate(m4_process, candidate, &m3_result,
                    workspace.compensated, sample_count,
                    &m4_config, &m4_workspace, &m4_result);
    if (status != WRJ_OK) {
        fprintf(stderr, "Module4 failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    status = wrj_write_results(output_dir, candidate, &m3_result, &m4_result);
    if (status != WRJ_OK) {
        fprintf(stderr, "result write failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    m4_build_module5_handoff(candidate, &m3_result, &m4_result, &m4_handoff);
    status = wrj_write_m4_handoff_csv(output_dir, &m4_handoff);
    if (status != WRJ_OK) {
        fprintf(stderr, "Module4-to-Module5 handoff write failed: %d\n", status);
        m3_workspace_release(&workspace);
        return 1;
    }
    printf("candidate=%s profile=%s status=%s cfo_hz=%.3f sync_confidence=%.4f frame_start0=%u frame_length=%u frames=%u\n",
           candidate->candidate_id, m3_result.profile_name, m3_result.status,
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
    status = m3_perf_write_csv(output_dir, candidate->candidate_id);
    if (status != WRJ_OK) {
        fprintf(stderr, "Module3 profiling CSV write failed: %d\n", status);
    }
#endif
    return 0;
}

int main(int argc, char **argv)
{
    const char *handoff_path = NULL, *candidate_id = NULL, *iq_path = NULL;
    const char *iq_dir = NULL, *output_dir = "results", *dump_m3_iq_path = NULL;
    module4_process_t process;
    module4_process_analysis_t *analysis;
    wrj_status_t status;
    int index, failed = 0;
    uint32_t candidate_count = 1U;
    batch_item_t *items = NULL;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--handoff") == 0 && index + 1 < argc)
            handoff_path = argv[++index];
        else if (strcmp(argv[index], "--candidate") == 0 && index + 1 < argc)
            candidate_id = argv[++index];
        else if (strcmp(argv[index], "--iq") == 0 && index + 1 < argc)
            iq_path = argv[++index];
        else if (strcmp(argv[index], "--iq-dir") == 0 && index + 1 < argc)
            iq_dir = argv[++index];
        else if (strcmp(argv[index], "--out") == 0 && index + 1 < argc)
            output_dir = argv[++index];
        else if (strcmp(argv[index], "--dump-m3-iq") == 0 && index + 1 < argc)
            dump_m3_iq_path = argv[++index];
        else { wrj_print_usage(argv[0]); return 2; }
    }
    if (handoff_path == NULL ||
        ((iq_dir == NULL) == (candidate_id == NULL || iq_path == NULL)) ||
        (iq_dir != NULL && dump_m3_iq_path != NULL)) {
        wrj_print_usage(argv[0]); return 2;
    }
    if (iq_dir != NULL) {
        DIR *dir = opendir(iq_dir);
        struct dirent *entry;
        uint32_t capacity = 0U;
        if (dir == NULL) { fprintf(stderr, "IQ directory open failed\n"); return 1; }
        while ((entry = readdir(dir)) != NULL) {
            size_t length = strlen(entry->d_name);
            batch_item_t *item;
            char id[WRJ_ID_CAPACITY];
            if (length < 6U || strcmp(entry->d_name + length - 5U, ".cf32") != 0)
                continue;
            if (length - 5U >= sizeof(id)) { failed = 1; break; }
            memcpy(id, entry->d_name, length - 5U);
            id[length - 5U] = '\0';
            if (candidate_count > capacity) {
                uint32_t next_capacity = capacity == 0U ? 16U : capacity * 2U;
                batch_item_t *next = (batch_item_t *)realloc(items,
                    (size_t)next_capacity * sizeof(*items));
                if (next == NULL) { failed = 1; break; }
                items = next;
                capacity = next_capacity;
            }
            item = &items[candidate_count - 1U];
            status = wrj_load_handoff_candidate(handoff_path, id, &item->candidate);
            if (status != WRJ_OK ||
                snprintf(item->iq_path, sizeof(item->iq_path), "%s/%s", iq_dir,
                         entry->d_name) >= (int)sizeof(item->iq_path) ||
                snprintf(item->sort_key, sizeof(item->sort_key), "%s__%s.mat",
                         item->candidate.source_file, item->candidate.candidate_id) >=
                    (int)sizeof(item->sort_key)) { failed = 1; break; }
            ++candidate_count;
        }
        closedir(dir);
        --candidate_count;
        if (failed || candidate_count == 0U) { free(items); return 1; }
        qsort(items, candidate_count, sizeof(*items), compare_batch_items);
        (void)WRJ_MKDIR(output_dir);
    }
    status = module4_process_init(&process, candidate_count);
    if (status != WRJ_OK) { free(items); return 1; }
    if (iq_dir == NULL) {
        wrj_candidate_t candidate;
        status = wrj_load_handoff_candidate(handoff_path, candidate_id, &candidate);
        if (status != WRJ_OK || run_candidate(&candidate, iq_path, output_dir,
                                               dump_m3_iq_path, &process) != 0)
            failed = 1;
    } else {
        uint32_t i;
        for (i = 0U; i < candidate_count; ++i) {
            char candidate_output[WRJ_PATH_CAPACITY];
            if (snprintf(candidate_output, sizeof(candidate_output), "%s/%s",
                         output_dir, items[i].candidate.candidate_id) >=
                    (int)sizeof(candidate_output) ||
                run_candidate(&items[i].candidate, items[i].iq_path,
                              candidate_output, NULL, &process) != 0) {
                failed = 1; break;
            }
        }
    }
    analysis = (module4_process_analysis_t *)malloc(sizeof(*analysis));
    if (!failed && analysis != NULL &&
        module4_process_finalize(&process, analysis) != WRJ_OK) failed = 1;
    if (analysis == NULL) failed = 1;
    free(analysis);
    module4_process_release(&process);
    free(items);
    return failed;
}
