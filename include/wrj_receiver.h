#ifndef WRJ_RECEIVER_H
#define WRJ_RECEIVER_H
#include "wrj_e34_module3.h"
#include <complex.h>

#define WRJ_RX_MAX_BYTES 4109U
#define WRJ_RX_MAX_RECORDS 64U
#define WRJ_RX_MAX_FIELDS 80U
typedef enum {
    WRJ_PROTOCOL_AUTO=0,WRJ_PROTOCOL_DJI_CONTROL,WRJ_PROTOCOL_DJI_WIDEBAND,
    WRJ_PROTOCOL_DRONEID,WRJ_PROTOCOL_AUTEL_CONTROL,WRJ_PROTOCOL_AUTEL_WIDEBAND,
    WRJ_PROTOCOL_UNKNOWN,WRJ_PROTOCOL_REMOTEID
} wrj_protocol_t;
typedef struct {
    char semantic[64],data_type[32],ascii_value[64],validation_status[32];
    uint16_t start_byte,end_byte;double numeric_value;
} wrj_rx_field_t;
typedef struct {
    char candidate[WRJ_TEXT_CAPACITY],source[WRJ_PATH_CAPACITY];
    wrj_protocol_t protocol;
    uint32_t start0,bit_start0,nfft,cp,active,byte_count;
    double cfo_hz,rotation_rad;
    uint8_t bytes[WRJ_RX_MAX_BYTES];
    uint8_t frame_valid,crc_valid,semantic_complete,repetition_combined,window_repaired;
    uint8_t received_crc_bits[24],raw_bits[336];
    uint8_t tx_add,adv_a[6],message_type,original_crc_present;
    uint32_t received_crc,computed_crc;
    uint32_t soft_bit_count,soft_bit_indices[4];double soft_bit_confidence[4];
    char phy_mode[64],byte_source[16];
    uint32_t field_count;wrj_rx_field_t fields[WRJ_RX_MAX_FIELDS];
} wrj_rx_record_t;
typedef struct {
    uint32_t cp_peaks,sync_success,cfo_success,symbol_success,frame_candidates,
        crc_checked,crc_pass,semantic_parse;
} wrj_rx_stages_t;
typedef struct {
    uint32_t bit_start0;double rotation;uint8_t header[11];
} wrj_scan_event_t;
typedef struct {
    wrj_rx_record_t *records;uint32_t count,capacity;
    uint64_t fft_calls,fft_cache_hits,total_hypotheses,unique_hypotheses,duplicates_skipped;
    wrj_rx_stages_t stages;
    char family[16],status[32],failure_stage[32],association_mode[40];
    uint8_t complete,raw_fast_attempted;double seconds;
    wrj_scan_event_t events[16];uint32_t event_count;
    wrj_scan_event_t incomplete_events[96];uint32_t incomplete_count;
} wrj_parse_result_t;

const char *wrj_protocol_name(wrj_protocol_t protocol);
uint16_t wrj_crc16_received(const uint8_t *bytes,uint32_t count);
uint32_t wrj_crc24_received(const uint8_t *bytes,uint32_t count);
int wrj_protocol_parse(wrj_rx_record_t *record);
wrj_status_t wrj_parse_result_init(wrj_parse_result_t *result);
void wrj_parse_result_release(wrj_parse_result_t *result);
int wrj_rx_append(wrj_parse_result_t *result,const wrj_rx_record_t *record,uint32_t tolerance);
int wrj_scan_bits(const uint8_t *bits,uint32_t count,wrj_protocol_t protocol,
    const wrj_rx_record_t *context,wrj_parse_result_t *result);
int wrj_symbols_to_records(const double complex *symbols,uint32_t count,
    const wrj_rx_record_t *context,wrj_parse_result_t *result);
int wrj_symbols_to_records_soft(const double complex *symbols,uint32_t count,
    const wrj_rx_record_t *context,wrj_parse_result_t *result);
void wrj_soft_data_budget_reset(uint32_t checks);
int wrj_received_window_end(uint32_t samples,uint32_t current_end0,
    const wrj_rx_record_t *context,const uint8_t header[11],uint32_t bit_start0,uint32_t *end0);
int wrj_remote_revalidate(wrj_rx_record_t *record);
int wrj_remote_associate(wrj_parse_result_t *result,double fs);
wrj_status_t wrj_remote_complete(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,
    wrj_parse_result_t *result,uint32_t *attempts);
wrj_status_t wrj_module4_fast(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,
    wrj_parse_result_t *result);
wrj_status_t wrj_module4_deep(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,
    uint32_t level,wrj_parse_result_t *result);
#endif
