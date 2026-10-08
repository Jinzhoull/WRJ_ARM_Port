#ifndef WRJ_TYPES_H
#define WRJ_TYPES_H

#include "wrj_common.h"

#define WRJ_ID_CAPACITY 96
#define WRJ_TEXT_CAPACITY 160
#define WRJ_PATH_CAPACITY 512
#define WRJ_MAX_FRAMES 64
#define WRJ_MAX_BYTES 512
#define WRJ_MAX_FIELDS 24
#define WRJ_MAX_PACKETS 32
#define WRJ_MATLAB_MAX_OBSERVATIONS 20
#define WRJ_MATLAB_MAX_FRAME_BYTES 64

typedef struct {
    float re;
    float im;
} wrj_cf32_t;

typedef enum {
    WRJ_PROFILE_UNKNOWN = 0,
    WRJ_PROFILE_DJI_WIDEBAND_CP,
    WRJ_PROFILE_AUTEL_WIDEBAND_CP,
    WRJ_PROFILE_DRONEID_ZC,
    WRJ_PROFILE_REMOTEID_BLE,
    WRJ_PROFILE_CONTROL_BURST,
    WRJ_PROFILE_AUTEL_CONTROL_CP,
    WRJ_PROFILE_DJI_CONTROL_BLIND
} wrj_profile_kind_t;

typedef struct {
    char candidate_id[WRJ_ID_CAPACITY];
    char source_file[WRJ_ID_CAPACITY];
    char predicted_link_type[WRJ_TEXT_CAPACITY];
    char predicted_protocol_family[WRJ_TEXT_CAPACITY];
    char sync_strategy[WRJ_TEXT_CAPACITY];
    char parser_template_id[WRJ_TEXT_CAPACITY];
    char candidate_iq_artifact[WRJ_PATH_CAPACITY];
    float sample_rate_hz;
    double candidate_start_sec;
    double candidate_end_sec;
    float center_frequency_hz;
    float candidate_center_offset_hz;
    float bandwidth_hz;
    float coarse_cfo_hz;
    float frame_period_estimate_sec;
    float frame_structure_score;
    float preamble_repeat_score;
    float recommended_guard_sec;
} wrj_candidate_t;

typedef struct {
    uint32_t max_samples;
    uint32_t fft_size;
    uint32_t max_frames;
    float sync_accept_threshold;
    float spectrum_search_fraction;
    uint8_t enable_integer_cfo_search;
    /* Legacy defaults stay enabled; E34 FAST explicitly disables these. */
    uint8_t enable_profile_retry;
    uint8_t enable_deep_receiver;
} m3_config_t;

typedef struct {
    double correlation_re,correlation_im,angle_rad,cycles_per_sample;
    uint32_t peak_count,nfft,cp_samples;
} m3_cp_probe_t;

typedef struct {
    double estimated_cfo_hz;
    m3_cp_probe_t cp_probe[3];
    double stage1_cfo_hz,stage2_cfo_hz,combined_cfo_hz,applied_cfo_hz,residual_probe_hz;
    uint32_t phase_origin;
    double spectral_correction_hz;
    double fractional_cfo_hz;
    float integer_cfo_offset_hz;
    int32_t integer_cfo_index;
    float residual_cfo_hz;
    float sync_confidence;
    float peak_metric;
    uint32_t frame_start_samples_0based[WRJ_MAX_FRAMES];
    uint32_t num_frames;
    uint32_t frame_length_samples;
    float frame_confidence[WRJ_MAX_FRAMES];
    float droneid_zc_peak_position;
    float original_frame_start;
    float corrected_frame_start;
    float frame_offset_correction;
    float offset_search_score;
    uint8_t timing_alignment_improved;
    float estimated_sfo_ppm;
    float frame_drift_samples;
    float timing_slope;
    uint8_t sfo_correction_applied;
    float access_address_confidence;
    float ble_sync_confidence;
    float symbol_timing_offset;
    uint32_t crc_attempt_count;
    uint32_t crc_success_count;
    uint8_t ble_soft_recovery_enabled;
    uint16_t ble_soft_recovered_count;
    uint16_t ble_verified_packet_count;
    uint8_t ble_verified_pdu[WRJ_MAX_PACKETS][39];
    uint8_t ble_verified_raw_bits[WRJ_MAX_PACKETS][336];
    char ble_origin_candidate[WRJ_TEXT_CAPACITY];
    char ble_origin_source[WRJ_PATH_CAPACITY];
    uint32_t ble_verified_crc[WRJ_MAX_PACKETS];
    uint32_t ble_verified_start[WRJ_MAX_PACKETS];
    float ble_verified_confidence[WRJ_MAX_PACKETS];
    uint32_t cfo_candidate_count;
    float selected_cfo_score;
    uint32_t profiles_tried;
    uint8_t profile_changed_by_evidence;
    char sync_method[WRJ_TEXT_CAPACITY];
    char recommended_profile[WRJ_TEXT_CAPACITY];
    char attempted_profiles[WRJ_PATH_CAPACITY];
    wrj_profile_kind_t profile;
    char profile_name[WRJ_TEXT_CAPACITY];
    char status[WRJ_TEXT_CAPACITY];
} m3_result_t;

