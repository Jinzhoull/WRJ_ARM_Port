#include "m3_module3.h"
#include "common/perf_timer.h"
#include "wrj_dsp64.h"

#include <stdlib.h>

static int m3_float_compare(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    return (a > b) - (a < b);
}

static int m3_peak_value_desc(const void *left, const void *right)
{
    const m3_peak_t *a = left;
    const m3_peak_t *b = right;
    return (a->value < b->value) - (a->value > b->value);
}

static int m3_peak_index_asc(const void *left, const void *right)
{
    const m3_peak_t *a = left;
    const m3_peak_t *b = right;
    return (a->index > b->index) - (a->index < b->index);
}

static float m3_quantile(float *scratch, const float *values, uint32_t count, float probability)
{
    const float position = probability * (float)(count - 1U);
    const uint32_t lower = (uint32_t)floorf(position);
    const uint32_t upper = (uint32_t)ceilf(position);
    if (scratch != values) {
        memcpy(scratch, values, sizeof(float) * (size_t)count);
    }
    qsort(scratch, count, sizeof(float), m3_float_compare);
    return scratch[lower] + (position - (float)lower) * (scratch[upper] - scratch[lower]);
}

static uint32_t m3_ble_crc24(const uint8_t *bits, uint32_t count)
{
    uint32_t crc = 0x555555U;
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        const uint32_t feedback = ((crc >> 23U) & 1U) ^ (uint32_t)(bits[index] != 0U);
        crc = (crc << 1U) & 0xFFFFFFU;
        if (feedback != 0U) {
            crc ^= 0x00065BU;
        }
    }
    return crc;
}

static void m3_ble_dewhiten(uint8_t *bits, uint32_t count)
{
    uint8_t state[7];
    uint32_t index;
    const uint8_t channel = 38U;
    state[0] = 1U;
    for (index = 0U; index < 6U; ++index) {
        state[index + 1U] = (uint8_t)((channel >> (5U - index)) & 1U);
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t whitening = state[6];
        uint32_t q;
        bits[index] ^= whitening;
        for (q = 6U; q > 0U; --q) {
            state[q] = state[q - 1U];
        }
        state[0] = whitening;
        state[4] ^= whitening;
    }
}

static void m3_ble_pack_pdu(const uint8_t *bits, uint8_t pdu[39])
{
    uint32_t i;
    memset(pdu, 0, 39U);
    for (i = 0U; i < 312U; ++i) pdu[i / 8U] |= (uint8_t)(bits[i] << (i & 7U));
}

static int m3_ble_soft_packet_plausible(const uint8_t *bits, uint8_t pdu[39])
{
    const uint32_t crc = m3_ble_crc24(bits, 312U);
    uint32_t received = 0U, i;
    const uint8_t *message;
    uint8_t type;
    for (i = 0U; i < 24U; ++i) received = (received << 1U) | bits[312U + i];
    if (crc != received) return 0;
    m3_ble_pack_pdu(bits, pdu);
    if (pdu[0] != 0x42U || pdu[1] != 37U || pdu[8] != 30U ||
        pdu[9] != 0x16U || pdu[10] != 0xFAU || pdu[11] != 0xFFU ||
        pdu[12] != 0x0DU) return 0;
    message = pdu + 14U;
    type = message[0] >> 4U;
    if ((message[0] & 0x0FU) != 2U ||
        (type != 0U && type != 1U && type != 4U && type != 5U)) return 0;
    if (type == 0U) {
        uint32_t visible = 0U;
        int ended = 0;
        for (i = 2U; i < 22U; ++i) {
            const uint8_t ch = message[i];
            if (ch == 0U) { ended = 1; continue; }
            if (ended || !((ch >= '0' && ch <= '9') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '_')) return 0;
            ++visible;
        }
        return visible >= 3U;
    }
    if (type == 1U) {
        const int32_t latitude = (int32_t)((uint32_t)message[5] |
            ((uint32_t)message[6] << 8U) | ((uint32_t)message[7] << 16U) |
            ((uint32_t)message[8] << 24U));
        const int32_t longitude = (int32_t)((uint32_t)message[9] |
            ((uint32_t)message[10] << 8U) | ((uint32_t)message[11] << 16U) |
            ((uint32_t)message[12] << 24U));
        const double pressure = ((uint16_t)message[13] | ((uint16_t)message[14] << 8U)) * .5 - 1000.0;
        const double altitude = ((uint16_t)message[15] | ((uint16_t)message[16] << 8U)) * .5 - 1000.0;
        const double speed = (message[1] & 1U) ? 63.75 + .75 * message[3] : .25 * message[3];
        return llabs((long long)latitude) <= 900000000LL &&
            llabs((long long)longitude) <= 1800000000LL &&
            fabs(pressure - altitude) <= 30.0 && altitude >= -500.0 &&
            altitude <= 10000.0 && speed <= 250.0;
    }
    return 1;
}

static uint32_t m3_ble_crc_syndrome(const uint8_t *bits)
{
    uint32_t received = 0U, i;
    for (i = 0U; i < 24U; ++i) received = (received << 1U) | bits[312U + i];
    return m3_ble_crc24(bits, 312U) ^ received;
}

static int m3_ble_soft_list_search(uint8_t *bits, const uint16_t *positions,
                                   uint32_t position_count, const uint32_t *effects,
                                   uint32_t depth, uint32_t next, uint32_t syndrome,
                                   uint8_t pdu[39])
{
    uint32_t i;
    if (depth == 0U) {
        return syndrome == 0U && m3_ble_soft_packet_plausible(bits, pdu);
    }
    for (i = next; i + depth <= position_count; ++i) {
        const uint32_t position = positions[i];
        bits[position] ^= 1U;
        if (m3_ble_soft_list_search(bits, positions, position_count, effects,
                depth - 1U, i + 1U, syndrome ^ effects[position], pdu)) return 1;
        bits[position] ^= 1U;
    }
    return 0;
}

static uint32_t m3_ble_popcount24(uint32_t value)
{
    uint32_t count = 0U;
    while (value != 0U) { value &= value - 1U; ++count; }
    return count;
}

static int m3_ble_crc_erasure_search(uint8_t *bits, const uint16_t *positions,
                                      uint32_t position_count, const uint32_t *effects,
                                      uint32_t depth, uint32_t next, uint32_t syndrome,
                                      uint8_t pdu[39])
{
    uint32_t i;
    if (depth == 0U) {
        if (m3_ble_popcount24(syndrome) > 2U) return 0;
        for (i = 0U; i < 24U; ++i)
            if ((syndrome >> (23U - i)) & 1U) bits[312U + i] ^= 1U;
        if (m3_ble_soft_packet_plausible(bits, pdu)) return 1;
        for (i = 0U; i < 24U; ++i)
            if ((syndrome >> (23U - i)) & 1U) bits[312U + i] ^= 1U;
        return 0;
    }
    for (i = next; i + depth <= position_count; ++i) {
        const uint32_t position = positions[i];
        bits[position] ^= 1U;
        if (m3_ble_crc_erasure_search(bits, positions, position_count, effects,
                depth - 1U, i + 1U, syndrome ^ effects[position], pdu)) return 1;
        bits[position] ^= 1U;
    }
    return 0;
}

static int m3_ble_soft_crc_recover(uint8_t bits[336], const float reliability[336],
                                   uint8_t pdu[39],
                                   const uint8_t extra_protected[336])
{
    static const uint8_t fixed_bytes[7] = {0x42U,37U,30U,0x16U,0xFAU,0xFFU,0x0DU};
    static const uint8_t fixed_positions[7] = {0U,1U,8U,9U,10U,11U,12U};
    static const uint8_t limits[6] = {24U,24U,24U,12U,10U,8U};
    static const uint8_t basic_limits[6] = {32U,28U,24U,20U,16U,12U};
    static uint32_t effects[336];
    static int effects_ready = 0;
    uint16_t order[336];
    uint16_t data_order[312];
    uint8_t protected[336] = {0U};
    uint32_t i, j, depth, syndrome;
    if (!effects_ready) {
        uint8_t zero[312] = {0U};
        const uint32_t base_crc = m3_ble_crc24(zero, 312U);
        for (i = 0U; i < 312U; ++i) {
            zero[i] = 1U;
            effects[i] = base_crc ^ m3_ble_crc24(zero, 312U);
            zero[i] = 0U;
        }
        for (i = 0U; i < 24U; ++i) effects[312U + i] = 1U << (23U - i);
        effects_ready = 1;
    }
    for (i = 0U; i < 7U; ++i) {
        for (j = 0U; j < 8U; ++j) {
            const uint32_t index = (uint32_t)fixed_positions[i] * 8U + j;
            bits[index] = (fixed_bytes[i] >> j) & 1U;
            protected[index] = 1U;
        }
    }
    if (extra_protected != NULL) {
        for (i = 0U; i < 336U; ++i)
            if (extra_protected[i]) protected[i] = 1U;
    }
    if (m3_ble_soft_packet_plausible(bits, pdu)) return 1;
    syndrome = m3_ble_crc_syndrome(bits);
    j = 0U;
    for (i = 0U; i < 336U; ++i) {
        uint32_t k;
        if (protected[i]) continue;
        for (k = j; k > 0U && reliability[order[k - 1U]] > reliability[i]; --k)
            order[k] = order[k - 1U];
        order[k] = (uint16_t)i;
        ++j;
    }
    {
        uint32_t data_count = 0U;
        for (i = 0U; i < j; ++i)
            if (order[i] < 312U) data_order[data_count++] = order[i];
        for (depth = 1U; depth <= 6U; ++depth) {
            const uint32_t n = WRJ_MIN((uint32_t)(extra_protected != NULL ?
                basic_limits[depth - 1U] : limits[depth - 1U]), j);
            if (n >= depth && m3_ble_soft_list_search(bits, order, n, effects,
                    depth, 0U, syndrome, pdu)) return 1;
        }
        for (depth = 0U; depth <= 3U; ++depth) {
            const uint32_t n = WRJ_MIN(depth == 3U ? 24U : 32U, data_count);
            if (m3_ble_crc_erasure_search(bits, data_order, n, effects,
                    depth, 0U, syndrome, pdu)) return 1;
        }
    }
    return 0;
}

int m3_ble_recover_soft_pdu(const float soft[336], uint8_t pdu[39], uint32_t *crc)
{
    uint8_t bits[336];
    float reliability[336];
    uint32_t i;
    if (soft == NULL || pdu == NULL || crc == NULL) return 0;
    for (i = 0U; i < 336U; ++i) {
        bits[i] = soft[i] > 0.0f ? 1U : 0U;
        reliability[i] = fabsf(soft[i]);
    }
    if (!m3_ble_soft_crc_recover(bits, reliability, pdu, NULL)) return 0;
    *crc = m3_ble_crc24(bits, 312U);
    return 1;
}

