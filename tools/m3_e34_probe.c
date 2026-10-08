#include "wrj_e34_module3.h"
#include "wrj_port_version.h"
#include "wrj_io.h"
#include <stdlib.h>
#include <time.h>

static int dump_iq(const char *root, const char *name, const wrj_cf32_t *iq, uint32_t count)
{
    char path[WRJ_PATH_CAPACITY];
    FILE *file;
    int ok;
    if (snprintf(path, sizeof(path), "%s/%s.cf32", root, name) >= (int)sizeof(path)) return 0;
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    ok = fwrite(iq, sizeof(*iq), count, file) == count;
    return fclose(file) == 0 && ok;
}

int main(int argc, char **argv)
{
    wrj_candidate_t candidate;
    m3_config_t config;
    m3_workspace_t workspace;
    wrj_sync_result_t result;
    wrj_cf32_t *input;
    FILE *file;
    long bytes;
    uint32_t count, read_count, k;
    wrj_status_t code;
    char path[WRJ_PATH_CAPACITY];
    clock_t started;
    int ok = 1;
    if (argc != 6 || (strcmp(argv[1], "frontend") != 0 && strcmp(argv[1], "fast") != 0)) {
        fprintf(stderr, "Usage: %s frontend|fast handoff.csv candidate capture.cf32 output_dir\n", argv[0]);
        return 2;
    }
    if (wrj_load_handoff_candidate(argv[2], argv[3], &candidate) != WRJ_OK) return 3;
    file = fopen(argv[4], "rb");
    if (file == NULL) return 4;
    if (fseek(file, 0, SEEK_END) != 0 || (bytes = ftell(file)) <= 0 ||
        (unsigned long)bytes % sizeof(wrj_cf32_t) != 0U) { fclose(file); return 4; }
    fclose(file);
    count = (uint32_t)((unsigned long)bytes / sizeof(wrj_cf32_t));
    wrj_module3_fast_config(&config, count);
    if (m3_workspace_init(&workspace, &config) != WRJ_OK) return 5;
    memset(&result, 0, sizeof(result));
    input = calloc(count, sizeof(*input));
    result.input_baseband = calloc(count, sizeof(*input));
    result.preprocessed_baseband = calloc(count, sizeof(*input));
    if (input == NULL || result.input_baseband == NULL || result.preprocessed_baseband == NULL) {
        ok = 0; goto finish;
    }
    if (wrj_read_cf32(argv[4], input, count, &read_count) != WRJ_OK || read_count != count ||
        wrj_make_directory(argv[5]) != WRJ_OK) { ok = 0; goto finish; }
    started = clock();
    if (strcmp(argv[1], "frontend") == 0) {
        memcpy(workspace.compensated, input, sizeof(*input) * (size_t)count);
        code = m3_preprocess_iq(workspace.compensated, count, &workspace);
        ok = code == WRJ_OK && dump_iq(argv[5], "preprocessed_capture", workspace.compensated, count);
        m3_cfo_compensate(input, result.input_baseband, count,
            candidate.sample_rate_hz, candidate.candidate_center_offset_hz);
        m3_cfo_compensate(workspace.compensated, result.preprocessed_baseband, count,
            candidate.sample_rate_hz, candidate.candidate_center_offset_hz);
    } else {
        code = wrj_module3_fast(&candidate, input, count, &config, &workspace, &result);
        if (code != WRJ_OK) { fprintf(stderr,"M3 error=%d status=%s\n",code,result.sync.status); ok = 0; }
        if (result.compensated_iq != NULL) ok &= dump_iq(argv[5], "compensated", result.compensated_iq, count);
    }
    ok &= dump_iq(argv[5], "raw_baseband", result.input_baseband, count);
    ok &= dump_iq(argv[5], "preprocessed_baseband", result.preprocessed_baseband, count);
    snprintf(path, sizeof(path), "%s/summary.csv", argv[5]);
    file = fopen(path, "w");
    if (file == NULL) { ok = 0; goto finish; }
    fprintf(file,"candidate,status,syncAccepted,profile,CFOHz,SfoPpm,frameCount,frameLength,runtimeSeconds,implementationStage,goldenVersion,portVersion,sourceFingerprint\n");
    fprintf(file,"%s,%s,%u,%s,%.17g,%.9g,%u,%u,%.9g,%s,%s,%s,%s\n",argv[3],
        result.sync.status,result.sync_accepted,result.sync.profile_name,result.sync.estimated_cfo_hz,
        result.sync.estimated_sfo_ppm,result.sync.num_frames,result.sync.frame_length_samples,
        (double)(clock()-started)/CLOCKS_PER_SEC,WRJ_M3_PORT_STAGE,MATLAB_GOLDEN_VERSION,C_PORT_VERSION,SOURCE_FINGERPRINT);
    ok &= fclose(file) == 0;
    snprintf(path,sizeof(path),"%s/cfo_stages.csv",argv[5]);file=fopen(path,"w");
    if(file==NULL){ok=0;goto finish;}
    fprintf(file,"candidate,stage,spectralCoarseHz,CPPeaks,NFFT,CPLength,correlationRe,correlationIm,angleRad,CPDerivedHz,stage1Hz,stage2Hz,combinedHz,appliedHz,residualHz,phaseOrigin\n");
    for(k=0U;k<3U;++k){const m3_cp_probe_t *probe=result.sync.cp_probe+k;
        fprintf(file,"%s,%u,%.17g,%u,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%u\n",
            argv[3],k+1U,result.sync.spectral_correction_hz,probe->peak_count,probe->nfft,probe->cp_samples,
            probe->correlation_re,probe->correlation_im,probe->angle_rad,probe->cycles_per_sample*candidate.sample_rate_hz,
            result.sync.stage1_cfo_hz,result.sync.stage2_cfo_hz,result.sync.combined_cfo_hz,result.sync.applied_cfo_hz,
            result.sync.residual_probe_hz,result.sync.phase_origin);
    }
    ok &= fclose(file)==0;
    snprintf(path, sizeof(path), "%s/frames.csv", argv[5]);
    file = fopen(path,"w");
    if (file == NULL) { ok = 0; goto finish; }
    fprintf(file,"candidate,index,start0,confidence\n");
    for(k=0U;k<result.sync.num_frames;++k) fprintf(file,"%s,%u,%u,%.9g\n",argv[3],k+1U,
        result.sync.frame_start_samples_0based[k],result.sync.frame_confidence[k]);
    ok &= fclose(file) == 0;
    snprintf(path,sizeof(path),"%s/cached_pdus.csv",argv[5]);
    file=fopen(path,"w");
    if(file==NULL){ok=0;goto finish;}
    fprintf(file,"candidate,source,index,start0,confidence,bytesHex,receivedCRC,rawBits,receivedCRCBits\n");
    for(k=0U;k<result.sync.ble_verified_packet_count;++k){
        fprintf(file,"%s,%s,%u,%u,%.9g,",result.sync.ble_origin_candidate,result.sync.ble_origin_source,k,
            result.sync.ble_verified_start[k],result.sync.ble_verified_confidence[k]);
        for(uint32_t j=0U;j<39U;++j)fprintf(file,"%02X",result.sync.ble_verified_pdu[k][j]);
        fprintf(file,",%u,",result.sync.ble_verified_crc[k]);
        for(uint32_t j=0U;j<336U;++j)fprintf(file,"%u",result.sync.ble_verified_raw_bits[k][j]);
        fputc(',',file);
        for(uint32_t j=312U;j<336U;++j)fprintf(file,"%u",result.sync.ble_verified_raw_bits[k][j]);
        fputc('\n',file);
    }
    ok &= fclose(file)==0;
finish:
    free(input); free(result.input_baseband); free(result.preprocessed_baseband);
    m3_workspace_release(&workspace);
    return ok ? 0 : 6;
}
