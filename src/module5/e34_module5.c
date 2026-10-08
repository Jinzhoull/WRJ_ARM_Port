#include "wrj_recovery_machine.h"
wrj_status_t wrj_module5_validate(const wrj_candidate_t *candidate,double fs,
    wrj_parse_result_t *rx,wrj_m5_proof_t *proof)
{
    if(!candidate||!rx||!rx->records||!proof||!isfinite(fs)||fs<=0.0)return WRJ_ERR_ARGUMENT;
    memset(proof,0,sizeof(*proof));int remote=0,private_complete=0;
    for(uint32_t k=0U;k<rx->count;++k){wrj_rx_record_t *r=rx->records+k;
        if(strcmp(r->candidate,candidate->candidate_id)!=0 || strcmp(r->source,candidate->source_file)!=0){
            ++proof->rejected_scope;continue;}
        if(strcmp(r->byte_source,"REAL_IQ")!=0||!r->original_crc_present){++proof->rejected_crc;continue;}
        /* Recompute from original evidence; M4 cached status is not a proof. */
        int valid;
        if(r->protocol==WRJ_PROTOCOL_REMOTEID){remote=1;valid=wrj_remote_revalidate(r);}
        else valid=wrj_protocol_parse(r);
        if(!valid || !r->crc_valid){++proof->rejected_crc;continue;}
        ++proof->crc_valid;proof->semantic_complete+=r->semantic_complete;
        if(r->protocol!=WRJ_PROTOCOL_REMOTEID && r->semantic_complete)private_complete=1;
    }
    if(proof->rejected_scope || proof->rejected_crc){proof->accepted=0U;return WRJ_OK;}
    proof->accepted=(uint8_t)(remote?wrj_remote_associate(rx,fs):private_complete);
    return WRJ_OK;
}
