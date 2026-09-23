#ifndef M4_MODULE4_H
#define M4_MODULE4_H

#include "wrj_types.h"

typedef struct {
    uint32_t max_bytes;
    float minimum_symbol_confidence;
} m4_config_t;

typedef struct {
    wrj_cf32_t *filtered;
    float *fft_re;
    float *fft_im;
    float *metric;
    float *scratch;
    uint32_t max_samples;
    uint32_t fft_capacity;
    uint32_t incremental_bytes;
} m4_workspace_t;

typedef struct {
    uint32_t start_sample;
    uint8_t message_type;
    uint8_t pdu[39];
    uint8_t message[25];
    float confidence;
} m4_ble_packet_t;

void m4_default_config(m4_config_t *config);
wrj_status_t m4_workspace_bind(m4_workspace_t *workspace, wrj_cf32_t *filtered,
                               float *metric, float *scratch,
                               float *fft_re, float *fft_im, uint32_t max_samples,
                               uint32_t fft_capacity);
wrj_status_t m4_run(const wrj_candidate_t *candidate, const m3_result_t *m3_result,
                    const wrj_cf32_t *compensated_iq, uint32_t sample_count,
                    const m4_config_t *config, m4_workspace_t *workspace,
                    m4_result_t *result);
wrj_status_t m4_recover_profile_bytes(const wrj_cf32_t *iq, uint32_t count,
                                      float sample_rate_hz, const m3_result_t *m3,
                                      const m4_config_t *config,
                                      m4_workspace_t *workspace, m4_result_t *result);
wrj_status_t m4_recover_blind_bytes(const wrj_cf32_t *iq, uint32_t count,
                                    const m3_result_t *m3, const m4_config_t *config,
                                    m4_workspace_t *workspace, m4_result_t *result);
wrj_status_t m4_recover_remoteid(const wrj_cf32_t *iq, uint32_t count, float sample_rate_hz,
                                 const m3_result_t *m3, m4_workspace_t *workspace,
                                 m4_result_t *result);
void m4_parse_structural_fields(m4_result_t *result);
void m4_parse_remoteid_messages(const m4_ble_packet_t *packets, uint32_t count,
                                m4_result_t *result);
uint32_t m4_crc24_ble(const uint8_t *bits, uint32_t bit_count);
void m4_ble_whiten(uint8_t *bits, uint32_t bit_count, uint8_t channel);
uint16_t m4_crc16_ccitt(const uint8_t *bytes, uint32_t count);
uint16_t m4_pack_bits_msb(const uint8_t *bits, uint32_t bit_count,
                          uint8_t *bytes, uint16_t capacity);
void m4_qpsk_decide(float re, float im, uint8_t *bit_i, uint8_t *bit_q,
                    float *confidence_i, float *confidence_q);

#endif
