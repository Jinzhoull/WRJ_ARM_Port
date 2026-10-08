#include "wrj_receiver.h"
#include "wrj_recovery_machine.h"
#include "wrj_io.h"
#include "wrj_port_version.h"
#include <stdlib.h>
#include <time.h>
static void hex(FILE *f,const uint8_t *bytes,uint32_t n)
{for(uint32_t k=0U;k<n;++k)fprintf(f,"%02X",bytes[k]);}
int main(int argc,char **argv)
{
    if(argc!=6 || (strcmp(argv[1],"fast") && strcmp(argv[1],"deep2") && strcmp(argv[1],"deep3") && strcmp(argv[1],"host"))){
        fprintf(stderr,"usage: e34_receiver_probe fast|deep2|deep3|host handoff.csv candidate actual.cf32 outdir\n");return 2;}
    wrj_candidate_t candidate;if(wrj_load_handoff_candidate(argv[2],argv[3],&candidate)!=WRJ_OK)return 3;
    FILE *f=fopen(argv[4],"rb");if(!f)return 4;if(fseek(f,0,SEEK_END)!=0){fclose(f);return 4;}
    const long length=ftell(f);fclose(f);if(length<=0 || (unsigned long)length%sizeof(wrj_cf32_t))return 4;
    const uint32_t n=(uint32_t)((unsigned long)length/sizeof(wrj_cf32_t));uint32_t read=0U;
    wrj_cf32_t *actual=calloc(n,sizeof(*actual));wrj_sync_result_t sync;memset(&sync,0,sizeof(sync));
    sync.input_baseband=calloc(n,sizeof(*actual));sync.preprocessed_baseband=calloc(n,sizeof(*actual));
    m3_config_t cfg;wrj_module3_fast_config(&cfg,n);m3_workspace_t workspace;memset(&workspace,0,sizeof(workspace));
    wrj_parse_result_t parse;memset(&parse,0,sizeof(parse));int result=5;
    if(!actual || !sync.input_baseband || !sync.preprocessed_baseband || m3_workspace_init(&workspace,&cfg)!=WRJ_OK ||
        wrj_parse_result_init(&parse)!=WRJ_OK)goto finish;
    if(wrj_read_cf32(argv[4],actual,n,&read)!=WRJ_OK || read!=n || wrj_make_directory(argv[5])!=WRJ_OK)goto finish;
    const clock_t begin=clock();if(wrj_module3_fast(&candidate,actual,n,&cfg,&workspace,&sync)!=WRJ_OK)goto finish;
    const double m3_seconds=(double)(clock()-begin)/CLOCKS_PER_SEC;
    const int host=strcmp(argv[1],"host")==0;
    wrj_status_t code=(strcmp(argv[1],"fast")==0 || host)?wrj_module4_fast(&candidate,&sync,&parse):
        wrj_module4_deep(&candidate,&sync,strcmp(argv[1],"deep2")==0?2U:3U,&parse);
    if(code!=WRJ_OK)goto finish;
    const uint8_t fast_complete=parse.complete,initial_m3_accepted=sync.sync_accepted;
    char initial_failure[48];snprintf(initial_failure,sizeof(initial_failure),"%s",parse.failure_stage);
    const uint32_t fast_records=parse.count;wrj_recovery_result_t recovery;memset(&recovery,0,sizeof(recovery));
    if(host){code=wrj_recovery_machine(&candidate,actual,n,&workspace,&sync,&parse,&recovery);if(code!=WRJ_OK)goto finish;}
    char path[WRJ_PATH_CAPACITY];snprintf(path,sizeof(path),"%s/optimization_profile.csv",argv[5]);f=fopen(path,"w");if(!f)goto finish;
    fprintf(f,"Candidate,M3FastMs,M4FastMs,DiagnoseMs,M3DeepMs,M4FastAfterSyncMs,M4DeepL1Ms,M4DeepL2Ms,M4DeepL3Ms,ProtocolRecoveryMs,GateMs,IOTimeMs,TotalRecoveryMs,FFTCalls,FFTCacheHits,TotalHypotheses,UniqueHypotheses,DuplicatesSkipped\n");
    fprintf(f,"%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,NaN,%.9g,%llu,%llu,%llu,%llu,%llu\n",candidate.candidate_id,
        1000*m3_seconds,1000*parse.seconds,1000*recovery.diagnose_seconds,1000*recovery.m3_deep_seconds,1000*recovery.m4_after_sync_seconds,
        1000*recovery.deep_seconds[0],1000*recovery.deep_seconds[1],1000*recovery.deep_seconds[2],1000*recovery.protocol_seconds,
        1000*recovery.gate_seconds,1000*recovery.seconds,(unsigned long long)parse.fft_calls,(unsigned long long)parse.fft_cache_hits,
        (unsigned long long)parse.total_hypotheses,(unsigned long long)parse.unique_hypotheses,(unsigned long long)parse.duplicates_skipped);fclose(f);
    snprintf(path,sizeof(path),"%s/summary.csv",argv[5]);f=fopen(path,"w");if(!f)goto finish;
    fprintf(f,"candidate,M3Status,M3SyncAccepted,M3Profile,CFOHz,M3Seconds,M4Status,complete,family,records,cpPeaks,syncSuccess,cfoSuccess,symbolSuccess,frameCandidates,CRCChecked,CRCPass,semanticParse,failureStage,M4Seconds,SourceFingerprint,M4FastComplete,FastRecords,InitialM3Accepted,M5Success,DeepAttempted,DeepRecovered,M3DeepCalls,M4DeepCalls,ProtocolCalls,M5CRCValid,M5SemanticComplete,RecoverySeconds,StopReason,AssociationMode,M3FrameCount,M3Confidence,InitialFailureStage,SampleCount,SampleRateHz\n");
    fprintf(f,"%s,%s,%u,%s,%.17g,%.9g,%s,%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s,%.9g,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%s,%s,%u,%.9g,%s,%u,%.17g\n",candidate.candidate_id,
        sync.sync.status,sync.sync_accepted,sync.sync.profile_name,sync.sync.estimated_cfo_hz,m3_seconds,parse.status,parse.complete,parse.family,parse.count,
        parse.stages.cp_peaks,parse.stages.sync_success,parse.stages.cfo_success,parse.stages.symbol_success,parse.stages.frame_candidates,
        parse.stages.crc_checked,parse.stages.crc_pass,parse.stages.semantic_parse,parse.failure_stage,parse.seconds,SOURCE_FINGERPRINT,
        fast_complete,fast_records,initial_m3_accepted,recovery.success,recovery.attempted,recovery.recovered,recovery.m3_calls,recovery.m4_calls,
        recovery.protocol_calls,recovery.proof.crc_valid,recovery.proof.semantic_complete,recovery.seconds,recovery.stop_reason,parse.association_mode,
        sync.sync.num_frames,sync.sync.sync_confidence,initial_failure,n,sync.fs);fclose(f);
    if(host){snprintf(path,sizeof(path),"%s/recovery_trace.csv",argv[5]);f=fopen(path,"w");if(!f)goto finish;
        fprintf(f,"candidate,step,state,result,details,CRCCount,seconds\n");
        for(uint32_t k=0U;k<recovery.trace_count;++k){const wrj_recovery_step_t *step=recovery.trace+k;
            fprintf(f,"%s,%u,%s,%s,\"%s\",%u,%.9g\n",candidate.candidate_id,k,step->state,step->result,step->details,step->crc_count,step->seconds);}fclose(f);}
    snprintf(path,sizeof(path),"%s/records.csv",argv[5]);f=fopen(path,"w");if(!f)goto finish;
    fprintf(f,"candidate,source,index,protocol,start0,nfft,cp,active,CFOHz,rotation,bytesHex,byteCount,receivedCRC,computedCRC,originalCRCPresent,crcValid,semanticComplete,phyMode,byteSource,fieldCount,repetitionCombined,TxAdd,AdvA,messageType,rawBits,receivedCRCBits\n");
    for(uint32_t k=0U;k<parse.count;++k){const wrj_rx_record_t *r=parse.records+k;
        fprintf(f,"%s,%s,%u,%s,%u,%u,%u,%u,%.17g,%.17g,",r->candidate,r->source,k,wrj_protocol_name(r->protocol),r->start0,r->nfft,r->cp,r->active,r->cfo_hz,r->rotation_rad);
        hex(f,r->bytes,r->byte_count);fprintf(f,",%u,%u,%u,%u,%u,%u,%s,%s,%u,%u,%u,",r->byte_count,r->received_crc,r->computed_crc,r->original_crc_present,
            r->crc_valid,r->semantic_complete,r->phy_mode,r->byte_source,r->field_count,r->repetition_combined,r->tx_add);hex(f,r->adv_a,6U);fprintf(f,",%u,",r->message_type);
        if(r->protocol==WRJ_PROTOCOL_REMOTEID)for(uint32_t bit=0U;bit<336U;++bit)fprintf(f,"%u",r->raw_bits[bit]);fputc(',',f);
        if(r->protocol==WRJ_PROTOCOL_REMOTEID)for(uint32_t bit=0U;bit<24U;++bit)fprintf(f,"%u",r->received_crc_bits[bit]);fputc('\n',f);}
    fclose(f);snprintf(path,sizeof(path),"%s/soft_list_evidence.csv",argv[5]);f=fopen(path,"w");if(!f)goto finish;
    fprintf(f,"Candidate,Record,DataBit,Confidence,OriginalDecision,CandidateDecision,OriginalReceivedCRC,ComputedCRC\n");
    for(uint32_t k=0U;k<parse.count;++k){const wrj_rx_record_t *r=parse.records+k;
        for(uint32_t b=0U;b<r->soft_bit_count;++b){const uint32_t bit=r->soft_bit_indices[b]-1U;
            const uint32_t decision=(r->bytes[bit/8U]>>(7U-bit%8U))&1U;
            fprintf(f,"%s,%u,%u,%.17g,%u,%u,%u,%u\n",r->candidate,k,bit+1U,r->soft_bit_confidence[b],1U-decision,decision,r->received_crc,r->computed_crc);}}
    fclose(f);snprintf(path,sizeof(path),"%s/fields.csv",argv[5]);f=fopen(path,"w");if(!f)goto finish;
    fprintf(f,"candidate,record,semantic,startByte,endByte,numericValue,rawHex,crcVerified,dataType,asciiValue,validationStatus\n");
    for(uint32_t k=0U;k<parse.count;++k){const wrj_rx_record_t *r=parse.records+k;
        for(uint32_t j=0U;j<r->field_count;++j){const wrj_rx_field_t *field=r->fields+j;
            fprintf(f,"%s,%u,%s,%u,%u,%.17g,",r->candidate,k,field->semantic,field->start_byte,field->end_byte,field->numeric_value);
            hex(f,r->bytes+(r->protocol==WRJ_PROTOCOL_REMOTEID?14U:0U)+field->start_byte-1U,field->end_byte-field->start_byte+1U);
            fprintf(f,",%u,%s,\"%s\",%s\n",r->crc_valid,field->data_type,field->ascii_value,field->validation_status);}}
    fclose(f);result=0;
finish:wrj_parse_result_release(&parse);m3_workspace_release(&workspace);free(actual);free(sync.input_baseband);free(sync.preprocessed_baseband);return result;
}