int m3_ble_recover_basic_id(const float soft[336], uint8_t message_counter,
                            uint8_t pdu[39], uint32_t *crc)
{
    static const char alphabet[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
    uint8_t base[336], selected[336], protected[336] = {0U};
    float reliability[336];
    float best_score = -INFINITY;
    uint32_t i, length;
    if (soft == NULL || pdu == NULL || crc == NULL) return 0;
    for (i = 0U; i < 336U; ++i) {
        base[i] = soft[i] > 0.0f;
        reliability[i] = fabsf(soft[i]);
    }
    for (i = 0U; i < 8U; ++i) {
        base[13U * 8U + i] = (message_counter >> i) & 1U;
        base[14U * 8U + i] = (0x02U >> i) & 1U;
        protected[13U * 8U + i] = 1U;
        protected[14U * 8U + i] = 1U;
    }
    for (length = 3U; length <= 20U; ++length) {
        uint8_t trial[336];
        float score = 0.0f;
        uint32_t byte;
        memcpy(trial, base, sizeof(trial));
        for (byte = 0U; byte < 20U; ++byte) {
            const uint32_t position = (16U + byte) * 8U;
            uint8_t best_char = 0U;
            float best_char_score = -INFINITY;
            uint32_t option;
            if (byte < length) {
                for (option = 0U; option < sizeof(alphabet) - 1U; ++option) {
                    const uint8_t ch = (uint8_t)alphabet[option];
                    float candidate_score = 0.0f;
                    for (i = 0U; i < 8U; ++i)
                        candidate_score += ((ch >> i) & 1U ? 1.0f : -1.0f) *
                            soft[position + i];
                    if (candidate_score > best_char_score) {
                        best_char_score = candidate_score;
                        best_char = ch;
                    }
                }
                score += best_char_score;
            } else {
                for (i = 0U; i < 8U; ++i) score -= soft[position + i];
            }
            for (i = 0U; i < 8U; ++i) {
                trial[position + i] = (best_char >> i) & 1U;
                protected[position + i] = 1U;
            }
        }
        if (score > best_score) {
            best_score = score;
            memcpy(selected, trial, sizeof(selected));
        }
    }
    if (!m3_ble_soft_crc_recover(selected, reliability, pdu, protected) ||
        (pdu[14] >> 4U) != 0U || pdu[13] != message_counter) return 0;
    *crc = m3_ble_crc24(selected, 312U);
    return 1;
}

static int m3_ble_crc_attempt(const double *phase_delta, uint32_t count, uint32_t start,
                              uint32_t sps, int32_t shift, uint32_t half_window,
                              const int8_t *expected, const uint8_t *whiten_mask,
                              int32_t *timing_offset, int allow_soft,
                              uint8_t pdu[39], uint32_t *verified_crc,
                              int *soft_recovered,
                              float raw_soft[336],uint8_t received_raw[336])
{
    enum { SYNC_BITS = 40, DATA_BITS = 336 };
    float soft[SYNC_BITS + DATA_BITS];
    uint8_t raw[DATA_BITS];
    float reliability[DATA_BITS];
    double expected_mean = 0.0;
    double soft_mean = 0.0;
    double covariance = 0.0;
    double expected_variance = 0.0;
    double slope;
    double intercept;
    uint32_t bit;
    uint32_t received_crc = 0U;
    uint32_t pdu_bits;
    const uint32_t lag = WRJ_MAX(1U, sps / 4U);

    for (bit = 0U; bit < SYNC_BITS + 16U; ++bit) {
        const int64_t center = (int64_t)start + (int64_t)shift + (int64_t)(sps / 2U) +
            (int64_t)bit * (int64_t)sps;
        const int64_t low = center - (int64_t)half_window;
        const int64_t high = center + (int64_t)half_window;
        double phase_sum = 0.0;
        int64_t sample;
        if (low < 0 || high + (int64_t)lag >= (int64_t)count) {
            return 0;
        }
        for (sample = low; sample <= high; ++sample) {
            phase_sum += phase_delta[sample];
        }
        soft[bit] = (float)(phase_sum / (double)(2U * half_window + 1U));
    }
    for (bit = 0U; bit < SYNC_BITS; ++bit) {
        expected_mean += expected[bit];
        soft_mean += soft[bit];
    }
    expected_mean /= SYNC_BITS;
    soft_mean /= SYNC_BITS;
    for (bit = 0U; bit < SYNC_BITS; ++bit) {
        const double centered_expected = expected[bit] - expected_mean;
        covariance += centered_expected * ((double)soft[bit] - soft_mean);
        expected_variance += centered_expected * centered_expected;
    }
    if (expected_variance < 1.0e-9) {
        return 0;
    }
    slope = covariance / expected_variance;
    if (fabs(slope) < 1.0e-9) {
        return 0;
    }
    intercept = soft_mean - slope * expected_mean;
    for (bit = 0U; bit < 16U; ++bit) {
        raw[bit] = (uint8_t)((((double)soft[SYNC_BITS + bit] - intercept) / slope) > 0.0);
    }
    {
        uint32_t length = 0U;
        for (bit = 0U; bit < 8U; ++bit) {
            const uint8_t decoded = raw[8U + bit] ^ whiten_mask[8U + bit];
            length |= (uint32_t)decoded << bit;
        }
        length &= 63U;
        if ((length < 6U || length > 37U) && !allow_soft) {
            return 0;
        }
        pdu_bits = (2U + (allow_soft ? 37U : length)) * 8U;
    }
    for (bit = SYNC_BITS + 16U; bit < SYNC_BITS + DATA_BITS; ++bit) {
        const int64_t center = (int64_t)start + (int64_t)shift + (int64_t)(sps / 2U) +
            (int64_t)bit * (int64_t)sps;
        const int64_t low = center - (int64_t)half_window;
        const int64_t high = center + (int64_t)half_window;
        double phase_sum = 0.0;
        int64_t sample;
        if (low < 0 || high + (int64_t)lag >= (int64_t)count) {
            return 0;
        }
        for (sample = low; sample <= high; ++sample) {
            phase_sum += phase_delta[sample];
        }
        soft[bit] = (float)(phase_sum / (double)(2U * half_window + 1U));
    }
    for (bit = 16U; bit < DATA_BITS; ++bit) {
        raw[bit] = (uint8_t)((((double)soft[SYNC_BITS + bit] - intercept) / slope) > 0.0);
    }
    for (bit = 0U; bit < DATA_BITS; ++bit) {
        reliability[bit] = (float)fabs(((double)soft[SYNC_BITS + bit] - intercept) / slope);
        raw[bit] ^= whiten_mask[bit];
        if (raw_soft != NULL)
            raw_soft[bit] = raw[bit] ? reliability[bit] : -reliability[bit];
    }
    if (pdu_bits + 24U > DATA_BITS) {
        return 0;
    }
    for (bit = 0U; bit < 24U; ++bit) {
        received_crc = (received_crc << 1U) | (uint32_t)raw[pdu_bits + bit];
    }
    M3_PERF_COUNT(M3_PERF_OP_CRC_CHECKS, 1U);
    if (m3_ble_crc24(raw, pdu_bits) == received_crc) {
        if(received_raw!=NULL)memcpy(received_raw,raw,336U);
        if (pdu_bits == 312U) {
            m3_ble_pack_pdu(raw, pdu);
            if (verified_crc != NULL) *verified_crc = received_crc;
        }
        if (soft_recovered != NULL) *soft_recovered = 0;
        if (timing_offset != NULL) {
            *timing_offset = shift;
        }
        return 1;
    }
    if (allow_soft && m3_ble_soft_crc_recover(raw, reliability, pdu, NULL)) {
        if (verified_crc != NULL) *verified_crc = m3_ble_crc24(raw, 312U);
        if (timing_offset != NULL) *timing_offset = shift;
        if (soft_recovered != NULL) *soft_recovered = 1;
        return 1;
    }
    return 0;
}

static void m3_ble_lagged_discriminator(const wrj_cf32_t *iq, uint32_t count,
                                        uint32_t sps, float *output, int coherent)
{
    static const float scale[4] = {0.65f, 1.0f, 1.45f, 1.90f};
    static const float weight[4] = {0.16f, 0.34f, 0.30f, 0.20f};
    const uint32_t base_lag = WRJ_MAX(1U, (uint32_t)lroundf(0.25f * (float)sps));
    uint32_t branch;
    uint32_t used_lags[4] = {0U};
    uint32_t branch_count = 0U;
    memset(output, 0, sizeof(*output) * (size_t)count);
    if (coherent) {
        for (branch = 0U; branch < 4U; ++branch) {
            const uint32_t lag = WRJ_MAX(1U, (uint32_t)lroundf((float)base_lag * scale[branch]));
            if (branch_count == 0U || lag != used_lags[branch_count - 1U])
                used_lags[branch_count++] = lag;
        }
        for (branch = 0U; branch < branch_count; ++branch) {
            const uint32_t lag = used_lags[branch];
            const uint32_t product_count = count - lag;
            const uint32_t offset = (lag - 1U) / 2U;
            const double branch_weight = branch_count == 4U ? weight[branch] : 1.0 / branch_count;
            double sum_re = 0.0, sum_im = 0.0;
            uint32_t left = 0U, right = 0U, index;
            float first = 0.0f, last = 0.0f;
            for (index = 0U; index < product_count; ++index) {
                const uint32_t wanted_left = index > lag / 2U ? index - lag / 2U : 0U;
                const uint32_t wanted_right = WRJ_MIN(product_count,
                    index + (lag - lag / 2U));
                while (right < wanted_right) {
                    const wrj_cf32_t a = iq[right], b = iq[right + lag];
                    sum_re += (double)a.re * b.re + (double)a.im * b.im;
                    sum_im += (double)a.re * b.im - (double)a.im * b.re;
                    ++right;
                }
                while (left < wanted_left) {
                    const wrj_cf32_t a = iq[left], b = iq[left + lag];
                    sum_re -= (double)a.re * b.re + (double)a.im * b.im;
                    sum_im -= (double)a.re * b.im - (double)a.im * b.re;
                    ++left;
                }
                last = (float)(atan2(sum_im, sum_re) / (double)lag);
                if (index == 0U) first = last;
                output[index + offset] += (float)(branch_weight * last);
            }
            for (index = 0U; index < offset; ++index)
                output[index] += (float)(branch_weight * first);
            for (index = product_count + offset; index < count; ++index)
                output[index] += (float)(branch_weight * last);
        }
        return;
    }
    for (branch = 0U; branch < 4U; ++branch) {
        const uint32_t lag = WRJ_MAX(1U, (uint32_t)lroundf((float)base_lag * scale[branch]));
        uint32_t index;
        for (index = 0U; index + lag < count; ++index) {
            const wrj_cf32_t a = iq[index];
            const wrj_cf32_t b = iq[index + lag];
            const float value = atan2f(a.re * b.im - a.im * b.re,
                                       a.re * b.re + a.im * b.im) / (float)lag;
            const uint32_t location = WRJ_MIN(count - 1U, index + (lag - 1U) / 2U);
            output[location] += weight[branch] * value;
        }
    }
}

static void m3_inverse_fft(float *re, float *im, uint32_t length)
{
    uint32_t index;
    for (index = 0U; index < length; ++index) {
        im[index] = -im[index];
    }
    (void)m3_fft_forward_radix2(re, im, length);
    for (index = 0U; index < length; ++index) {
        re[index] /= (float)length;
        im[index] = -im[index] / (float)length;
    }
}

static double m3_bessel_i0(double x)
{
    double term=1.0,sum=1.0;
    for (uint32_t k=1U;k<64U;++k) {
        term*=x*x/(4.0*k*k);sum+=term;
        if (term<sum*1e-16) break;
    }
    return sum;
}

static void m3_generate_droneid_template(uint32_t nfft, uint32_t active,
                                         uint32_t root, float *template_re,
                                         float *template_im, m3_workspace_t *workspace)
{
    uint32_t index,divisor=nfft,b=1024U;
    double re[1024]={0.0},im[1024]={0.0};
    const uint32_t first_shifted = 512U - active / 2U;
    (void)workspace;
    while (b) {const uint32_t t=divisor%b;divisor=b;b=t;}
    const uint32_t p=nfft/divisor,q=1024U/divisor,m=WRJ_MAX(p,q),half=10U*m;
    for (index = 0U; index < active; ++index) {
        const double phase = -WRJ_PI * (double)root * (double)index *
            (double)(index + 1U) / (double)active;
        const uint32_t shifted_bin = first_shifted + index;
        const uint32_t raw_bin = (shifted_bin + 512U) % 1024U;
        re[raw_bin] = cos(phase);
        im[raw_bin] = sin(phase);
    }
    (void)wrj_fft64(re,im,1024U,1);
    double *h=calloc(2U*half+1U,sizeof(double)),sum=0.0;
    if (!h) return;
    for (index=0U;index<=2U*half;++index) {
        const double x=(double)index-half;
        const double sinc=fabs(x)<1e-12 ? 1.0/m : sin(WRJ_PI*x/m)/(WRJ_PI*x);
        h[index]=sinc*m3_bessel_i0(5.0*sqrt(WRJ_MAX(0.0,1.0-x*x/(half*half))))/m3_bessel_i0(5.0);
        sum+=h[index];
    }
    for (index=0U;index<=2U*half;++index) h[index]*=p/sum;
    for (index = 0U; index < nfft; ++index) {
        double sr=0.0,si=0.0;
        if (p==1U && q==1U) {sr=re[index];si=im[index];}
        else {
            const int64_t pos=(int64_t)index*q;
            const int64_t first=WRJ_MAX(0,(pos-(int64_t)half+(int64_t)p-1)/(int64_t)p);
            const int64_t last=WRJ_MIN(1023,(pos+half)/p);
            for (int64_t j=first;j<=last;++j) {
                const double weight=h[pos-j*p+half];sr+=weight*re[j];si+=weight*im[j];
            }
        }
        template_re[index]=(float)(32.0*sr);template_im[index]=(float)(32.0*si);
    }
    free(h);
}

/* MATLAB m3_matched_metric_fft: global normalized ZC correlation. The FFT
 * workspace is allocated once per template, not in the frame-search loop. */
static int m3_droneid_global_peak(const wrj_cf32_t *iq, uint32_t count,
                                  const float *tpl_re, const float *tpl_im,
                                  uint32_t length, uint32_t *peak_start,
                                  float *peak_metric)
{
    uint32_t size = 1U, i;
    float *xr, *xi, *tr, *ti;
    double tpl_energy = 0.0, window_energy = 0.0;
    float best = -1.0f;
    uint32_t best_start = 0U;
    if (count < length || length == 0U) return 0;
    while (size < count + length - 1U && size <= (1U << 22U)) size <<= 1U;
    if (size < count + length - 1U) return 0;
    xr = (float *)calloc(size, sizeof(float));
    xi = (float *)calloc(size, sizeof(float));
    tr = (float *)calloc(size, sizeof(float));
    ti = (float *)calloc(size, sizeof(float));
    if (!xr || !xi || !tr || !ti) {
        free(xr); free(xi); free(tr); free(ti);
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        xr[i] = iq[i].re;
        xi[i] = iq[i].im;
    }
    for (i = 0U; i < length; ++i) {
        tr[i] = tpl_re[length - 1U - i];
        ti[i] = -tpl_im[length - 1U - i];
        tpl_energy += (double)tpl_re[i] * tpl_re[i] + (double)tpl_im[i] * tpl_im[i];
        window_energy += wrj_complex_abs2(iq[i].re, iq[i].im);
    }
    if (m3_fft_forward_radix2(xr, xi, size) != WRJ_OK ||
        m3_fft_forward_radix2(tr, ti, size) != WRJ_OK) {
        free(xr); free(xi); free(tr); free(ti);
        return 0;
    }
    for (i = 0U; i < size; ++i) {
        const float a = xr[i], b = xi[i];
        xr[i] = a * tr[i] - b * ti[i];
        xi[i] = a * ti[i] + b * tr[i];
    }
    m3_inverse_fft(xr, xi, size);
    for (i = 0U; i + length <= count; ++i) {
        const uint32_t index = i + length - 1U;
        const double magnitude = hypot((double)xr[index], (double)xi[index]);
        const float score = (float)(magnitude /
            sqrt(WRJ_MAX(window_energy * tpl_energy, 1.0e-30)));
        if (score > best) { best = score; best_start = i; }
        if (i + length < count) {
            window_energy += wrj_complex_abs2(iq[i + length].re, iq[i + length].im) -
                             wrj_complex_abs2(iq[i].re, iq[i].im);
        }
    }
    free(xr); free(xi); free(tr); free(ti);
    *peak_start = best_start;
    *peak_metric = best;
    return 1;
}

/* MATLAB m3_collect_droneid_cp: one coherence per variable-CP symbol,
 * averaged over all complete observations on the late ZC-anchored grid. */
static float m3_droneid_cp_pattern_score(const wrj_cf32_t *iq, uint32_t count,
                                         int64_t start, uint32_t frame_length,
                                         uint32_t nfft, float scale)
{
    static const uint32_t cp_base[9] = {80U,72U,72U,72U,72U,72U,72U,72U,80U};
    double sum = 0.0;
    uint32_t used = 0U;
    /* m3_droneid_grid_in_bounds folds the trial into the first period for
     * CP scoring, even though the selected ZC anchor itself remains late. */
    while (start < -(int64_t)lroundf(80.0f * scale)) start += frame_length;
    while (start > (int64_t)frame_length) start -= frame_length;
    for (; start < (int64_t)count; start += frame_length) {
        int64_t position = start;
        uint32_t symbol;
        for (symbol = 0U; symbol < 9U; ++symbol) {
            const uint32_t cp = (uint32_t)lroundf((float)cp_base[symbol] * scale);
            uint32_t j;
            double re = 0.0, im = 0.0, ea = 0.0, eb = 0.0;
            if (position < 0 || position + (int64_t)nfft + cp > (int64_t)count) break;
            for (j = 0U; j < cp; ++j) {
                const wrj_cf32_t a = iq[position + j];
                const wrj_cf32_t b = iq[position + nfft + j];
                re += (double)a.re * b.re + (double)a.im * b.im;
                im += (double)a.re * b.im - (double)a.im * b.re;
                ea += wrj_complex_abs2(a.re, a.im);
                eb += wrj_complex_abs2(b.re, b.im);
            }
            sum += hypot(re, im) / (sqrt(ea * eb) + 1.0e-20);
            ++used;
            position += (int64_t)nfft + cp;
        }
    }
    return used ? (float)(sum / (double)used) : -1.0f;
}

wrj_status_t m3_droneid_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                    float sample_rate_hz, uint32_t max_frames,
                                    m3_workspace_t *workspace, m3_result_t *result)
{
    const float scale = sample_rate_hz / 15360000.0f;
    const uint32_t nfft = (uint32_t)lroundf(1024.0f * scale);
    const uint32_t cp = (uint32_t)lroundf(72.0f * scale);
    const uint32_t frame_length = (uint32_t)lroundf((9.0f * 1024.0f + 2.0f * 80.0f +
                                                     7.0f * 72.0f) * scale);
    const uint32_t offset600 = (uint32_t)lroundf((1104.0f + 1096.0f + 1096.0f + 72.0f) * scale);
    const uint32_t offset147 = (uint32_t)lroundf((1104.0f + 4.0f * 1096.0f + 72.0f) * scale);
    m3_result_t cp_result;
    int64_t best_start = -1;
    float best_score = -1.0f;
    uint32_t peak600 = 0U, peak147 = 0U;
    float metric600 = 0.0f, metric147 = 0.0f;
    float cp_pattern_best = 0.0f;
    int valid600, valid147;
    uint32_t output = 0U;
    M3_PERF_TIMER(sync_timer);
    M3_PERF_TIMER(cp_fusion_timer);
    M3_PERF_TIMER(zc_timer);
    M3_PERF_TIMER(alignment_timer);
    if (nfft > workspace->spectrum_length || count <= frame_length) {
        return WRJ_ERR_CAPACITY;
    }
    memset(&cp_result, 0, sizeof(cp_result));
    M3_PERF_START(sync_timer);
    M3_PERF_START(cp_fusion_timer);
    (void)m3_cp_synchronize(iq, count, nfft, cp, max_frames, workspace, &cp_result);
    M3_PERF_STOP(M3_PERF_DRONEID_CP_FUSION, cp_fusion_timer);
    M3_PERF_START(zc_timer);
    m3_generate_droneid_template(nfft, 601U, 600U, workspace->spectrum_smooth,
                                 workspace->spectrum_weight, workspace);
    m3_generate_droneid_template(nfft, 601U, 147U, workspace->spectrum_aux,
                                 workspace->power, workspace);
    valid600 = m3_droneid_global_peak(iq, count, workspace->spectrum_smooth,
                                      workspace->spectrum_weight, nfft,
                                      &peak600, &metric600);
    valid147 = m3_droneid_global_peak(iq, count, workspace->spectrum_aux,
                                      workspace->power, nfft,
                                      &peak147, &metric147);
    workspace->droneid_peak600_0based = peak600;
    workspace->droneid_peak147_0based = peak147;
    /* Filter ZC observations before anchor selection and confidence fusion.
     * A template peak preceding the admissible CP guard is not a frame. */
    valid600 = valid600 && (int64_t)peak600 - offset600 >= -(int64_t)cp;
    valid147 = valid147 && (int64_t)peak147 - offset147 >= -(int64_t)cp;
    const float zc_mean = (valid600 && valid147) ?
        0.5f * (metric600 + metric147) : (valid600 ? metric600 : metric147);
    if (valid600 || valid147) {
        const int64_t start600 = (int64_t)peak600 - offset600;
        const int64_t start147 = (int64_t)peak147 - offset147;
        const int64_t anchor = (!valid147 || (valid600 && metric600 >= metric147)) ?
            start600 : start147;
        double weighted = 0.0, weight_sum = 0.0;
        const double agreement = WRJ_MAX((double)cp, 0.20 * (double)nfft);
        const int64_t aligned600 = start600 +
            (int64_t)llround((double)(anchor - start600) / (double)frame_length) * frame_length;
        const int64_t aligned147 = start147 +
            (int64_t)llround((double)(anchor - start147) / (double)frame_length) * frame_length;
        if (valid600 && start600 >= -(int64_t)cp &&
            (double)llabs(aligned600 - anchor) <= agreement) {
            const double w = (double)metric600 * metric600;
            weighted += w * (double)aligned600; weight_sum += w;
        }
        if (valid147 && start147 >= -(int64_t)cp &&
            (double)llabs(aligned147 - anchor) <= agreement) {
            const double w = (double)metric147 * metric147;
            weighted += w * (double)aligned147; weight_sum += w;
        }
        best_start = weight_sum > 0.0 ? (int64_t)llround(weighted / weight_sum) : anchor;
        best_score = valid600 && valid147 ? WRJ_MAX(metric600, metric147) : zc_mean;
        result->droneid_zc_peak_position = (float)((!valid147 ||
            (valid600 && metric600 >= metric147)) ? peak600 + 1U : peak147 + 1U);
    }
    if (best_start < 0) {
        M3_PERF_STOP(M3_PERF_DRONEID_ZC, zc_timer);
        M3_PERF_STOP(M3_PERF_DRONEID_SYNC, sync_timer);
        return WRJ_ERR_DATA;
    }
    M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES, 2U);
    result->original_frame_start = (float)best_start;
    {
        const int32_t radius = WRJ_MAX(8, (int32_t)lroundf(0.12f * (float)nfft));
        const int32_t step = WRJ_MAX(2, (int32_t)lroundf((float)cp / 10.0f));
        int32_t offset;
        int32_t best_offset = 0;
        float cp_best = -1.0f;
        for (offset = -radius; offset <= radius; offset += step) {
            const float score = m3_droneid_cp_pattern_score(iq, count, best_start + offset,
                                                            frame_length, nfft, scale);
            if (score > cp_best) {
                cp_best = score;
                best_offset = offset;
            }
        }
        M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES,
                      (uint64_t)((2 * radius) / step + 1 + 2 * step + 1));
        const int32_t fine_origin = best_offset;
        cp_best = -1.0f;
        for (offset = fine_origin - step; offset <= fine_origin + step; ++offset) {
            const float score = m3_droneid_cp_pattern_score(iq, count, best_start + offset,
                                                            frame_length, nfft, scale);
            if (score > cp_best) {
                cp_best = score;
                best_offset = offset;
            }
        }
        best_start += best_offset;
        cp_pattern_best = WRJ_MAX(cp_best, 0.0f);
        result->frame_offset_correction = (float)best_offset;
        result->timing_alignment_improved = (uint8_t)(best_offset != 0);
    }
    M3_PERF_STOP(M3_PERF_DRONEID_ZC, zc_timer);
    M3_PERF_START(alignment_timer);
    while (best_start < 0) {
        best_start += frame_length;
    }
    /* Preserve the selected global ZC frame, as in MATLAB, instead of
     * folding it to the first period and inventing earlier observations. */
    for (; best_start < (int64_t)count && output < WRJ_MIN(max_frames, WRJ_MAX_FRAMES);
         best_start += frame_length) {
        if (best_start + (int64_t)frame_length <= (int64_t)count) {
            result->frame_start_samples_0based[output] = (uint32_t)best_start;
            const float zc_evidence = wrj_clip01(1.0f /
                (1.0f + expf(-((zc_mean - 0.08f) / 0.04f))));
            const float cp_evidence = wrj_clip01((cp_pattern_best - 0.03f) / 0.22f);
            result->frame_confidence[output] = wrj_clip01(0.70f * zc_evidence +
                                                            0.30f * cp_evidence);
            ++output;
        }
    }
    result->num_frames = output;
    result->frame_length_samples = frame_length;
    result->corrected_frame_start = output > 0U ? (float)result->frame_start_samples_0based[0] : NAN;
    result->offset_search_score = best_score;
    result->peak_metric = best_score;
    result->sync_confidence = wrj_clip01(0.75f /
        (1.0f + expf(-((zc_mean - 0.08f) / 0.04f))) +
        0.25f * cp_result.sync_confidence);
    result->fractional_cfo_hz = cp_result.fractional_cfo_hz;
    snprintf(result->sync_method, sizeof(result->sync_method),
             "DJI_DroneID_ZC600_ZC147_plus_CP");
    M3_PERF_STOP(M3_PERF_FRAME_ALIGNMENT, alignment_timer);
    M3_PERF_STOP(M3_PERF_DRONEID_SYNC, sync_timer);
    return output > 0U ? WRJ_OK : WRJ_ERR_DATA;
}

