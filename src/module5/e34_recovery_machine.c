#include "wrj_recovery_machine.h"
#include <stdlib.h>
#include <time.h>
static void trace(wrj_recovery_result_t *r,const char *state,const char *status,const char *details,const wrj_parse_result_t *rx,clock_t start)
{
    if(r->trace_count>=WRJ_RECOVERY_MAX_TRACE)return;wrj_recovery_step_t *step=r->trace+r->trace_count++;
    snprintf(step->state,sizeof(step->state),"%s",state);snprintf(step->result,sizeof(step->result),"%s",status);
    snprintf(step->details,sizeof(step->details),"%s",details);step->crc_count=rx->count;step->seconds=(double)(clock()-start)/CLOCKS_PER_SEC;
}
static wrj_status_t merge_and_validate(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,
    wrj_parse_result_t *rx,wrj_parse_result_t *next,wrj_recovery_result_t *r,clock_t start)
{
    /* Reject provenance BEFORE deduplication, so a foreign duplicate cannot
     * disappear behind a valid local observation. */
    for(uint32_t k=0U;k<next->count;++k)if(strcmp(next->records[k].candidate,candidate->candidate_id)!=0 ||
        strcmp(next->records[k].source,candidate->source_file)!=0)return WRJ_ERR_ARGUMENT;
    for(uint32_t k=0U;k<next->count;++k){const wrj_rx_record_t *record=next->records+k;
        (void)wrj_rx_append(rx,record,record->protocol==WRJ_PROTOCOL_REMOTEID?UINT32_MAX:2U*WRJ_MAX(64U,record->nfft+record->cp));}
    rx->stages.cp_peaks+=next->stages.cp_peaks;rx->stages.sync_success+=next->stages.sync_success;
    rx->stages.cfo_success+=next->stages.cfo_success;rx->stages.symbol_success+=next->stages.symbol_success;
    rx->stages.frame_candidates+=next->stages.frame_candidates;rx->stages.crc_checked+=next->stages.crc_checked;
    rx->stages.crc_pass+=next->stages.crc_pass;rx->stages.semantic_parse+=next->stages.semantic_parse;
    rx->fft_calls+=next->fft_calls;rx->fft_cache_hits+=next->fft_cache_hits;
    rx->total_hypotheses+=next->total_hypotheses;rx->unique_hypotheses+=next->unique_hypotheses;
    rx->duplicates_skipped+=next->duplicates_skipped;
    clock_t gate_begin=clock();wrj_status_t code=wrj_module5_validate(candidate,sync->fs,rx,&r->proof);
    r->gate_seconds+=(double)(clock()-gate_begin)/CLOCKS_PER_SEC;
    trace(r,"REVALIDATE",r->proof.accepted?"PASS":"GATE_FAIL","Independent original CRC, semantics and candidate scope",rx,start);
    return code;
}
wrj_status_t wrj_recovery_machine(const wrj_candidate_t *candidate,const wrj_cf32_t *actual,uint32_t n,
    m3_workspace_t *workspace,wrj_sync_result_t *sync,wrj_parse_result_t *rx,wrj_recovery_result_t *r)
{
    if(!candidate||!actual||!workspace||!sync||!rx||!r||!n)return WRJ_ERR_ARGUMENT;memset(r,0,sizeof(*r));clock_t start=clock();
    clock_t gate_begin=clock();wrj_status_t code=wrj_module5_validate(candidate,sync->fs,rx,&r->proof);
    r->gate_seconds+=(double)(clock()-gate_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)return code;
    r->first_pass_accepted=r->proof.accepted;trace(r,"FIRST_PASS_CHECK",r->proof.accepted?"PASS":"FIRST_PASS_FAILED","Independent received evidence gate",rx,start);
    if(r->proof.accepted){snprintf(r->failure_owner,sizeof(r->failure_owner),"NONE");snprintf(r->failure_stage,sizeof(r->failure_stage),"NONE");
        snprintf(r->stop_reason,sizeof(r->stop_reason),"FIRST_PASS_VALID");goto finish;}
    clock_t diagnose_begin=clock();
    snprintf(r->failure_stage,sizeof(r->failure_stage),"%s",rx->complete?"MODULE5_GATE_FAIL":rx->failure_stage);
    snprintf(r->failure_owner,sizeof(r->failure_owner),"%s",rx->complete?"MODULE5_VALIDATION":!sync->sync_accepted&&!rx->stages.frame_candidates?"MODULE3_SYNC":"MODULE4_PHY");
    trace(r,"DIAGNOSE_FAILURE","DIAGNOSED",r->failure_stage,rx,start);
    r->diagnose_seconds=(double)(clock()-diagnose_begin)/CLOCKS_PER_SEC;
    if(rx->complete){snprintf(r->stop_reason,sizeof(r->stop_reason),"INDEPENDENT_EVIDENCE_REJECTED");goto finish;}
    wrj_parse_result_t next;memset(&next,0,sizeof(next));code=wrj_parse_result_init(&next);if(code!=WRJ_OK)return code;
    int remote=sync->sync.profile==WRJ_PROFILE_REMOTEID_BLE;
    if(remote){++r->protocol_calls;trace(r,"PROTOCOL_RECOVERY","EXECUTE","Bounded received PDU association",rx,start);
        clock_t protocol_begin=clock();(void)wrj_remote_associate(rx,sync->fs);
        r->protocol_seconds+=(double)(clock()-protocol_begin)/CLOCKS_PER_SEC;
        gate_begin=clock();code=wrj_module5_validate(candidate,sync->fs,rx,&r->proof);
        r->gate_seconds+=(double)(clock()-gate_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)goto release;
    }
    if(!r->proof.accepted && (remote || (!rx->stages.frame_candidates && !sync->sync_accepted))){
        r->m3_calls=1U;trace(r,"M3_DEEP_RETRY","EXECUTE",remote?"BLE actual AA/CFO/filter bank":"Bounded received sync/CFO retry",rx,start);
        snprintf(sync->receiver_family,sizeof(sync->receiver_family),"%s",rx->family);
        clock_t m3_begin=clock();code=wrj_module3_deep(candidate,actual,n,1U,workspace,sync);
        r->m3_deep_seconds+=(double)(clock()-m3_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)goto release;
        ++r->m4_calls;trace(r,"M4_FAST_AFTER_M3_RETRY","EXECUTE","Actual IQ with original CRC",rx,start);
        clock_t fast_begin=clock();code=wrj_module4_fast(candidate,sync,&next);
        r->m4_after_sync_seconds+=(double)(clock()-fast_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)goto release;
        code=merge_and_validate(candidate,sync,rx,&next,r,start);if(code!=WRJ_OK)goto release;
    }
    if(remote){snprintf(r->stop_reason,sizeof(r->stop_reason),"BLE_PHY_SESSION_EXHAUSTED");goto release;}
    for(uint32_t level=1U;level<=3U && !r->proof.accepted;++level){
        if(level==1U && rx->raw_fast_attempted){trace(r,"M4_CHEAP_PATH_ALREADY_EXECUTED","SKIP","FAST compensated/raw cheap paths exhausted",rx,start);continue;}
        if(r->m4_calls>=6U){snprintf(r->stop_reason,sizeof(r->stop_reason),"M4_MODEL_LIMIT");break;}
        next.count=0U;memset(&next.stages,0,sizeof(next.stages));next.event_count=0U;
        next.fft_calls=next.fft_cache_hits=next.total_hypotheses=next.unique_hypotheses=next.duplicates_skipped=0U;
        snprintf(next.family,sizeof(next.family),"%s",rx->family);++r->m4_calls;
        char details[64];snprintf(details,sizeof(details),"LEVEL_%u_RECEIVED_PHY_SPECIALIST",level);trace(r,"M4_DEEP_RETRY","EXECUTE",details,rx,start);
        clock_t deep_begin=clock();code=wrj_module4_deep(candidate,sync,level,&next);
        r->deep_seconds[level-1U]+=(double)(clock()-deep_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)goto release;
        code=merge_and_validate(candidate,sync,rx,&next,r,start);if(code!=WRJ_OK)goto release;
        if(level==2U && strcmp(rx->family,"OFDM")!=0 && !r->proof.accepted){snprintf(r->stop_reason,sizeof(r->stop_reason),"PROTOCOL_SPECIALIST_EXHAUSTED");break;}
        if(level==3U)snprintf(r->stop_reason,sizeof(r->stop_reason),"BOUNDED_DEEP_EXHAUSTED");
    }
release:wrj_parse_result_release(&next);if(code!=WRJ_OK)return code;
finish:
    if(r->proof.accepted && sync->sync.profile==WRJ_PROFILE_REMOTEID_BLE){uint32_t attempts=0U;
        clock_t protocol_begin=clock();code=wrj_remote_complete(candidate,sync,rx,&attempts);
        r->protocol_seconds+=(double)(clock()-protocol_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)return code;
        gate_begin=clock();code=wrj_module5_validate(candidate,sync->fs,rx,&r->proof);
        r->gate_seconds+=(double)(clock()-gate_begin)/CLOCKS_PER_SEC;if(code!=WRJ_OK)return code;
        trace(r,"REMOTEID_MESSAGE_COMPLETION","BUDGETED","Original CRC and unchanged received identity",rx,start);
    }
    r->success=r->proof.accepted;r->attempted=(uint8_t)(r->m3_calls+r->m4_calls+r->protocol_calls>0U);
    r->recovered=(uint8_t)(r->success&&!r->first_pass_accepted);if(r->recovered)snprintf(r->stop_reason,sizeof(r->stop_reason),"ORIGINAL_CRC_AND_INDEPENDENT_GATE");
    rx->complete=r->success;snprintf(rx->status,sizeof(rx->status),"%s",r->success?"COMPLETE":"PARTIAL");
    if(r->success)snprintf(rx->failure_stage,sizeof(rx->failure_stage),"NONE");
    trace(r,r->success?"SUCCESS":"TERMINAL_FAILED",r->success?"PASS":"FAIL",r->stop_reason,rx,start);r->seconds=(double)(clock()-start)/CLOCKS_PER_SEC;return WRJ_OK;
}
