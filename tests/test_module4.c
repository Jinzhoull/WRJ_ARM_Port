#include "m4_module4.h"
#include "m4_matlab_flow.h"
#include "m4_matlab_statistics.h"
#include "m4_module5_handoff.h"
#include "wrj/module4/wideband_assembler.h"
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

static void test_matlab_validation_flow(void)
{
    static const double matlab_rand[5] = {
        .80404314467249449, .45037576650349764, .96393630770079675,
        .13443055361141576, .24561988333458729};
    m4_matlab_rng_t rng;
    m4_matlab_frame_t frame;
    uint8_t raw[96];
    uint8_t changed[64];
    uint32_t i;
    for (i = 0U; i < 96U; ++i) raw[i] = (uint8_t)i;
    CHECK(m4_matlab_stable_string_hash("sim_dji_wideband_018") != 0U);
    CHECK(m4_matlab_build_validation_frame("DJI_Wideband_Link", raw, 96U,
          "sim_dji_wideband_018", 155U, 2U, 2U, 96650000U, 1U, &frame) == WRJ_OK);
    CHECK(frame.count == 48U && frame.bytes[0] == 0xA5U && frame.bytes[1] == 0x5AU);
    CHECK(frame.bytes[5] == 48U && frame.bytes[6] == 155U);
    CHECK(m4_crc16_ccitt(frame.bytes, 46U) ==
          (uint16_t)(((uint16_t)frame.bytes[46] << 8U) | frame.bytes[47]));
    memcpy(changed, frame.bytes, frame.count);
    changed[18] ^= 4U;
    CHECK(m4_matlab_crc16_recover_unique_single_bit(changed, frame.count) == 1);
    CHECK(memcmp(changed, frame.bytes, frame.count) == 0);
    m4_matlab_rng_seed(&rng, 20260724U);
    for (i = 0U; i < 5U; ++i)
        CHECK(fabs(m4_matlab_rand(&rng) - matlab_rand[i]) < 1e-15);
}

static void test_matlab_observations_reach_handoff(void)
{
    wrj_candidate_t candidate;
    m3_result_t m3;
    m4_result_t m4;
    m4_to_m5_handoff_t handoff;
    m4_matlab_rng_t rng;
    m4_matlab_record_view_t views[WRJ_MATLAB_MAX_OBSERVATIONS];
    m4_matlab_record_store_t store;
    m4_matlab_category_analysis_t analysis;
    uint32_t view_count = 0U;
    uint32_t i;
    memset(&candidate, 0, sizeof(candidate));
    memset(&m3, 0, sizeof(m3));
    memset(&m4, 0, sizeof(m4));
    strcpy(candidate.candidate_id, "probe");
    strcpy(candidate.source_file, "sim_dji_wideband_018");
    m3.profile = WRJ_PROFILE_DJI_WIDEBAND_CP;
    m3.num_frames = 2U;
    m3.frame_confidence[0] = .9f;
    m3.frame_confidence[1] = .9f;
    m4.packet_count = 2U;
    m4.packet_lengths[0] = 96U;
    m4.packet_lengths[1] = 96U;
    for (i = 0U; i < 96U; ++i) {
        m4.packet_bytes[0][i] = (uint8_t)i;
        m4.packet_bytes[1][i] = (uint8_t)(i + 1U);
    }
    m4_matlab_rng_seed(&rng, 20260724U);
    CHECK(m4_matlab_attach_observations(&candidate, &m3, &m4, &rng) == WRJ_OK);
    CHECK(m4.matlab_observation_count == 16U);
    CHECK(m4.matlab_observation_length[0] == 48U);
    CHECK(m4.matlab_observation_physical_index[0] == 1U);
    CHECK(m4.matlab_observation_physical_index[2] == 1U);
    m4_build_module5_handoff(&candidate, &m3, &m4, &handoff);
    CHECK(handoff.matlab_observation_count == 16U);
    CHECK(memcmp(handoff.matlab_observation_bytes[0], m4.matlab_observation_bytes[0], 48U) == 0);
    CHECK(handoff.matlab_observation_labels[0][0] == M4_MATLAB_HEADER);
    CHECK(m4_matlab_make_record_views(&candidate, &m4, views,
          WRJ_MATLAB_MAX_OBSERVATIONS, &view_count) == WRJ_OK);
    CHECK(view_count == 16U);
    CHECK(views[0].labels[0] == M4_MATLAB_HEADER);
    CHECK(views[0].state_index == m4.matlab_observation_state_index[0]);
    CHECK(handoff.matlab_observation_message_type[0] ==
          m4.matlab_observation_message_type[0]);
    CHECK(m4_matlab_record_store_init(&store, 20U) == WRJ_OK);
    CHECK(m4_matlab_record_store_append(&store, &candidate, &m3, &m4) == WRJ_OK);
    CHECK(m4_matlab_record_store_analyze(&store, "DJI_Wideband_Link", .55,
                                         &analysis) == WRJ_OK);
    CHECK(analysis.num_observations == 16U);
    CHECK(analysis.field_template.count > 0U);
    m4_matlab_record_store_release(&store);
}