static float m3_droneid_template_score_at(const wrj_cf32_t *iq, uint32_t count,
                                           int64_t start, const float *tr,
                                           const float *ti, uint32_t length)
{
    double re = 0.0, im = 0.0, ea = 0.0, eb = 0.0;
    uint32_t i;
    if (start < 0 || start + (int64_t)length > (int64_t)count) return 0.0f;
    for (i = 0U; i < length; ++i) {
        const wrj_cf32_t z = iq[start + i];
        re += (double)tr[i] * z.re + (double)ti[i] * z.im;
        im += (double)tr[i] * z.im - (double)ti[i] * z.re;
        ea += wrj_complex_abs2(z.re, z.im);
        eb += (double)tr[i] * tr[i] + (double)ti[i] * ti[i];
    }
    return (float)(hypot(re, im) / (sqrt(ea * eb) + 1.0e-20));
}

/* MATLAB m3_collect_droneid_cp, retaining both average coherence and the
 * weighted circular phase needed by m3_estimate_droneid_frame_cfo. */
static float m3_droneid_cp_measure(const wrj_cf32_t *iq, uint32_t count,
                                   int64_t first, uint32_t frame_length,
                                   uint32_t nfft, float scale, int all_frames,
                                   double *phase_re, double *phase_im)
{
    static const uint32_t cp_base[9] = {80U,72U,72U,72U,72U,72U,72U,72U,80U};
    double coherence_sum = 0.0;
    uint32_t used = 0U;
    if (phase_re) *phase_re = 0.0;
    if (phase_im) *phase_im = 0.0;
    for (; first < (int64_t)count; first += frame_length) {
        int64_t d = first;
        uint32_t symbol;
        for (symbol = 0U; symbol < 9U; ++symbol) {
            const uint32_t cp = (uint32_t)lroundf((float)cp_base[symbol] * scale);
            double re = 0.0, im = 0.0, ea = 0.0, eb = 0.0;
            uint32_t j;
            if (d < 0 || d + (int64_t)nfft + cp > (int64_t)count) break;
            for (j = 0U; j < cp; ++j) {
                const wrj_cf32_t a = iq[d + j];
                const wrj_cf32_t b = iq[d + nfft + j];
                re += (double)a.re * b.re + (double)a.im * b.im;
                im += (double)a.re * b.im - (double)a.im * b.re;
                ea += wrj_complex_abs2(a.re, a.im);
                eb += wrj_complex_abs2(b.re, b.im);
            }
            {
                const double magnitude = hypot(re, im);
                const double coherence = magnitude / (sqrt(ea * eb) + 1.0e-20);
                coherence_sum += coherence;
                ++used;
                if (phase_re && phase_im && coherence >= 0.04 && magnitude > 1.0e-20) {
                    const double weight = magnitude * coherence;
                    *phase_re += weight * re / magnitude;
                    *phase_im += weight * im / magnitude;
                }
            }
            d += (int64_t)nfft + cp;
        }
        if (!all_frames) break;
    }
    return used ? (float)(coherence_sum / used) : -1.0f;
}

