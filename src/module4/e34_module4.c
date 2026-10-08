#include "wrj_phy.h"
#include "wrj_ble_e34.h"
#include <stdlib.h>
#include <time.h>
static wrj_rx_record_t context_for(const wrj_candidate_t *candidate)
{
    wrj_rx_record_t r;memset(&r,0,sizeof(r));r.protocol=WRJ_PROTOCOL_AUTO;
    snprintf(r.candidate,sizeof(r.candidate),"%s",candidate->candidate_id);snprintf(r.source,sizeof(r.source),"%s",candidate->source_file);
    snprintf(r.byte_source,sizeof(r.byte_source),"REAL_IQ");snprintf(r.phy_mode,sizeof(r.phy_mode),"NOT_APPLICABLE");return r;
}
static double carrier_line_score(const double complex *x,uint32_t n)
{
    const uint32_t size=262144U;double complex *fourth=calloc(size,sizeof(*fourth));if(!fourth)return 0.0;
    for(uint32_t k=0U;k<WRJ_MIN(n,size);++k){const uint32_t pos=n>size?(uint32_t)llround((double)k*(n-1U)/(size-1U)):k;
        fourth[k]=x[pos]*x[pos]*x[pos]*x[pos];}
    (void)wrj_dft_complex(fourth,size,0);double total=0.0,peak=0.0;
    for(uint32_t k=0U;k<size;++k){const double p=pow(cabs(fourth[k]),2.0);total+=p;peak=WRJ_MAX(p,peak);}free(fourth);return peak/(total+2.2204460492503131e-16);
}
static int receive_private(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,const wrj_cf32_t *iq,wrj_parse_result_t *out,uint32_t level)
{
    const uint32_t n=sync->sample_count;double complex *x=wrj_iq64_normalized(iq,n);if(!x)return 0;
    wrj_rx_record_t context=context_for(candidate);const uint32_t before=out->count;
    if(level>1U && out->family[0]){
        if(strcmp(out->family,"DRONE")==0)(void)wrj_drone_receive(x,n,sync->fs,&context,out,1);
        else if(strcmp(out->family,"SC")==0)(void)wrj_sc_receive(x,n,sync->fs,&context,out);
        else if(strcmp(out->family,"OPENSET")==0)(void)wrj_openset_receive(x,n,sync->fs,&context,out,1U);
        else (void)wrj_ofdm_receive(x,n,sync->fs,&context,out,level,0);
        free(x);return (int)(out->count-before);
    }
    if(sync->sync.profile==WRJ_PROFILE_UNKNOWN || sync->sync.profile==WRJ_PROFILE_DJI_CONTROL_BLIND){
        if(wrj_openset_receive(x,n,sync->fs,&context,out,2U)>0){snprintf(out->family,sizeof(out->family),"OPENSET");free(x);return (int)(out->count-before);}}
    if(sync->sync.profile==WRJ_PROFILE_DRONEID_ZC || wrj_drone_looks(x,n,sync->fs)){snprintf(out->family,sizeof(out->family),"DRONE");
        (void)wrj_drone_receive(x,n,sync->fs,&context,out,level>1U);}
    else {
        const uint32_t headers_before=out->stages.frame_candidates,sync_before=out->stages.sync_success;
        snprintf(out->family,sizeof(out->family),"OFDM");(void)wrj_ofdm_receive(x,n,sync->fs,&context,out,level,0);
        const double line=carrier_line_score(x,n);const int sc=line>2.5e-4 || (out->stages.sync_success==sync_before && line>1e-4);
        if(out->count==before && out->stages.frame_candidates==headers_before && sc){
            if(wrj_sc_receive(x,n,sync->fs,&context,out)>0)snprintf(out->family,sizeof(out->family),"SC");}
        if(out->count==before)(void)wrj_ofdm_receive(x,n,sync->fs,&context,out,3U,1);
    }
    free(x);return (int)(out->count-before);
}
static void summarize(wrj_parse_result_t *out,int remote)
{
    if(!remote){out->complete=0U;for(uint32_t k=0U;k<out->count;++k)out->complete|=out->records[k].semantic_complete;}
    snprintf(out->status,sizeof(out->status),"%s",out->complete?"COMPLETE":"PARTIAL");
    snprintf(out->failure_stage,sizeof(out->failure_stage),"%s",out->complete?"NONE":
        out->stages.crc_pass?"SEMANTIC_FAIL":out->stages.crc_checked?"CRC_FAIL":out->stages.frame_candidates?"BAD_FRAME_ASSEMBLY":
        out->stages.symbol_success?"BAD_SYMBOLS":out->stages.cfo_success?"BAD_TIMING":out->stages.sync_success?"BAD_CFO":"BAD_SYNC");
}
wrj_status_t wrj_module4_fast(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,wrj_parse_result_t *out)
{
    if(!candidate || !sync || !out || !out->records || !sync->sample_count || !sync->compensated_iq)return WRJ_ERR_ARGUMENT;
    const clock_t start=clock();const int remote=sync->sync.profile==WRJ_PROFILE_REMOTEID_BLE;
    if(remote){snprintf(out->family,sizeof(out->family),"BLE");(void)wrj_remote_receive(candidate,sync,out,0);
        if(!out->complete && sync->input_baseband){wrj_sync_result_t *hint=calloc(1U,sizeof(*hint));
            if(!hint)return WRJ_ERR_MEMORY;hint->fs=sync->fs;
            snprintf(hint->sync.ble_origin_candidate,sizeof(hint->sync.ble_origin_candidate),"%s",candidate->candidate_id);
            snprintf(hint->sync.ble_origin_source,sizeof(hint->sync.ble_origin_source),"%s",candidate->source_file);
            wrj_status_t code=wrj_ble_filter_decode_e34(sync->input_baseband,sync->sample_count,sync->fs,candidate->coarse_cfo_hz,.9e6,16U,&hint->sync);
            if(code==WRJ_OK){
                wrj_parse_result_t pool;memset(&pool,0,sizeof(pool));code=wrj_parse_result_init(&pool);
                if(code==WRJ_OK){
                    pool.count=out->count;pool.stages=out->stages;
                    memcpy(pool.records,out->records,out->count*sizeof(*out->records));
                    (void)wrj_remote_receive(candidate,hint,&pool,0);
                    /* Frozen M4 accepts this one-CFO fallback pool only
                     * when it completes the independently checked core.
                     * Incomplete fallback PDUs belong to DEEP, not FAST. */
                    out->stages=pool.stages;
                    if(pool.complete){out->count=pool.count;out->complete=1U;
                        memcpy(out->records,pool.records,pool.count*sizeof(*out->records));
                        snprintf(out->association_mode,sizeof(out->association_mode),"%s",pool.association_mode);}
                    wrj_parse_result_release(&pool);
                }
            }
            free(hint);if(code!=WRJ_OK)return code;
        }
    }
    else {
        (void)receive_private(candidate,sync,sync->compensated_iq,out,1U);
        if(!out->count && strcmp(out->family,"DRONE")!=0 && sync->input_baseband){
            out->raw_fast_attempted=1U;(void)receive_private(candidate,sync,sync->input_baseband,out,1U);}
    }
    if(remote && out->complete){uint32_t attempts=0U;wrj_status_t code=wrj_remote_complete(candidate,sync,out,&attempts);if(code!=WRJ_OK)return code;}
    summarize(out,remote);out->seconds=(double)(clock()-start)/CLOCKS_PER_SEC;return WRJ_OK;
}
wrj_status_t wrj_module4_deep(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,uint32_t level,wrj_parse_result_t *out)
{
    if(!candidate || !sync || !out || !out->records || level<1U || level>3U)return WRJ_ERR_ARGUMENT;
    if(level==1U)return wrj_module4_fast(candidate,sync,out);
    const clock_t start=clock();const int remote=sync->sync.profile==WRJ_PROFILE_REMOTEID_BLE;
    if(remote)(void)wrj_remote_receive(candidate,sync,out,1);
    else (void)receive_private(candidate,sync,sync->input_baseband?sync->input_baseband:sync->compensated_iq,out,level);
    summarize(out,remote);out->seconds=(double)(clock()-start)/CLOCKS_PER_SEC;return WRJ_OK;
}
