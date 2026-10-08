#ifndef WRJ_BLE_E34_H
#define WRJ_BLE_E34_H
#include "m3_module3.h"
/* v22 actual-IQ PDU decoder. Acquisition peaks remain a separate collection. */
typedef struct {
    uint32_t start,crc;
    double score,structure,rank,timing,soft[336],white_soft[360];
    uint8_t raw[336],pdu[39],valid,found,pdu_found,recovered;
} wrj_ble_packet_evidence_t;
wrj_status_t wrj_ble_decode_e34(const wrj_cf32_t *iq,uint32_t count,double fs,m3_result_t *result);
wrj_status_t wrj_ble_decode_e34_ex(const wrj_cf32_t *iq,uint32_t count,double fs,m3_result_t *result,
    wrj_ble_packet_evidence_t *packets,uint32_t capacity,uint32_t *packet_count);
wrj_status_t wrj_ble_filter_decode_e34(const wrj_cf32_t *iq,uint32_t count,double fs,
    double center_hz,double width_hz,uint32_t output_sps,m3_result_t *result);
wrj_status_t wrj_ble_filter_decode_e34_ex(const wrj_cf32_t *iq,uint32_t count,double fs,
    double center_hz,double width_hz,uint32_t output_sps,m3_result_t *result,
    wrj_ble_packet_evidence_t *packets,uint32_t capacity,uint32_t *packet_count,int local_windows);
void wrj_ble_gap_recover_e34(wrj_ble_packet_evidence_t *packets,uint32_t count,uint32_t sps,m3_result_t *result);
#endif