static float m3_droneid_symbol_quality(const wrj_cf32_t *iq, uint32_t count,
                                       int64_t first, uint32_t frame_length,
                                       uint32_t nfft, uint32_t cp,
                                       m3_workspace_t *workspace)
{
    float values[12];
    uint32_t n = 0U;
    while (first < 0) first += frame_length;
    while (first > (int64_t)frame_length) first -= frame_length;
    for (; first + (int64_t)cp + nfft <= count && n < 12U; first += frame_length) {
        uint32_t j, kept = 0U;
        double real_sum = 0.0, imag_sum = 0.0;
        float cut;
        for (j = 0U; j < nfft; ++j) {
            const wrj_cf32_t z = iq[first + cp + j];
            workspace->fft_re[j] = z.re;
            workspace->fft_im[j] = z.im;
        }
        if (m3_fft_forward_radix2(workspace->fft_re, workspace->fft_im, nfft) != WRJ_OK)
            return 0.0f;
        for (j = 0U; j < nfft; ++j) {
            workspace->metric[j] = workspace->fft_re[j] * workspace->fft_re[j] +
                                   workspace->fft_im[j] * workspace->fft_im[j];
        }
        cut = m3_quantile(workspace->scratch, workspace->metric, nfft, 0.55f);
        for (j = 0U; j < nfft; ++j) {
            if (workspace->metric[j] >= cut && workspace->metric[j] > 1.0e-20f) {
                const double magnitude = sqrt((double)workspace->metric[j]);
                const double ur = workspace->fft_re[j] / magnitude;
                const double ui = workspace->fft_im[j] / magnitude;
                const double squared_re = ur * ur - ui * ui;
                const double squared_im = 2.0 * ur * ui;
                real_sum += squared_re * squared_re - squared_im * squared_im;
                imag_sum += 2.0 * squared_re * squared_im;
                ++kept;
            }
        }
        if (kept >= 16U) values[n++] = (float)(hypot(real_sum, imag_sum) / kept);
    }
    return n ? wrj_clip01(m3_quantile(workspace->scratch, values, n, 0.5f)) : 0.0f;
}

static float m3_droneid_residual_cfo(const wrj_cf32_t *iq, uint32_t count,
                                     int64_t first, uint32_t frame_length,
                                     uint32_t nfft, float scale, float fs)
{
    const int32_t radius = (int32_t)lroundf(0.28f * (float)nfft);
    const int32_t step = WRJ_MAX(2, (int32_t)lroundf((144.0f * scale) / 12.0f));
    int32_t offset, best_offset = 0;
    float best = -1.0f;
    double re = 0.0, im = 0.0;
    for (offset = -radius; offset <= radius; offset += step) {
        const float score = m3_droneid_cp_measure(iq, count, first + offset,
            frame_length, nfft, scale, 1, NULL, NULL);
        if (score > best) { best = score; best_offset = offset; }
    }
    best = -1.0f;
    for (offset = best_offset - step; offset <= best_offset + step; ++offset) {
        const float score = m3_droneid_cp_measure(iq, count, first + offset,
            frame_length, nfft, scale, 1, NULL, NULL);
        if (score > best) { best = score; best_offset = offset; }
    }
    (void)m3_droneid_cp_measure(iq, count, first + best_offset,
        frame_length, nfft, scale, 1, &re, &im);
    return (re == 0.0 && im == 0.0) ? 0.0f :
        (float)(atan2(im, re) * fs / (2.0 * WRJ_PI * nfft));
}

