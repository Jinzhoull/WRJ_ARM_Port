#ifndef M4_MODULE5_HANDOFF_H
#define M4_MODULE5_HANDOFF_H

#include "wrj_types.h"

#define M4_M5_MAX_PACKET_BYTES 2048U
#define M4_M5_MAX_PACKETS (WRJ_MAX_PACKETS + 1U)

typedef enum {
    M4_M5_SYMBOL_OBSERVATION = 0,
    M4_M5_BLE_PDU,
    M4_M5_LOGICAL_FRAME
} m4_m5_packet_kind_t;

typedef enum {
    M4_M5_CRC_NONE = 0,
    M4_M5_CRC24_BLE,
    M4_M5_CRC16_CCITT
} m4_m5_crc_kind_t;

typedef struct {
    m4_m5_packet_kind_t kind;
    m4_m5_crc_kind_t crc_kind;
    uint16_t observation_index;
    uint16_t frame_index;
    uint32_t start_sample;
    double observation_time_sec;
    uint16_t byte_count;
    uint8_t bytes[M4_M5_MAX_PACKET_BYTES];
    uint8_t crc_checked;
    uint8_t crc_valid;
    uint8_t polarity_inverted;
    int16_t message_type;
    int32_t sequence_number;
    uint8_t device_address[6];
    uint8_t device_address_valid;
    float confidence;
} m4_m5_packet_t;

typedef struct {
    char candidate_id[WRJ_ID_CAPACITY];
    char source_file[WRJ_ID_CAPACITY];
    double candidate_start_sec;
    double candidate_end_sec;
    char protocol[WRJ_TEXT_CAPACITY];
    char m3_status[WRJ_TEXT_CAPACITY];
    char m4_status[64];
    char byte_recovery_status[64];
    uint8_t real_iq_only;
    uint8_t parse_complete;
    uint32_t synced_frame_count;
    float sync_confidence;
    float cfo_hz;
    uint16_t packet_count;
    m4_m5_packet_t packets[M4_M5_MAX_PACKETS];
    uint16_t field_count;
    m4_field_t fields[WRJ_MAX_FIELDS];
    uint16_t crc_valid_count;
    char uas_id[32];
    char operator_id[32];
    double latitude_deg;
    double longitude_deg;
    double altitude_m;
    double speed_mps;
    double heading_deg;
    /* MATLAB Module4 observation records, kept distinct from physical
     * packet evidence for the next-stage parser. */
    uint8_t matlab_observation_count;
    uint8_t matlab_observation_length[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_bytes[WRJ_MATLAB_MAX_OBSERVATIONS][WRJ_MATLAB_MAX_FRAME_BYTES];
    uint8_t matlab_observation_labels[WRJ_MATLAB_MAX_OBSERVATIONS][WRJ_MATLAB_MAX_FRAME_BYTES];
    uint8_t matlab_observation_physical_index[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_state_index[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_message_type[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_sequence[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint32_t matlab_observation_timestamp[WRJ_MATLAB_MAX_OBSERVATIONS];
} m4_to_m5_handoff_t;

void m4_build_module5_handoff(const wrj_candidate_t *candidate,
                              const m3_result_t *m3, const m4_result_t *m4,
                              m4_to_m5_handoff_t *handoff);

#endif
