#include "m3_module3.h"
#include "common/perf_timer.h"

static int m3_is_power_of_two(uint32_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static uint32_t m3_reverse_bits(uint32_t value, uint32_t bits)
{
    uint32_t reversed = 0U;
    uint32_t index;
    for (index = 0U; index < bits; ++index) {
        reversed = (reversed << 1U) | (value & 1U);
        value >>= 1U;
    }
    return reversed;
}

wrj_status_t m3_fft_forward_radix2(float *re, float *im, uint32_t length)
{
    uint32_t bits = 0U;
    uint32_t index;
    uint32_t span;

    if (re == NULL || im == NULL || !m3_is_power_of_two(length)) {
        return WRJ_ERR_ARGUMENT;
    }
    M3_PERF_COUNT(M3_PERF_OP_FFT_CALLS, 1U);
    while ((1U << bits) < length) {
        ++bits;
    }
    for (index = 0U; index < length; ++index) {
        const uint32_t reversed = m3_reverse_bits(index, bits);
        if (reversed > index) {
            const float re_temp = re[index];
            const float im_temp = im[index];
            re[index] = re[reversed];
            im[index] = im[reversed];
            re[reversed] = re_temp;
            im[reversed] = im_temp;
        }
    }

    for (span = 2U; span <= length; span <<= 1U) {
        const uint32_t half = span >> 1U;
        const float step = -2.0f * (float)WRJ_PI / (float)span;
        uint32_t offset;
        /* Within one stage, each butterfly pair belongs to one disjoint span
         * block and one offset, so changing their traversal order is safe. */
        for (offset = 0U; offset < half; ++offset) {
            const float angle = step * (float)offset;
            const float tw_re = cosf(angle);
            const float tw_im = sinf(angle);
            uint32_t base;
            for (base = 0U; base < length; base += span) {
                const uint32_t upper = base + offset;
                const uint32_t lower = upper + half;
                const float vr = re[lower] * tw_re - im[lower] * tw_im;
                const float vi = re[lower] * tw_im + im[lower] * tw_re;
                const float ur = re[upper];
                const float ui = im[upper];
                re[upper] = ur + vr;
                im[upper] = ui + vi;
                re[lower] = ur - vr;
                im[lower] = ui - vi;
            }
        }
        if (span == length) {
            break;
        }
    }
    return WRJ_OK;
}