float m3_droneid_frame_residual_cfo(const wrj_cf32_t *iq, uint32_t count,
                                    float sample_rate_hz, const m3_result_t *result)
{
    const float scale = sample_rate_hz / 15360000.0f;
    const uint32_t nfft = (uint32_t)lroundf(1024.0f * scale);
    const uint32_t L = result->frame_length_samples;
    const int32_t radius = (int32_t)lroundf(0.28f * (float)nfft);
    /* MATLAB uses round(min(cpSeq) / 12), where the short CP is 72 base samples. */
    const int32_t step = WRJ_MAX(2, (int32_t)lroundf(72.0f * scale / 12.0f));
    int32_t best_offset = 0, offset;
    float best = -1.0f;
    double re = 0.0, im = 0.0;
    uint32_t end;
    int64_t first;
    if (result->num_frames == 0U || L == 0U) return 0.0f;
    first = result->frame_start_samples_0based[0];
    end = WRJ_MIN(count, result->frame_start_samples_0based[result->num_frames - 1U] + L);
    for (offset = -radius; offset <= radius; offset += step) {
        const float score = m3_droneid_cp_measure(iq, end, first + offset,
            L, nfft, scale, 1, NULL, NULL);
        if (score > best) { best = score; best_offset = offset; }
    }
    best = -1.0f;
    for (offset = best_offset - step; offset <= best_offset + step; ++offset) {
        const float score = m3_droneid_cp_measure(iq, end, first + offset,
            L, nfft, scale, 1, NULL, NULL);
        if (score > best) { best = score; best_offset = offset; }
    }
    (void)m3_droneid_cp_measure(iq, end, first + best_offset,
        L, nfft, scale, 1, &re, &im);
    return (re == 0.0 && im == 0.0) ? 0.0f :
        (float)(atan2(im, re) * sample_rate_hz / (2.0 * WRJ_PI * nfft));
}

static float m3_droneid_absolute_score(const wrj_cf32_t *iq, uint32_t count,
                                        int64_t trial, uint32_t frame_length,
                                        uint32_t nfft, uint32_t cp, float scale,
                                        float fs, uint32_t offset600,
                                        uint32_t offset147, m3_workspace_t *workspace)
{
    float top[3] = {-1.0f, -1.0f, -1.0f};
    uint32_t found = 0U;
    double mean, deviation = 0.0;
    float peak_quality, consistency, symbol_quality, residual, residual_quality;
    while (trial < 0) trial += frame_length;
    while (trial > (int64_t)frame_length) trial -= frame_length;
    for (int64_t start = trial; start < (int64_t)count; start += frame_length) {
        const float cp_score = WRJ_MAX(0.0f, m3_droneid_cp_measure(iq, count,
            start, frame_length, nfft, scale, 0, NULL, NULL));
        const float z600 = m3_droneid_template_score_at(iq, count, start + offset600,
            workspace->spectrum_smooth, workspace->spectrum_weight, nfft);
        const float z147 = m3_droneid_template_score_at(iq, count, start + offset147,
            workspace->spectrum_aux, workspace->power, nfft);
        const float score = 0.46f * cp_score + 0.30f * z600 + 0.24f * z147;
        uint32_t j;
        ++found;
        for (j = 0U; j < 3U; ++j) {
            if (score > top[j]) {
                uint32_t k;
                for (k = 2U; k > j; --k) top[k] = top[k-1U];
                top[j] = score;
                break;
            }
        }
    }
    found = WRJ_MIN(found, 3U);
    if (found == 0U) return -1.0f;
    mean = 0.0;
    for (uint32_t i = 0U; i < found; ++i) mean += top[i];
    mean /= found;
    peak_quality = wrj_clip01((float)mean);
    if (found >= 2U) {
        for (uint32_t i = 0U; i < found; ++i)
            deviation += (top[i] - mean) * (top[i] - mean);
        consistency = wrj_clip01(1.0f -
            (float)(sqrt(deviation / (found - 1U)) / WRJ_MAX(mean, 1.0e-20)));
    } else consistency = 0.5f;
    symbol_quality = m3_droneid_symbol_quality(iq, count, trial, frame_length,
                                               nfft, cp, workspace);
    residual = m3_droneid_residual_cfo(iq, count, trial, frame_length,
                                        nfft, scale, fs);
    residual_quality = wrj_clip01(expf(-powf(fabsf(residual) /
        WRJ_MAX(500.0f, 0.08f * fs / (float)nfft), 2.0f)));
    return wrj_clip01(0.62f * peak_quality + 0.16f * consistency +
                      0.12f * symbol_quality + 0.10f * residual_quality);
}

int32_t m3_refine_droneid_absolute_timing(const wrj_cf32_t *iq, uint32_t count,
                                           float sample_rate_hz, m3_workspace_t *workspace,
                                           m3_result_t *result)
{
    const uint32_t nfft = (uint32_t)lroundf(1024.0f * sample_rate_hz / 15360000.0f);
    const uint32_t cp = (uint32_t)lroundf(72.0f * sample_rate_hz / 15360000.0f);
    const uint32_t L = result->frame_length_samples;
    const float scale = sample_rate_hz / 15360000.0f;
    const uint32_t off600 = (uint32_t)lroundf(3368.0f * scale);
    const uint32_t off147 = (uint32_t)lroundf(5560.0f * scale);
    const int32_t radius = WRJ_MAX(12, (int32_t)lroundf(0.12f * (float)nfft));
    const int32_t step = WRJ_MAX(4, (int32_t)lroundf((float)cp / 8.0f));
    int64_t base;
    int32_t centers[8], offsets[6001], ncenters = 0, n_offsets = 0;
    uint8_t seen[6001] = {0U};
    int32_t best_offset = 0, fine_center;
    float best_score = -1.0f, baseline;
    uint32_t i;
    if (L < 8U || result->num_frames == 0U || nfft > workspace->fft_size)
        return 0;
    base = result->frame_start_samples_0based[0];
    result->original_frame_start = (float)(base + 1);
    result->corrected_frame_start = (float)(base + 1);
    result->frame_offset_correction = 0.0f;
    result->timing_alignment_improved = 0U;
    centers[ncenters++] = 0;
    for (i = 0U; i < 2U; ++i) {
        const int64_t peak = i == 0U ? workspace->droneid_peak600_0based :
                                      workspace->droneid_peak147_0based;
        const int64_t expected = i == 0U ? off600 : off147;
        int64_t center = peak - expected - base;
        center -= (int64_t)llround((double)center / L) * L;
        if (llabs(center) <= 3000) centers[ncenters++] = (int32_t)center;
    }
    {
        int32_t boundary = 0;
        static const uint32_t cp_base[8] = {80U,72U,72U,72U,72U,72U,72U,72U};
        for (i = 0U; i < 8U; ++i) {
            boundary += (int32_t)lroundf((1024.0f + (float)cp_base[i]) * scale);
            if (boundary <= 3000 && ncenters + 2 <= 8) {
                centers[ncenters++] = boundary;
                centers[ncenters++] = -boundary;
            }
        }
    }
    seen[3000] = 1U;
    offsets[n_offsets++] = 0;
    for (int32_t c = 0; c < ncenters; ++c) {
        for (int32_t delta = -radius; delta <= radius; delta += step) {
            const int32_t offset = centers[c] + delta;
            if (offset >= -3000 && offset <= 3000 && !seen[offset + 3000]) {
                seen[offset + 3000] = 1U;
                offsets[n_offsets++] = offset;
            }
        }
    }
    for (int32_t c = 0; c < n_offsets; ++c) {
        const float score = m3_droneid_absolute_score(iq, count, base + offsets[c],
            L, nfft, cp, scale, sample_rate_hz, off600, off147, workspace);
        if (score > best_score) { best_score = score; best_offset = offsets[c]; }
    }
    fine_center = best_offset;
    for (int32_t offset = WRJ_MAX(-3000, fine_center - step);
         offset <= WRJ_MIN(3000, fine_center + step); ++offset) {
        const float score = m3_droneid_absolute_score(iq, count, base + offset,
            L, nfft, cp, scale, sample_rate_hz, off600, off147, workspace);
        if (score > best_score) { best_score = score; best_offset = offset; }
    }
    baseline = m3_droneid_absolute_score(iq, count, base, L, nfft, cp,
        scale, sample_rate_hz, off600, off147, workspace);
    result->offset_search_score = best_score;
    if (best_offset != 0 && best_score >= baseline +
        WRJ_MAX(0.006f, 0.025f * WRJ_MAX(baseline, 0.0f))) {
        result->frame_offset_correction = (float)best_offset;
        result->timing_alignment_improved = 1U;
        result->corrected_frame_start = (float)(base + best_offset + 1);
        return best_offset;
    }
    return 0;
}

static float m3_droneid_timing_score_at(const wrj_cf32_t *iq, uint32_t count,
                                         int64_t start, uint32_t frame_length,
                                         uint32_t nfft, float scale,
                                         uint32_t off600, uint32_t off147,
                                         const m3_workspace_t *workspace)
{
    const float cp_score = WRJ_MAX(0.0f, m3_droneid_cp_measure(iq, count,
        start, frame_length, nfft, scale, 0, NULL, NULL));
    const float z600 = m3_droneid_template_score_at(iq, count, start + off600,
        workspace->spectrum_smooth, workspace->spectrum_weight, nfft);
    const float z147 = m3_droneid_template_score_at(iq, count, start + off147,
        workspace->spectrum_aux, workspace->power, nfft);
    return 0.46f * cp_score + 0.30f * z600 + 0.24f * z147;
}

/* MATLAB m3_estimate_and_compensate_sfo, DroneID branch only. The local
 * offset evidence, robust timing line and acceptance gates all use IQ. */