typedef enum {
    M4_STATUS_NOT_RUN = 0,
    M4_STATUS_PARTIAL,
    M4_STATUS_NO_BYTES,
    M4_STATUS_DEFERRED_REMOTEID,
    M4_STATUS_UNSUPPORTED_PROFILE,
    M4_STATUS_BYTE_RECOVERY_FAILED,
    M4_STATUS_CRC_FAILED,
    M4_STATUS_PARSED
} m4_status_t;

typedef enum {
    M4_BYTE_SOURCE_NONE = 0,
    M4_BYTE_SOURCE_REAL_IQ,
    M4_BYTE_SOURCE_SYNTHETIC_TEST
} m4_byte_source_t;

typedef struct {
    uint16_t offset;
    uint16_t length;
    uint16_t field_id;
    char field_type[32];
    char semantic[32];
    double numeric_value;
    char string_value[64];
    float confidence;
} m4_field_t;

/* This describes a CRC-verified engineering proxy frame, never a claimed
 * vendor-private DJI packet. Unknown fields are omitted from this struct. */
typedef struct {
    uint8_t attempted;
    uint8_t complete;
    uint8_t polarity_inverted;
    uint8_t frame_id;
    uint8_t message_type;
    uint8_t state_code;
    uint16_t device_id;
    uint16_t timestamp_counter;
    uint16_t payload_length;
    uint16_t source_observation;
    uint16_t byte_offset;
    uint16_t observation_count;
    uint16_t max_observation_bytes;
    uint16_t header_candidates;
    uint16_t complete_candidates;
    uint16_t crc_valid_count;
    uint32_t assembled_byte_count;
    uint16_t assembled_observation_count;
    uint16_t crc16_checked;
    uint16_t crc16_passed;
    uint16_t assembly_start_observation;
    uint16_t assembly_end_observation;
    uint8_t assembly_continuous;
    uint32_t assembly_offsets[WRJ_MAX_PACKETS];
    char assembly_method[40];
    char polarity_mode[32];
    char status[48];
} m4_dji_wideband_parse_t;

typedef struct {
    uint8_t bytes[WRJ_MAX_BYTES];
    float byte_confidence[WRJ_MAX_BYTES];
    uint16_t byte_count;
    char protocol_type[WRJ_TEXT_CAPACITY];
    m4_field_t fields[WRJ_MAX_FIELDS];
    uint16_t field_count;
    float byte_recovery_confidence;
    float bit_error_estimate;
    float semantic_confidence;
    uint16_t packet_count;
    uint16_t packet_lengths[WRJ_MAX_PACKETS];
    uint8_t packet_bytes[WRJ_MAX_PACKETS][WRJ_MAX_BYTES];
    uint32_t packet_source_start[WRJ_MAX_PACKETS];
    float packet_confidence[WRJ_MAX_PACKETS];
    uint8_t packet_full_symbol[WRJ_MAX_PACKETS];
    uint16_t crc_valid_count;
    uint8_t crc_checked;
    uint8_t crc_passed;
    uint8_t parse_complete;
    m4_byte_source_t byte_source;
    char byte_recovery_status[64];
    char parse_status[64];
    char diagnostics[WRJ_TEXT_CAPACITY];
    char uas_id[32];
    char operator_id[32];
    double latitude_deg;
    double longitude_deg;
    double altitude_m;
    double speed_mps;
    double heading_deg;
    uint8_t message_types_mask;
    char decoder_method[WRJ_TEXT_CAPACITY];
    char crc_candidate_status[WRJ_TEXT_CAPACITY];
    m4_dji_wideband_parse_t dji_wideband;
    uint16_t verified_frame_length;
    uint8_t verified_frame_bytes[2048];
    m4_status_t status;
    char status_text[WRJ_TEXT_CAPACITY];
    uint8_t matlab_observation_count;
    uint8_t matlab_observation_length[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_bytes[WRJ_MATLAB_MAX_OBSERVATIONS][WRJ_MATLAB_MAX_FRAME_BYTES];
    uint8_t matlab_observation_labels[WRJ_MATLAB_MAX_OBSERVATIONS][WRJ_MATLAB_MAX_FRAME_BYTES];
    uint8_t matlab_observation_physical_index[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_state_index[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_message_type[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint8_t matlab_observation_sequence[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint32_t matlab_observation_timestamp[WRJ_MATLAB_MAX_OBSERVATIONS];
} m4_result_t;

#endif