static void test_matlab_category_analysis(void)
{
    m4_matlab_record_view_t records[3];
    m4_matlab_category_analysis_t analysis;
    m4_matlab_frame_t frames[3];
    uint8_t raw[96] = {0U};
    uint32_t i;
    memset(records, 0, sizeof(records));
    for (i = 0U; i < 3U; ++i) {
        CHECK(m4_matlab_build_validation_frame("DJI_Wideband_Link", raw, 96U,
              "category-test", (uint8_t)i, 1U, 1U, 100U + i * 10U,
              i + 1U, &frames[i]) == WRJ_OK);
        records[i].bytes = frames[i].bytes;
        records[i].labels = frames[i].labels;
        records[i].length = frames[i].count;
        records[i].source_hash = 1U;
        records[i].observation_index = (uint16_t)(i + 1U);
        records[i].state_index = 1U;
        records[i].message_type = 1U;
    }
    CHECK(m4_matlab_analyze_category(records, 3U, "DJI_Wideband_Link", .55,
                                     &analysis) == WRJ_OK);
    CHECK(analysis.num_candidates == 1U && analysis.num_observations == 3U);
    CHECK(analysis.stats.length == 48U);
    CHECK(analysis.field_template.count > 0U);
    CHECK(analysis.truth_available == 1U);
    CHECK(strcmp(analysis.field_template.fields[0].semantic, "Header") == 0);
    CHECK(strcmp(analysis.field_template.fields[0].parser_data_type,
                 "bytes/fingerprint") == 0);
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

static void test_dji_wideband_proxy_crc_gate(void)
{
    uint8_t frame[11U + 900U + 2U] = {0xD1U, 0xB4U, 0x22U, 5U, 3U, 132U, 3U,
                                      0x12U, 0x34U, 0x56U, 0x78U,
                                      0xD0U, 5U, 0U, 1U, 3U, 132U, 3U};
    m4_dji_wideband_parse_t parsed;
    uint16_t crc;
    uint32_t i;
    crc = m4_crc16_ccitt(frame, (uint32_t)sizeof(frame) - 2U);
    frame[sizeof(frame) - 2U] = (uint8_t)(crc >> 8U);
    frame[sizeof(frame) - 1U] = (uint8_t)crc;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(m4_decode_dji_wideband_proxy_frame(frame, sizeof(frame), &parsed) == 1);
    CHECK(parsed.complete == 1U && parsed.frame_id == 5U);
    CHECK(parsed.device_id == 0x1234U && parsed.timestamp_counter == 0x5678U);
    CHECK(parsed.payload_length == 900U && parsed.crc_valid_count == 1U);
    frame[18] ^= 1U;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(m4_decode_dji_wideband_proxy_frame(frame, sizeof(frame), &parsed) == 0);
    CHECK(parsed.complete == 0U && parsed.crc_valid_count == 0U);
    frame[18] ^= 1U;
    for (i = 0U; i < sizeof(frame); ++i) frame[i] ^= 0xFFU;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(m4_decode_dji_wideband_proxy_frame(frame, sizeof(frame), &parsed) == 1);
    CHECK(parsed.polarity_inverted == 1U && parsed.frame_id == 5U);
}

static void test_dji_wideband_assembly_continuity(void)
{
    uint8_t frame[11U + 900U + 2U] = {0xD1U, 0xB4U, 0x22U, 5U, 3U, 132U, 3U,
                                      0x12U, 0x34U, 0x56U, 0x78U,
                                      0xD0U, 5U, 0U, 1U, 3U, 132U, 3U};
    m3_result_t m3;
    m4_result_t result;
    uint16_t crc, i;
    memset(&m3, 0, sizeof(m3));
    memset(&result, 0, sizeof(result));
    m3.frame_length_samples = 2208U;
    crc = m4_crc16_ccitt(frame, (uint32_t)sizeof(frame) - 2U);
    frame[sizeof(frame) - 2U] = (uint8_t)(crc >> 8U);
    frame[sizeof(frame) - 1U] = (uint8_t)crc;
    result.packet_count = 10U;
    for (i = 0U; i < 10U; ++i) {
        const uint16_t source = (uint16_t)(9U - i);
        const size_t offset = (size_t)source * 96U;
        const size_t length = WRJ_MIN(96U, sizeof(frame) - offset);
        result.packet_source_start[i] = 1000U + (uint32_t)source * 2208U;
        result.packet_lengths[i] = (uint16_t)length;
        result.packet_confidence[i] = .9f;
        result.packet_full_symbol[i] = 1U;
        memcpy(result.packet_bytes[i], frame + offset, length);
    }
    m4_assemble_dji_wideband(&m3, &result);
    CHECK(result.dji_wideband.assembled_byte_count == sizeof(frame));
    CHECK(result.dji_wideband.assembled_observation_count == 10U);
    CHECK(result.dji_wideband.complete == 1U);
    CHECK(result.dji_wideband.crc16_passed == 1U);
    memset(&result.dji_wideband, 0, sizeof(result.dji_wideband));
    result.packet_source_start[4] += 300U; /* a missing/displaced symbol */
    m4_assemble_dji_wideband(&m3, &result);
    CHECK(result.dji_wideband.complete == 0U);
    CHECK(result.dji_wideband.assembled_byte_count < sizeof(frame));
}

static void test_module5_handoff_keeps_validation_boundary(void)
{
    static wrj_candidate_t candidate;
    static m3_result_t m3;
    static m4_result_t m4;
    static m4_to_m5_handoff_t handoff;
    memset(&candidate, 0, sizeof(candidate));
    memset(&m3, 0, sizeof(m3));
    memset(&m4, 0, sizeof(m4));
    snprintf(candidate.candidate_id, sizeof(candidate.candidate_id), "test-candidate");
    candidate.sample_rate_hz = 1000000.0f;
    candidate.candidate_start_sec = 2.0;
    candidate.candidate_end_sec = 2.5;
    snprintf(m3.status, sizeof(m3.status), "ok");
    m3.profile = WRJ_PROFILE_REMOTEID_BLE;
    m4.byte_source = M4_BYTE_SOURCE_REAL_IQ;
    m4.packet_count = 1U;
    m4.crc_valid_count = 1U;
    m4.packet_lengths[0] = 39U;
    m4.packet_source_start[0] = 1234U;
    m4.packet_confidence[0] = .9f;
    m4.packet_bytes[0][2] = 0xAAU;
    m4.packet_bytes[0][13] = 7U;
    m4.packet_bytes[0][14] = 0x10U;
    m4_build_module5_handoff(&candidate, &m3, &m4, &handoff);
    CHECK(handoff.real_iq_only == 1U);
    CHECK(handoff.packet_count == 1U);
    CHECK(handoff.packets[0].kind == M4_M5_BLE_PDU);
    CHECK(handoff.packets[0].crc_checked == 1U && handoff.packets[0].crc_valid == 1U);
    CHECK(handoff.packets[0].sequence_number == 7);
    CHECK(handoff.packets[0].message_type == 1);
    CHECK(fabs(handoff.packets[0].observation_time_sec - .001234) < 1e-9);
    CHECK(handoff.candidate_start_sec == 2.0 && handoff.candidate_end_sec == 2.5);

    m3.profile = WRJ_PROFILE_DJI_WIDEBAND_CP;
    m3.num_frames = 2U;
    m3.frame_start_samples_0based[0] = 100U;
    m3.frame_start_samples_0based[1] = 1234U;
    m4.crc_valid_count = 0U;
    m4.verified_frame_length = 0U;
    m4.packet_lengths[0] = 96U;
    m4_build_module5_handoff(&candidate, &m3, &m4, &handoff);
    CHECK(handoff.packets[0].kind == M4_M5_SYMBOL_OBSERVATION);
    CHECK(handoff.packets[0].frame_index == 2U);
    CHECK(handoff.packets[0].crc_checked == 0U && handoff.packets[0].crc_valid == 0U);
}

int main(void)
{
    test_crc24();
    test_bit_pack();
    test_matlab_validation_flow();
    test_matlab_observations_reach_handoff();
    test_matlab_category_analysis();
    test_qpsk();
    test_fft_carrier_mapping();
    test_ble_whitening();
    test_field_parser();
    test_dji_wideband_proxy_crc_gate();
    test_dji_wideband_assembly_continuity();
    test_module5_handoff_keeps_validation_boundary();
    puts("module4 tests passed");
    return 0;
}