void m3_estimate_droneid_sfo(const wrj_cf32_t *iq, uint32_t count,
                             float sample_rate_hz, m3_workspace_t *workspace,
                             m3_result_t *result)
{
    const uint32_t frames = WRJ_MIN(result->num_frames, WRJ_MAX_FRAMES);
    const uint32_t L = result->frame_length_samples;
    const float scale = sample_rate_hz / 15360000.0f;
    const uint32_t nfft = (uint32_t)lroundf(1024.0f * scale);
    const uint32_t off600 = (uint32_t)lroundf(3368.0f * scale);
    const uint32_t off147 = (uint32_t)lroundf(5560.0f * scale);
    const int32_t radius = 28;
    float offsets[WRJ_MAX_FRAMES], best[WRJ_MAX_FRAMES], base[WRJ_MAX_FRAMES];
    float residuals[WRJ_MAX_FRAMES], absolute[WRJ_MAX_FRAMES];
    float candidate_scores[WRJ_MAX_FRAMES];
    double intercept = 0.0, slope = 0.0, center, robust_scale;
    uint32_t i, iteration, inlier_count = 0U;
    float ppm, drift, retention, candidate_retention;
    result->estimated_sfo_ppm = NAN;
    result->frame_drift_samples = 0.0f;
    result->timing_slope = 0.0f;
    result->sfo_correction_applied = 0U;
    if (frames < 4U || L < 8U || nfft > workspace->fft_size) return;
    for (i = 0U; i < frames; ++i) {
        const int64_t start = result->frame_start_samples_0based[i];
        float optimum = -1.0f;
        int32_t best_offset = 0;
        for (int32_t delta = -radius; delta <= radius; ++delta) {
            const float score = m3_droneid_timing_score_at(iq, count, start + delta,
                L, nfft, scale, off600, off147, workspace);
            const float objective = score - 0.015f * (float)abs(delta) / (float)radius;
            if (delta == 0) base[i] = score;
            if (objective > optimum) {
                optimum = objective;
                best_offset = delta;
                best[i] = score;
            }
        }
        offsets[i] = (float)best_offset;
    }
    {
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (i = 0U; i < frames; ++i) {
            sx += i; sy += offsets[i]; sxx += (double)i*i; sxy += (double)i*offsets[i];
        }
        {
            const double den = frames*sxx - sx*sx;
            slope = fabs(den) > 1.0e-20 ? (frames*sxy - sx*sy)/den : 0.0;
            intercept = (sy - slope*sx)/frames;
        }
    }
    for (iteration = 0U; iteration < 5U; ++iteration) {
        double sw=0.0, sx=0.0, sy=0.0, sxx=0.0, sxy=0.0;
        for (i = 0U; i < frames; ++i)
            residuals[i] = offsets[i] - (float)(intercept + slope*i);
        center = m3_quantile(workspace->scratch, residuals, frames, 0.5f);
        for (i = 0U; i < frames; ++i) absolute[i] = (float)fabs(residuals[i] - center);
        robust_scale = 1.4826 * m3_quantile(workspace->scratch, absolute, frames, 0.5f) + 1.0e-12;
        for (i = 0U; i < frames; ++i) {
            const double u = fabs(residuals[i] - center) / (1.5 * robust_scale);
            const double w = u > 1.0 ? 1.0/u : 1.0;
            sw += w; sx += w*i; sy += w*offsets[i];
            sxx += w*(double)i*i; sxy += w*i*offsets[i];
        }
        {
            const double den = sw*sxx - sx*sx;
            if (fabs(den) > 1.0e-20) {
                slope = (sw*sxy - sx*sy)/den;
                intercept = (sy - slope*sx)/sw;
            }
        }
    }
    for (i = 0U; i < frames; ++i)
        residuals[i] = offsets[i] - (float)(intercept + slope*i);
    center = m3_quantile(workspace->scratch, residuals, frames, 0.5f);
    for (i = 0U; i < frames; ++i) absolute[i] = (float)fabs(residuals[i] - center);
    robust_scale = 1.4826 * m3_quantile(workspace->scratch, absolute, frames, 0.5f) + 1.0e-12;
    {
        const double threshold = WRJ_MAX(2.0, 2.5*robust_scale);
        uint32_t retained = 0U;
        for (i = 0U; i < frames; ++i) {
            if (fabs(residuals[i] - center) <= threshold)
                absolute[retained++] = fabsf(residuals[i]);
        }
        inlier_count = retained;
    }
    ppm = (float)(1.0e6*slope/L);
    drift = (float)(slope*(frames-1U));
    result->estimated_sfo_ppm = ppm;
    result->frame_drift_samples = drift;
    result->timing_slope = (float)slope;
    retention = m3_quantile(workspace->scratch, best, frames, 0.5f) /
                WRJ_MAX(m3_quantile(workspace->scratch, base, frames, 0.5f), 1.0e-20f);
    if (inlier_count < WRJ_MAX(3U, (uint32_t)ceilf(0.60f * (float)frames)) ||
        fabsf(ppm) < 3.0f || fabsf(ppm) > 250.0f ||
        fabsf(drift) < 0.5f || fabsf(drift) > 24.0f ||
        m3_quantile(workspace->scratch, absolute, inlier_count, 0.5f) >
            WRJ_MAX(2.0f, 0.12f * 144.0f * scale) || retention <= 0.0f)
        return;
    for (i = 0U; i < frames; ++i) {
        const int32_t correction = (int32_t)lround(slope*i);
        const int64_t next = (int64_t)result->frame_start_samples_0based[i] + correction;
        if (next < 0 || next >= count) return;
        candidate_scores[i] = m3_droneid_timing_score_at(iq, count, next,
            L, nfft, scale, off600, off147, workspace);
    }
    candidate_retention = m3_quantile(workspace->scratch, candidate_scores, frames, 0.5f) /
        WRJ_MAX(m3_quantile(workspace->scratch, base, frames, 0.5f), 1.0e-20f);
    if (candidate_retention < 0.98f) return;
    for (i = 0U; i < frames; ++i) {
        result->frame_start_samples_0based[i] = (uint32_t)((int64_t)
            result->frame_start_samples_0based[i] + (int32_t)lround(slope*i));
    }
    result->sfo_correction_applied = 1U;
}

