#include "m4_module4.h"
#include "m3_module3.h"

#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "test failure: %s at line %d\n", #condition, __LINE__); \
    exit(1); \
} } while (0)

static void test_crc24(void)
{
    const uint8_t bits[8] = {1U,0U,1U,0U,1U,0U,1U,0U};
    uint8_t changed[8];
    memcpy(changed, bits, sizeof(bits));
    changed[3] ^= 1U;
    CHECK(m4_crc24_ble(bits, 0U) == 0x555555U);
    CHECK(m4_crc24_ble(bits, 8U) != m4_crc24_ble(changed, 8U));
}

static void test_bit_pack(void)
{
    const uint8_t bits[16] = {1U,0U,1U,0U,0U,1U,0U,1U,
                              0U,1U,0U,1U,1U,0U,1U,0U};
    uint8_t bytes[2];
    CHECK(m4_pack_bits_msb(bits, 16U, bytes, 2U) == 2U);
    CHECK(bytes[0] == 0xA5U && bytes[1] == 0x5AU);
}

static void test_qpsk(void)
{
    uint8_t bi, bq;
    float ci, cq;
    m4_qpsk_decide(-1.0f, 1.0f, &bi, &bq, &ci, &cq);
    CHECK(bi == 1U && bq == 0U);
    CHECK(ci > .9f && cq > .9f);
}

static void test_fft_carrier_mapping(void)
{
    float re[32], im[32];
    uint32_t i;
    const uint32_t positive_carrier = 5U;
    for (i = 0U; i < 32U; ++i) {
        const float phase = 2.0f * (float)WRJ_PI * (float)(positive_carrier * i) / 32.0f;
        re[i] = cosf(phase);
        im[i] = sinf(phase);
    }
    CHECK(m3_fft_forward_radix2(re, im, 32U) == WRJ_OK);
    CHECK(re[positive_carrier] > 31.9f);
    CHECK(fabsf(im[positive_carrier]) < .01f);
    /* MATLAB fftshift: positive carrier +5 appears at zero-based 16+5. */
    CHECK(((16U + positive_carrier + 16U) % 32U) == positive_carrier);
}

static void test_ble_whitening(void)
{
    uint8_t bits[64], original[64];
    uint32_t i;
    for (i = 0U; i < 64U; ++i) bits[i] = (uint8_t)((i * 7U) & 1U);
    memcpy(original, bits, sizeof(bits));
    m4_ble_whiten(bits, 64U, 38U);
    CHECK(memcmp(bits, original, sizeof(bits)) != 0);
    m4_ble_whiten(bits, 64U, 38U);
    CHECK(memcmp(bits, original, sizeof(bits)) == 0);
}

static void test_field_parser(void)
{
    m4_result_t result;
    m4_ble_packet_t packets[2];
    memset(&result, 0, sizeof(result));
    memset(packets, 0, sizeof(packets));
    result.latitude_deg = NAN;
    result.longitude_deg = NAN;
    packets[0].pdu[12] = 0x0DU;
    packets[0].message_type = 0U;
    packets[0].confidence = .9f;
    memcpy(packets[0].message + 2U, "TEST-UAS", 8U);
    packets[1].pdu[12] = 0x0DU;
    packets[1].message_type = 1U;
    packets[1].confidence = .8f;
    /* latitude 31.0 degrees, longitude 121.0 degrees, little endian. */
    packets[1].message[5] = 0x80U; packets[1].message[6] = 0xC2U;
    packets[1].message[7] = 0x79U; packets[1].message[8] = 0x12U;
    packets[1].message[9] = 0x80U; packets[1].message[10] = 0x7BU;
    packets[1].message[11] = 0x1FU; packets[1].message[12] = 0x48U;
    m4_parse_remoteid_messages(packets, 2U, &result);
    CHECK(result.parse_complete == 1U);
    CHECK(strcmp(result.uas_id, "TEST-UAS") == 0);
    CHECK(result.field_count >= 3U);
}

int main(void)
{
    test_crc24();
    test_bit_pack();
    test_qpsk();
    test_fft_carrier_mapping();
    test_ble_whitening();
    test_field_parser();
    puts("module4 tests passed");
    return 0;
}
