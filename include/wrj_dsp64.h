#ifndef WRJ_DSP64_H
#define WRJ_DSP64_H
#include "wrj_types.h"
/* Double precision intermediates used by E34. The existing CF32 FFT stays
 * available to the baseline receiver and embedded workspace. */
wrj_status_t wrj_fft64(double *re, double *im, uint32_t length, int inverse);
wrj_status_t wrj_bandlimit64(wrj_cf32_t *iq, uint32_t count, double fs, double bw);
wrj_status_t wrj_ble_fast_sync(const wrj_cf32_t *iq, uint32_t count, double fs,
    m3_result_t *result);
#endif