wrj_status_t m3_remoteid_ble_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                         float sample_rate_hz, uint32_t max_frames,
                                         m3_workspace_t *workspace, m3_result_t *result)
{
    static const uint8_t access_bytes[4] = {0xD6U, 0xBEU, 0x89U, 0x8EU};
    static const uint8_t fixed_bytes[7] = {0x42U, 37U, 30U, 0x16U, 0xFAU, 0xFFU, 0x0DU};
    static const uint8_t fixed_byte_positions[7] = {0U, 1U, 8U, 9U, 10U, 11U, 12U};
    int8_t expected[40];
    uint16_t known_positions[96];
    int8_t known_signs[96];
    uint8_t known_count = 0U;
    uint8_t whiten_mask[336] = {0U};
    const uint32_t sps = WRJ_MAX(4U, (uint32_t)lroundf(sample_rate_hz / 1000000.0f));
    uint32_t phase;
    uint32_t candidate_count = 0U;
    uint32_t selected = 0U;
    uint32_t index;
    uint32_t recovery_locations[96];
    uint32_t recovery_count = 0U;
    uint32_t score_count = 0U;
    float threshold;
    float peak = 0.0f;
    float repeat_score = 0.0f;
    const wrj_cf32_t *signal = workspace->baseband;
    const uint32_t min_separation = (uint32_t)lroundf(0.45e-3f * sample_rate_hz);
    M3_PERF_TIMER(sync_timer);
    M3_PERF_TIMER(aa_timer);
    M3_PERF_TIMER(phase_timer);
    M3_PERF_TIMER(recovery_timer);
    M3_PERF_TIMER(alignment_timer);
#ifdef WRJ_ENABLE_PROFILING
    uint64_t hypothesis_count = 0U;
#endif
    if (count < 64U * sps) {
        return WRJ_ERR_DATA;
    }
    workspace->ble_soft_observation_count = 0U;
    M3_PERF_START(sync_timer);
    M3_PERF_START(aa_timer);
    /* Fixed receiver-side anti-noise smoothing, equivalent to the bounded
     * BLE low-pass branch in MATLAB.  The window depends only on samples per
     * symbol and never on candidate identity or decoded content. */
    {
        const uint32_t radius = WRJ_MAX(1U, sps / 7U);
        double sum_re = 0.0;
        double sum_im = 0.0;
        uint32_t right = 0U;
        uint32_t left = 0U;
        for (index = 0U; index < count; ++index) {
            const uint32_t wanted_right = WRJ_MIN(count - 1U, index + radius);
            const uint32_t wanted_left = index > radius ? index - radius : 0U;
            while (right <= wanted_right) {
                sum_re += iq[right].re;
                sum_im += iq[right].im;
                ++right;
            }
            while (left < wanted_left) {
                sum_re -= iq[left].re;
                sum_im -= iq[left].im;
                ++left;
            }
            workspace->baseband[index].re = (float)(sum_re / (double)(right - left));
            workspace->baseband[index].im = (float)(sum_im / (double)(right - left));
        }
    }
    for (index = 0U; index < 8U; ++index) {
        expected[index] = (int8_t)((index & 1U) != 0U ? 1 : -1);
    }
    for (index = 0U; index < 32U; ++index) {
        expected[8U + index] = (int8_t)(((access_bytes[index / 8U] >> (index & 7U)) & 1U) != 0U ? 1 : -1);
    }
    for (index = 0U; index < 40U; ++index) {
        known_positions[known_count] = (uint16_t)index;
        known_signs[known_count++] = expected[index];
    }
    m3_ble_dewhiten(whiten_mask, 336U);
    for (index = 0U; index < 7U; ++index) {
        uint32_t bit;
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t raw_position = (uint32_t)fixed_byte_positions[index] * 8U + bit;
            const uint8_t raw_bit = (uint8_t)((fixed_bytes[index] >> bit) & 1U);
            const uint8_t whitened = raw_bit ^ whiten_mask[raw_position];
            known_positions[known_count] = (uint16_t)(40U + raw_position);
            known_signs[known_count++] = whitened != 0U ? 1 : -1;
        }
    }
    m3_ble_lagged_discriminator(signal, count, sps, workspace->metric,
                                result->ble_soft_recovery_enabled != 0U);
    M3_PERF_START(phase_timer);
    for (phase = 0U; phase < sps; ++phase) {
        const uint32_t symbols = (count - 1U - phase) / sps;
        uint32_t symbol;
        if (symbols < 160U) {
            continue;
        }
        M3_PERF_COUNT(M3_PERF_OP_BLE_SAMPLING_PHASES, 1U);
        for (symbol = 0U; symbol < symbols; ++symbol) {
            uint32_t q;
            double sum = 0.0;
            const uint32_t begin = phase + symbol * sps + sps / 4U;
            const uint32_t end = WRJ_MIN(count - 1U, begin + WRJ_MAX(1U, sps / 2U));
            for (q = begin; q < end; ++q) {
                sum += workspace->metric[q];
            }
            workspace->scratch[symbol] = (float)(sum / (double)WRJ_MAX(1U, end - begin));
        }
        for (symbol = 0U; symbol + 160U <= symbols; ++symbol) {
            uint32_t bit;
            double dot = 0.0;
            double energy = 0.0;
            for (bit = 0U; bit < known_count; ++bit) {
                const float value = workspace->scratch[symbol + known_positions[bit]];
                dot += (double)known_signs[bit] * value;
                energy += (double)value * value;
            }
            if (energy > 1.0e-12) {
                const float score = (float)(fabs(dot) / sqrt((double)known_count * energy));
                const uint32_t location = phase + symbol * sps;
                workspace->metric[location] = score;
                workspace->scratch[score_count++] = score;
                peak = WRJ_MAX(peak, score);
            }
        }
    }
    M3_PERF_STOP(M3_PERF_BLE_PHASE_SEARCH, phase_timer);
    if (score_count == 0U) {
        M3_PERF_STOP(M3_PERF_BLE_PREAMBLE_AA, aa_timer);
        M3_PERF_STOP(M3_PERF_BLE_SYNC, sync_timer);
        return WRJ_ERR_DATA;
    }
    /* Match the MATLAB receiver's robust candidate policy: the absolute
     * floor rejects unstructured noise, while the upper-tail quantile keeps
     * weak but repeatable advertisements.  This is an internal acquisition
     * threshold; the public 0.80 acceptance threshold is unchanged. */
    if (result->ble_soft_recovery_enabled != 0U) {
        threshold = WRJ_MAX(0.055f, WRJ_MIN(
            m3_quantile(workspace->scratch, workspace->scratch, score_count, 0.995f),
            0.30f * peak));
    } else {
        threshold = WRJ_MAX(0.12f,
            m3_quantile(workspace->scratch, workspace->scratch, score_count, 0.995f));
    }
    for (phase = 0U; phase < sps; ++phase) {
        const uint32_t symbols = (count - 1U - phase) / sps;
        uint32_t symbol;
        for (symbol = 0U; symbol + 160U <= symbols; ++symbol) {
            const uint32_t location = phase + symbol * sps;
            const float score = workspace->metric[location];
            if (score >= threshold && candidate_count < workspace->peak_capacity) {
                workspace->peak_candidates[candidate_count].index = location;
                workspace->peak_candidates[candidate_count].value = score;
                ++candidate_count;
            }
        }
    }
    if (candidate_count == 0U) {
        M3_PERF_STOP(M3_PERF_BLE_PREAMBLE_AA, aa_timer);
        M3_PERF_STOP(M3_PERF_BLE_SYNC, sync_timer);
        return WRJ_ERR_DATA;
    }
    M3_PERF_COUNT(M3_PERF_OP_CORRELATION_CALLS, score_count);
    M3_PERF_COUNT(M3_PERF_OP_CORRELATION_EVALUATIONS,
                  (uint64_t)score_count * (uint64_t)known_count);
    M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES, score_count);
    qsort(workspace->peak_candidates, candidate_count, sizeof(m3_peak_t), m3_peak_value_desc);
    /* Packet recovery deliberately keeps a denser finite bank than the
     * frame-grid output.  CRC24, not correlation alone, decides which of
     * these low-SNR hypotheses is valid. */
    for (index = 0U; index < candidate_count && recovery_count < 96U; ++index) {
        uint32_t other;
        int separated = 1;
        for (other = 0U; other < recovery_count; ++other) {
            const uint32_t a = recovery_locations[other];
            const uint32_t b = workspace->peak_candidates[index].index;
            const uint32_t distance = a > b ? a - b : b - a;
            if (distance < 4U * sps) {
                separated = 0;
                break;
            }
        }
        if (separated != 0) {
            recovery_locations[recovery_count++] = workspace->peak_candidates[index].index;
        }
    }
    M3_PERF_STOP(M3_PERF_BLE_PREAMBLE_AA, aa_timer);
    M3_PERF_START(recovery_timer);
    if (recovery_count != 0U) {
        if (result->ble_soft_recovery_enabled != 0U) {
            /* MATLAB's weak-signal demodulator integrates the same coherent,
             * multi-lag phase discriminator used for acquisition. */
            m3_ble_lagged_discriminator(signal, count, sps, workspace->scratch, 1);
            for (index = 0U; index < count; ++index)
                workspace->ble_phase_delta[index] = workspace->scratch[index];
        } else {
            const uint32_t lag = WRJ_MAX(1U, sps / 4U);
            for (index = 0U; index + lag < count; ++index) {
                const wrj_cf32_t a = signal[index];
                const wrj_cf32_t b = signal[index + lag];
                workspace->ble_phase_delta[index] =
                    atan2((double)a.re * b.im - (double)a.im * b.re,
                          (double)a.re * b.re + (double)a.im * b.im) / (double)lag;
            }
        }
    }
    for (index = 0U; index < recovery_count; ++index) {
        static const float window_fractions[3] = {0.22f, 0.32f, 0.42f};
        const int32_t shift_step = (int32_t)WRJ_MAX(1U, sps / 8U);
        uint32_t window_index;
        int crc_ok = 0;
        int32_t best_shift = 0;
        int soft_recovered = 0;
        uint8_t recovered_pdu[39] = {0U};
        uint8_t received_raw[336] = {0U};
        uint32_t recovered_crc = 0U;
        float best_soft[336], best_soft_score = -1.0f;
        for (window_index = 0U; window_index < 3U && crc_ok == 0; ++window_index) {
            const uint32_t half_window = WRJ_MAX(1U,
                (uint32_t)lroundf(window_fractions[window_index] * (float)sps));
            int32_t shift;
            for (shift = -(int32_t)sps; shift <= (int32_t)sps; shift += shift_step) {
                ++result->crc_attempt_count;
#ifdef WRJ_ENABLE_PROFILING
                ++hypothesis_count;
#endif
                if (m3_ble_crc_attempt(workspace->ble_phase_delta, count, recovery_locations[index], sps,
                                       shift, half_window, expected, whiten_mask, &best_shift,
                                       0, recovered_pdu, &recovered_crc,
                                       &soft_recovered, NULL,received_raw) != 0) {
                    ++result->crc_success_count;
                    crc_ok = 1;
                    break;
                }
            }
        }
        if (crc_ok == 0 && result->ble_soft_recovery_enabled != 0U &&
            index < 8U && workspace->metric[recovery_locations[index]] >= 0.70f) {
            const int32_t offsets[3] = {0, -(int32_t)WRJ_MAX(1U, sps / 4U),
                                        (int32_t)WRJ_MAX(1U, sps / 4U)};
            for (window_index = 0U; window_index < 3U && crc_ok == 0; ++window_index) {
                const uint32_t half_window = WRJ_MAX(1U,
                    (uint32_t)lroundf(window_fractions[window_index] * (float)sps));
                uint32_t offset_index;
                for (offset_index = 0U; offset_index < 3U; ++offset_index) {
                    float trial_soft[336] = {0.0f};
                    uint32_t fixed_index, matched = 0U;
                    float structure_score;
                    int recovered;
                    ++result->crc_attempt_count;
                    recovered = m3_ble_crc_attempt(workspace->ble_phase_delta, count,
                            recovery_locations[index], sps, offsets[offset_index],
                            half_window, expected, whiten_mask, &best_shift,
                            1, recovered_pdu, &recovered_crc, &soft_recovered,
                            trial_soft,received_raw);
                    if (fabsf(trial_soft[0]) + fabsf(trial_soft[1]) < 1.0e-8f)
                        continue;
                    for (fixed_index = 0U; fixed_index < 7U; ++fixed_index) {
                        static const uint8_t values[7] = {0x42U,37U,30U,0x16U,0xFAU,0xFFU,0x0DU};
                        static const uint8_t positions[7] = {0U,1U,8U,9U,10U,11U,12U};
                        uint32_t q;
                        for (q = 0U; q < 8U; ++q) {
                            const uint32_t bit = (uint32_t)positions[fixed_index] * 8U + q;
                            matched += (trial_soft[bit] > 0.0f) ==
                                (((values[fixed_index] >> q) & 1U) != 0U);
                        }
                    }
                    structure_score = (float)matched / 56.0f;
                    if (structure_score > best_soft_score) {
                        memcpy(best_soft, trial_soft, sizeof(best_soft));
                        best_soft_score = structure_score;
                    }
                    if (recovered) {
                        ++result->crc_success_count;
                        result->ble_soft_recovered_count += (uint16_t)soft_recovered;
                        crc_ok = 1;
                        break;
                    }
                }
            }
        }
        if (result->ble_soft_recovery_enabled != 0U && index < 8U &&
            best_soft_score >= 0.60f && workspace->ble_soft_observation_count < 8U) {
            const uint8_t n = workspace->ble_soft_observation_count++;
            workspace->ble_soft_start[n] = recovery_locations[index];
            workspace->ble_soft_score[n] = best_soft_score *
                workspace->metric[recovery_locations[index]];
            memcpy(workspace->ble_soft_bits[n], best_soft, sizeof(best_soft));
        }
        if (crc_ok != 0 && recovered_pdu[1] == 37U) {
            uint16_t old;
            for (old = 0U; old < result->ble_verified_packet_count; ++old) {
                if (memcmp(result->ble_verified_pdu[old], recovered_pdu, 39U) == 0) break;
            }
            if (old == result->ble_verified_packet_count && old < WRJ_MAX_PACKETS) {
                memcpy(result->ble_verified_pdu[old], recovered_pdu, 39U);
                memcpy(result->ble_verified_raw_bits[old],received_raw,336U);
                result->ble_verified_crc[old] = recovered_crc;
                result->ble_verified_start[old] = recovery_locations[index];
                result->ble_verified_confidence[old] =
                    workspace->metric[recovery_locations[index]];
                ++result->ble_verified_packet_count;
            }
        }
        if (crc_ok != 0 && result->crc_success_count == 1U) {
            result->symbol_timing_offset = (float)best_shift;
        }
    }
    M3_PERF_COUNT(M3_PERF_OP_BLE_HYPOTHESES, hypothesis_count);
    M3_PERF_STOP(M3_PERF_BLE_LOW_SNR_RECOVERY, recovery_timer);
    M3_PERF_START(alignment_timer);
    for (index = 0U; index < candidate_count && selected < WRJ_MIN(max_frames, WRJ_MAX_FRAMES); ++index) {
        uint32_t other;
        int separated = 1;
        for (other = 0U; other < selected; ++other) {
            const uint32_t distance = workspace->peak_selected[other].index > workspace->peak_candidates[index].index ?
                workspace->peak_selected[other].index - workspace->peak_candidates[index].index :
                workspace->peak_candidates[index].index - workspace->peak_selected[other].index;
            if (distance < min_separation) {
                separated = 0;
                break;
            }
        }
        if (separated != 0) {
            workspace->peak_selected[selected++] = workspace->peak_candidates[index];
        }
    }
    qsort(workspace->peak_selected, selected, sizeof(m3_peak_t), m3_peak_index_asc);
    M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES, candidate_count);
    M3_PERF_STOP(M3_PERF_FRAME_ALIGNMENT, alignment_timer);
    for (index = 0U; index < selected; ++index) {
        result->frame_start_samples_0based[index] = workspace->peak_selected[index].index;
        result->frame_confidence[index] = workspace->peak_selected[index].value;
    }
    result->num_frames = selected;
    result->frame_length_samples = selected > 1U ?
        result->frame_start_samples_0based[1] - result->frame_start_samples_0based[0] : 376U * sps;
    result->peak_metric = workspace->peak_candidates[0].value;
    result->access_address_confidence = result->peak_metric;
    if (selected >= 3U) {
        double mean_gap = 0.0;
        double variance = 0.0;
        for (index = 1U; index < selected; ++index) {
            mean_gap += (double)(result->frame_start_samples_0based[index] -
                                 result->frame_start_samples_0based[index - 1U]);
        }
        mean_gap /= (double)(selected - 1U);
        for (index = 1U; index < selected; ++index) {
            const double gap = (double)(result->frame_start_samples_0based[index] -
                                        result->frame_start_samples_0based[index - 1U]);
            variance += (gap - mean_gap) * (gap - mean_gap);
        }
        variance /= (double)(selected - 1U);
        repeat_score = wrj_clip01(1.0f - (float)(sqrt(variance) / WRJ_MAX(mean_gap, 1.0)));
    }
    result->ble_sync_confidence = wrj_clip01(
        0.72f * wrj_clip01((result->peak_metric - 0.08f) / 0.42f) + 0.28f * repeat_score);
    if (result->crc_success_count > 0U) {
        const float crc_evidence = wrj_clip01((float)result->crc_success_count / 2.0f);
        result->ble_sync_confidence = WRJ_MAX(result->ble_sync_confidence,
            wrj_clip01(0.82f + 0.16f * crc_evidence));
    }
    result->sync_confidence = result->ble_sync_confidence;
    if (result->crc_success_count == 0U) {
        result->symbol_timing_offset = (float)(workspace->peak_candidates[0].index % sps);
    }
    snprintf(result->sync_method, sizeof(result->sync_method),
             "BLE_1M_preamble_access_address_multiphase");
    M3_PERF_STOP(M3_PERF_BLE_SYNC, sync_timer);
    return WRJ_OK;
}

