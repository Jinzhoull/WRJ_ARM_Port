#ifndef WRJ_RECOVERY_MACHINE_H
#define WRJ_RECOVERY_MACHINE_H
#include "wrj_receiver.h"
#define WRJ_RECOVERY_MAX_TRACE 32U
typedef struct {uint8_t accepted;uint32_t crc_valid,semantic_complete,rejected_scope,rejected_crc;} wrj_m5_proof_t;
typedef struct {char state[48],result[24],details[120];uint32_t crc_count;double seconds;} wrj_recovery_step_t;
typedef struct {
    uint8_t first_pass_accepted,attempted,recovered,success;
    uint32_t m3_calls,m4_calls,protocol_calls,trace_count;
    char failure_owner[32],failure_stage[48],stop_reason[64];
    wrj_recovery_step_t trace[WRJ_RECOVERY_MAX_TRACE];wrj_m5_proof_t proof;
    double seconds;
    double diagnose_seconds,m3_deep_seconds,m4_after_sync_seconds,deep_seconds[3],gate_seconds,protocol_seconds;
} wrj_recovery_result_t;
wrj_status_t wrj_module5_validate(const wrj_candidate_t *candidate,double fs,
    wrj_parse_result_t *records,wrj_m5_proof_t *proof);
wrj_status_t wrj_recovery_machine(const wrj_candidate_t *candidate,const wrj_cf32_t *actual,
    uint32_t count,m3_workspace_t *workspace,wrj_sync_result_t *sync,
    wrj_parse_result_t *records,wrj_recovery_result_t *result);
#endif
