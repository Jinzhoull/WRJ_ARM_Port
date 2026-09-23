#ifndef M3_MODULE3_H
#define M3_MODULE3_H

#include "wrj_types.h"

typedef struct {
    uint32_t index;
    float value;
} m3_peak_t;

typedef struct {
    uint32_t max_samples;
    uint32_t fft_size;
    wrj_cf32_t *baseband;
    wrj_cf32_t *compensated;
    float *fft_re;
    float *fft_im;
    float *power;
    float *metric;
    float *scratch;
    m3_peak_t *peak_candidates;
    m3_peak_t *peak_selected;
    uint8_t *flags;
    uint32_t peak_capacity;
    uint32_t selected_peak_capacity;
    uint32_t spectrum_length;
    uint32_t spectrum_blocks;
    float *spectrum_block_power;
    float *spectrum_block_energy;
    float *spectrum_smooth;
    float *spectrum_weight;
    float *spectrum_aux;
} m3_workspace_t;

void m3_default_config(m3_config_t *config);
wrj_status_t m3_workspace_init(m3_workspace_t *workspace, const m3_config_t *config);
void m3_workspace_release(m3_workspace_t *workspace);
size_t m3_workspace_bytes(const m3_workspace_t *workspace);

wrj_profile_kind_t m3_select_profile(const wrj_candidate_t *candidate,
                                     uint32_t *nfft, uint32_t *cp_samples);
wrj_status_t m3_estimate_spectral_center(const wrj_cf32_t *iq, uint32_t count,
                                         float sample_rate_hz, float bandwidth_hz,
                                         m3_workspace_t *workspace, float *offset_hz);
void m3_cfo_compensate(const wrj_cf32_t *input, wrj_cf32_t *output, uint32_t count,
                       float sample_rate_hz, float correction_hz);
wrj_status_t m3_bandlimit_fir(wrj_cf32_t *iq, uint32_t count, float sample_rate_hz,
                              float bandwidth_hz, m3_workspace_t *workspace);
wrj_status_t m3_fft_forward_radix2(float *re, float *im, uint32_t length);
wrj_status_t m3_integer_cfo_search(const wrj_cf32_t *iq, uint32_t count,
                                   float sample_rate_hz, float subcarrier_spacing_hz,
                                   m3_workspace_t *workspace, int32_t *selected_index,
                                   float *selected_offset_hz);
wrj_status_t m3_cp_synchronize(const wrj_cf32_t *iq, uint32_t count,
                               uint32_t nfft, uint32_t cp_samples,
                               uint32_t max_frames, m3_workspace_t *workspace,
                               m3_result_t *result);
wrj_status_t m3_preprocess_iq(wrj_cf32_t *iq, uint32_t count,
                              m3_workspace_t *workspace);
wrj_status_t m3_select_wideband_numerology(const wrj_cf32_t *iq, uint32_t count,
                                           float sample_rate_hz,
                                           m3_workspace_t *workspace,
                                           uint32_t *nfft, uint32_t *cp_samples);
wrj_status_t m3_droneid_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                    float sample_rate_hz, uint32_t max_frames,
                                    m3_workspace_t *workspace, m3_result_t *result);
wrj_status_t m3_remoteid_ble_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                         float sample_rate_hz, uint32_t max_frames,
                                         m3_workspace_t *workspace, m3_result_t *result);
wrj_status_t m3_burst_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                  float sample_rate_hz, uint32_t max_frames,
                                  m3_workspace_t *workspace, m3_result_t *result);
wrj_status_t m3_blind_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                  float sample_rate_hz, uint32_t max_frames,
                                  m3_workspace_t *workspace, m3_result_t *result);
void m3_estimate_sfo(const wrj_cf32_t *iq, uint32_t count, uint32_t nfft,
                     uint32_t cp_samples, m3_result_t *result);
wrj_status_t m3_run(const wrj_candidate_t *candidate, const wrj_cf32_t *iq,
                    uint32_t count, const m3_config_t *config,
                    m3_workspace_t *workspace, m3_result_t *result);

#endif
