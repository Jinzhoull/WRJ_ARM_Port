#include "m3_module3.h"
#include "m4_module4.h"

#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv)
{
    m3_config_t m3_config;
    m3_workspace_t m3_workspace;
    m4_workspace_t m4_workspace;
    m4_config_t m4_config;
    m3_result_t m3;
    m4_result_t m4;
    wrj_candidate_t candidate;
    uint32_t count = 0U;
    uint16_t i;
    if (argc != 3) {
        fprintf(stderr, "usage: m4_frame_probe wideband|droneid frameLength < CF32\n");
        return 2;
    }
    memset(&candidate, 0, sizeof(candidate));
    memset(&m3, 0, sizeof(m3));
    m3.profile = strcmp(argv[1], "droneid") == 0 ? WRJ_PROFILE_DRONEID_ZC :
                 WRJ_PROFILE_DJI_WIDEBAND_CP;
    m3.frame_length_samples = (uint32_t)strtoul(argv[2], NULL, 10);
    m3.num_frames = 1U;
    m3.frame_start_samples_0based[0] = 0U;
    candidate.sample_rate_hz = 30720000.0f;
    m3_default_config(&m3_config);
    if (m3_workspace_init(&m3_workspace, &m3_config) != WRJ_OK) return 1;
#ifdef _WIN32
    (void)_setmode(_fileno(stdin), _O_BINARY);
#endif
    while (count < m3_workspace.max_samples &&
           fread(&m3_workspace.compensated[count], sizeof(wrj_cf32_t), 1U, stdin) == 1U)
        ++count;
    m4_default_config(&m4_config);
    if (m4_workspace_bind(&m4_workspace, m3_workspace.baseband, m3_workspace.metric,
                          m3_workspace.scratch, m3_workspace.fft_re, m3_workspace.fft_im,
                          m3_workspace.max_samples, m3_workspace.fft_size) != WRJ_OK) return 1;
    if (m4_run(&candidate, &m3, m3_workspace.compensated, count, &m4_config,
               &m4_workspace, &m4) != WRJ_OK) return 1;
    printf("method=%s count=%u %s hex=", m4.decoder_method, m4.byte_count,
           m4.status_text[0] == '\0' ? "sps=0;offset=0;phase=0;score=0" : m4.status_text);
    for (i = 0U; i < m4.byte_count; ++i) printf("%02X", m4.bytes[i]);
    putchar('\n');
    m3_workspace_release(&m3_workspace);
    return 0;
}