wrj_status_t m3_burst_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                  float sample_rate_hz, uint32_t max_frames,
                                  m3_workspace_t *workspace, m3_result_t *result)
{
    const uint32_t length = 256U;
    const uint32_t hop = 128U;
    const uint32_t blocks = count > length ? (count - length) / hop + 1U : 0U;
    uint32_t block;
    uint32_t selected = 0U;
    float q20;
    float q80;
    float threshold;
    uint32_t previous = UINT32_MAX;
    if (blocks < 6U) {
        return WRJ_ERR_DATA;
    }
    M3_PERF_COUNT(M3_PERF_OP_FRAME_CANDIDATES, blocks);
    for (block = 0U; block < blocks; ++block) {
        uint32_t sample;
        double energy = 0.0;
        for (sample = 0U; sample < length; ++sample) {
            energy += wrj_complex_abs2(iq[block * hop + sample].re, iq[block * hop + sample].im);
        }
        workspace->metric[block] = 10.0f * log10f((float)(energy / length) + 1.0e-20f);
    }
    q20 = m3_quantile(workspace->scratch, workspace->metric, blocks, 0.20f);
    q80 = m3_quantile(workspace->scratch, workspace->metric, blocks, 0.80f);
    threshold = q20 + 0.35f * (q80 - q20);
    for (block = 1U; block < blocks && selected < WRJ_MIN(max_frames, WRJ_MAX_FRAMES); ++block) {
        if (workspace->metric[block] > threshold && workspace->metric[block - 1U] <= threshold) {
            const uint32_t start = block * hop;
            if (previous == UINT32_MAX || start - previous >= (uint32_t)lroundf(0.0004f * sample_rate_hz)) {
                result->frame_start_samples_0based[selected] = start;
                result->frame_confidence[selected] = wrj_clip01((q80 - q20) / 8.0f);
                previous = start;
                ++selected;
            }
        }
    }
    result->num_frames = selected;
    result->frame_length_samples = selected > 1U ?
        result->frame_start_samples_0based[1] - result->frame_start_samples_0based[0] : length;
    result->peak_metric = q80 - q20;
    result->sync_confidence = wrj_clip01((q80 - q20) / 8.0f);
    snprintf(result->sync_method, sizeof(result->sync_method), "envelope_activity_edges");
    return selected > 0U ? WRJ_OK : WRJ_ERR_DATA;
}

wrj_status_t m3_blind_synchronize(const wrj_cf32_t *iq, uint32_t count,
                                  float sample_rate_hz, uint32_t max_frames,
                                  m3_workspace_t *workspace, m3_result_t *result)
{
    static const uint32_t base_lags[] = {256U, 512U, 768U, 1024U, 1152U, 2048U, 2192U};
    uint32_t lag_index;
    float best_score = -1.0f;
    m3_result_t best;
    memset(&best, 0, sizeof(best));
    for (lag_index = 0U; lag_index < WRJ_ARRAY_COUNT(base_lags); ++lag_index) {
        const uint32_t nfft = (uint32_t)lroundf((float)base_lags[lag_index] *
                                                sample_rate_hz / 30720000.0f);
        const float ratios[] = {1.0f / 16.0f, 0.10f, 1.0f / 8.0f};
        uint32_t ratio;
        for (ratio = 0U; ratio < WRJ_ARRAY_COUNT(ratios); ++ratio) {
            const uint32_t cp = WRJ_MAX(16U, (uint32_t)lroundf((float)nfft * ratios[ratio]));
            m3_result_t trial;
            float score;
            memset(&trial, 0, sizeof(trial));
            if (m3_cp_synchronize(iq, count, nfft, cp, max_frames, workspace, &trial) != WRJ_OK) {
                continue;
            }
            score = trial.sync_confidence * trial.peak_metric;
            if (score > best_score) {
                best_score = score;
                best = trial;
            }
        }
    }
    if (best_score < 0.12f) {
        return WRJ_ERR_DATA;
    }
    *result = best;
    snprintf(result->sync_method, sizeof(result->sync_method), "blind_CP_repetition");
    return WRJ_OK;
}

void m3_estimate_sfo(const wrj_cf32_t *iq, uint32_t count, uint32_t nfft,
                     uint32_t cp_samples, m3_result_t *result)
{
    float ppm[WRJ_MAX_FRAMES];
    uint32_t observations = 0U;
    uint32_t index;
    result->estimated_sfo_ppm = NAN;
    result->frame_drift_samples = 0.0f;
    result->timing_slope = 0.0f;
    result->sfo_correction_applied = 0U;
    if (result->num_frames < 4U || result->frame_length_samples < 8U) {
        return;
    }
    if (iq != NULL && nfft >= 8U && cp_samples >= 4U &&
        result->frame_length_samples > 5U * nfft) {
        float offsets[WRJ_MAX_FRAMES];
        float scores[WRJ_MAX_FRAMES];
        float base_scores[WRJ_MAX_FRAMES];
        double sx = 0.0;
        double sy = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        const uint32_t frames = result->num_frames;
        for (index = 0U; index < frames; ++index) {
            int32_t offset;
            float best = -1.0f;
            int32_t best_offset = 0;
            const int64_t start = result->frame_start_samples_0based[index];
            for (offset = -24; offset <= 24; ++offset) {
                uint32_t q;
                double sr = 0.0;
                double si = 0.0;
                double ea = 0.0;
                double eb = 0.0;
                const int64_t probe = start + offset;
                float coherence;
                if (probe < 0 || probe + (int64_t)nfft + cp_samples > (int64_t)count) {
                    continue;
                }
                for (q = 0U; q < cp_samples; ++q) {
                    const wrj_cf32_t a = iq[probe + q];
                    const wrj_cf32_t b = iq[probe + q + nfft];
                    sr += (double)a.re * b.re + (double)a.im * b.im;
                    si += (double)a.re * b.im - (double)a.im * b.re;
                    ea += wrj_complex_abs2(a.re, a.im);
                    eb += wrj_complex_abs2(b.re, b.im);
                }
                coherence = (float)(sqrt(sr * sr + si * si) / (sqrt(ea * eb) + 1.0e-20));
                if (offset == 0) {
                    base_scores[index] = coherence;
                }
                if (coherence - 0.0006f * (float)abs(offset) > best) {
                    best = coherence - 0.0006f * (float)abs(offset);
                    best_offset = offset;
                }
            }
            offsets[index] = (float)best_offset;
            scores[index] = best;
            sx += index;
            sy += offsets[index];
            sxx += (double)index * index;
            sxy += (double)index * offsets[index];
        }
        {
            const double denominator = (double)frames * sxx - sx * sx;
            const double slope = fabs(denominator) > 1.0e-12 ?
                ((double)frames * sxy - sx * sy) / denominator : 0.0;
            double residual_sum = 0.0;
            double best_sum = 0.0;
            double base_sum = 0.0;
            for (index = 0U; index < frames; ++index) {
                const double intercept = (sy - slope * sx) / (double)frames;
                residual_sum += fabs(offsets[index] - (float)(intercept + slope * index));
                best_sum += scores[index];
                base_sum += base_scores[index];
            }
            result->timing_slope = (float)slope;
            result->estimated_sfo_ppm = (float)(1.0e6 * slope /
                (double)result->frame_length_samples);
            result->frame_drift_samples = (float)(slope * (double)(frames - 1U));
            if (fabsf(result->estimated_sfo_ppm) >= 3.0f &&
                fabsf(result->estimated_sfo_ppm) <= 250.0f &&
                fabsf(result->frame_drift_samples) <= 24.0f &&
                residual_sum / frames <= 2.0 && best_sum >= 0.98 * base_sum) {
                for (index = 0U; index < frames; ++index) {
                    const int32_t correction = (int32_t)lround(slope * (double)index);
                    const int64_t corrected = (int64_t)result->frame_start_samples_0based[index] + correction;
                    if (corrected >= 0 && corrected < (int64_t)count) {
                        result->frame_start_samples_0based[index] = (uint32_t)corrected;
                    }
                }
                result->sfo_correction_applied = 1U;
            }
        }
        return;
    }
    for (index = 1U; index < result->num_frames; ++index) {
        const float gap = (float)(result->frame_start_samples_0based[index] -
                                  result->frame_start_samples_0based[index - 1U]);
        const float steps = WRJ_MAX(1.0f, roundf(gap / (float)result->frame_length_samples));
        const float value = 1.0e6f * (gap / (steps * (float)result->frame_length_samples) - 1.0f);
        if (fabsf(value) <= 250.0f) {
            ppm[observations++] = value;
        }
    }
    if (observations >= 3U) {
        qsort(ppm, observations, sizeof(float), m3_float_compare);
        result->estimated_sfo_ppm = ppm[observations / 2U];
        result->timing_slope = result->estimated_sfo_ppm *
            (float)result->frame_length_samples / 1.0e6f;
        result->frame_drift_samples = result->timing_slope * (float)(result->num_frames - 1U);
    }
}
