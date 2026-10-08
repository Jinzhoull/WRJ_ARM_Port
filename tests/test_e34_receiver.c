#include "wrj_recovery_machine.h"
#include <stdlib.h>

#define CHECK(value) do { if(!(value)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); return 1; } } while(0)

static void private_record(wrj_rx_record_t *record,const wrj_candidate_t *candidate)
{
    static const uint8_t header[11]={0xd1U,0xc2U,0x21U,0U,0U,12U,0U,0U,1U,0U,2U};
    memset(record,0,sizeof(*record));
    snprintf(record->candidate,sizeof(record->candidate),"%s",candidate->candidate_id);
    snprintf(record->source,sizeof(record->source),"%s",candidate->source_file);
    snprintf(record->byte_source,sizeof(record->byte_source),"REAL_IQ");
    record->protocol=WRJ_PROTOCOL_DJI_CONTROL;record->byte_count=25U;record->original_crc_present=1U;
    memcpy(record->bytes,header,11U);record->bytes[11]=0xc2U;record->bytes[13]=1U;
    uint16_t crc=wrj_crc16_received(record->bytes,23U);
    record->bytes[23]=(uint8_t)(crc>>8U);record->bytes[24]=(uint8_t)crc;
}
static void remote_record(wrj_rx_record_t *record,const wrj_candidate_t *candidate,uint8_t type)
{
    memset(record,0,sizeof(*record));
    snprintf(record->candidate,sizeof(record->candidate),"%s",candidate->candidate_id);
    snprintf(record->source,sizeof(record->source),"%s",candidate->source_file);
    snprintf(record->byte_source,sizeof(record->byte_source),"REAL_IQ");
    record->protocol=WRJ_PROTOCOL_REMOTEID;record->byte_count=39U;record->original_crc_present=1U;
    record->bytes[0]=0x42U;record->bytes[1]=37U;record->tx_add=1U;
    record->bytes[8]=30U;record->bytes[9]=0x16U;record->bytes[10]=0xfaU;record->bytes[11]=0xffU;record->bytes[12]=13U;
    record->bytes[14]=16U*type+2U;
    if(type==0U){record->bytes[15]=0x12U;memcpy(record->bytes+16U,"UNIT_ID",7U);}
    else {record->bytes[29]=0xd0U;record->bytes[30]=0x07U;}
    const uint32_t crc=wrj_crc24_received(record->bytes,39U);
    for(uint32_t k=0U;k<312U;++k)record->raw_bits[k]=(record->bytes[k/8U]>>(k%8U))&1U;
    for(uint32_t k=0U;k<24U;++k)record->received_crc_bits[k]=record->raw_bits[312U+k]=(crc>>(23U-k))&1U;
}
int main(void)
{
    wrj_candidate_t candidate;memset(&candidate,0,sizeof(candidate));
    snprintf(candidate.candidate_id,sizeof(candidate.candidate_id),"unit_candidate");
    snprintf(candidate.source_file,sizeof(candidate.source_file),"unit_source");candidate.sample_rate_hz=20e6f;
    wrj_parse_result_t received;memset(&received,0,sizeof(received));CHECK(wrj_parse_result_init(&received)==WRJ_OK);
    wrj_m5_proof_t proof;
    CHECK(wrj_crc16_received((const uint8_t *)"123456789",9U)==0x29b1U);
    private_record(received.records,&candidate);received.count=1U;
    CHECK(wrj_module5_validate(&candidate,20e6,&received,&proof)==WRJ_OK && proof.accepted && proof.crc_valid==1U);
    CHECK(received.records[0].field_count==14U);
    /* Cached COMPLETE/CRC flags cannot conceal a changed received byte. */
    received.complete=1U;received.records[0].bytes[19]^=1U;received.records[0].crc_valid=1U;
    CHECK(wrj_module5_validate(&candidate,20e6,&received,&proof)==WRJ_OK && !proof.accepted && proof.rejected_crc==1U);
    private_record(received.records,&candidate);received.records[1]=received.records[0];received.count=2U;
    snprintf(received.records[1].candidate,sizeof(received.records[1].candidate),"foreign_candidate");
    CHECK(wrj_module5_validate(&candidate,20e6,&received,&proof)==WRJ_OK && !proof.accepted && proof.rejected_scope==1U);
    received.count=1U;private_record(received.records,&candidate);received.records[0].original_crc_present=0U;
    CHECK(wrj_module5_validate(&candidate,20e6,&received,&proof)==WRJ_OK && !proof.accepted);
    remote_record(received.records,&candidate,0U);remote_record(received.records+1U,&candidate,1U);received.count=2U;
    CHECK(wrj_module5_validate(&candidate,20e6,&received,&proof)==WRJ_OK && proof.accepted && proof.crc_valid==2U);
    CHECK(strcmp(received.records[0].fields[2].ascii_value,"UNIT_ID")==0);
    received.records[1].received_crc_bits[7]^=1U;
    CHECK(wrj_module5_validate(&candidate,20e6,&received,&proof)==WRJ_OK && !proof.accepted && proof.rejected_crc==1U);
    received.count=1U;private_record(received.records,&candidate);
    wrj_sync_result_t sync;memset(&sync,0,sizeof(sync));sync.fs=20e6;
    wrj_cf32_t actual[1]={{0.0f,0.0f}};m3_workspace_t workspace;memset(&workspace,0,sizeof(workspace));wrj_recovery_result_t recovery;
    CHECK(wrj_recovery_machine(&candidate,actual,1U,&workspace,&sync,&received,&recovery)==WRJ_OK);
    CHECK(recovery.success && recovery.first_pass_accepted && !recovery.attempted && recovery.m3_calls==0U && recovery.m4_calls==0U);
    CHECK(strcmp(recovery.trace[0].state,"FIRST_PASS_CHECK")==0 && strcmp(recovery.trace[recovery.trace_count-1U].state,"SUCCESS")==0);
    wrj_parse_result_release(&received);puts("E34 original CRC, fields, scope and independent recovery gate passed");return 0;
}
