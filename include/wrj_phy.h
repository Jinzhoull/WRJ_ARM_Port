#ifndef WRJ_PHY_H
#define WRJ_PHY_H
#include "wrj_receiver.h"
typedef struct {uint32_t start;double score,phase;} wrj_phy_peak_t;
double complex *wrj_iq64_normalized(const wrj_cf32_t *iq,uint32_t count);
wrj_status_t wrj_dft_complex(double complex *x,uint32_t count,int inverse);
double complex *wrj_resample64(const double complex *x,uint32_t count,uint32_t p,uint32_t q,uint32_t *output_count);
double *wrj_matched_metric(const double complex *x,uint32_t count,const double complex *reference,uint32_t length);
int wrj_solve_complex(double complex *a,double complex *b,uint32_t size);
uint32_t wrj_phy_cp_peaks(const double complex *x,uint32_t count,uint32_t nfft,
    uint32_t cp,wrj_phy_peak_t *peaks,uint32_t limit,double floor_score,int distributed);
uint32_t wrj_phy_coarse_ofdm_peaks(const double complex *x,uint32_t count,double fs,uint32_t nfft,
    uint32_t cp,wrj_phy_peak_t *peaks,uint32_t limit);
void wrj_phy_begin_ofdm_cache(void);
void wrj_phy_end_ofdm_cache(void);
int wrj_ofdm_receive(const double complex *x,uint32_t count,double fs,
    const wrj_rx_record_t *context,wrj_parse_result_t *result,uint32_t level,int primary_only);
int wrj_sc_receive(const double complex *x,uint32_t count,double fs,
    const wrj_rx_record_t *context,wrj_parse_result_t *result);
int wrj_openset_receive(const double complex *x,uint32_t count,double fs,
    const wrj_rx_record_t *context,wrj_parse_result_t *result,uint32_t evidence_limit);
int wrj_drone_receive(const double complex *x,uint32_t count,double fs,
    const wrj_rx_record_t *context,wrj_parse_result_t *result,int deep);
int wrj_drone_looks(const double complex *x,uint32_t count,double fs);
int wrj_remote_receive(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,
    wrj_parse_result_t *result,int deep);
#endif
