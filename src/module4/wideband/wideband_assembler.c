#include "wrj/module4/wideband_assembler.h"
#include "m4_module4.h"

#include <limits.h>

#define M4_WIDEBAND_STREAM_CAPACITY (WRJ_MAX_PACKETS * WRJ_MAX_BYTES)

typedef struct {
    uint16_t packet;
    uint32_t start;
} m4_observation_ref_t;

static void m4_sort_observations(const m4_result_t *result,
                                 m4_observation_ref_t *refs, uint16_t count)
{
    uint16_t i;
    for (i = 0U; i < count; ++i) {
        uint16_t j = i;
        const m4_observation_ref_t value = {i, result->packet_source_start[i]};
        while (j > 0U && refs[j - 1U].start > value.start) {
            refs[j] = refs[j - 1U];
            --j;
        }
        refs[j] = value;
    }
}

void m4_assemble_dji_wideband(const m3_result_t *m3, m4_result_t *result)
{
    m4_dji_wideband_parse_t *parsed;
    m4_observation_ref_t refs[WRJ_MAX_PACKETS];
    uint8_t stream[M4_WIDEBAND_STREAM_CAPACITY];
    uint16_t count, run_first, run_last, i;
    uint32_t best_bytes = 0U;
    float best_quality = -1.0f;
    int best_verified = 0;
    if (m3 == NULL || result == NULL) return;
    parsed = &result->dji_wideband;
    count = WRJ_MIN(result->packet_count, WRJ_MAX_PACKETS);
    for (i = 0U; i < WRJ_MAX_PACKETS; ++i) parsed->assembly_offsets[i] = UINT32_MAX;
    snprintf(parsed->polarity_mode, sizeof(parsed->polarity_mode),
             "global_normal_or_180_crc");
    if (count == 0U || m3->frame_length_samples == 0U) {
        snprintf(parsed->assembly_method, sizeof(parsed->assembly_method), "no_observations");
        return;
    }
    m4_sort_observations(result, refs, count);
    run_first = 0U;
    while (run_first < count) {
        uint32_t byte_count = 0U;
        float quality = 0.0f;
        int verified = 0;
        const uint32_t step = m3->frame_length_samples;
        const uint32_t tolerance = WRJ_MAX(8U, step / 128U);
        if (result->packet_full_symbol[refs[run_first].packet] == 0U) {
            ++run_first;
            continue;
        }
        run_last = (uint16_t)(run_first + 1U);
        while (run_last < count) {
            const uint32_t previous = refs[run_last - 1U].start;
            const uint32_t next = refs[run_last].start;
            if (result->packet_full_symbol[refs[run_last].packet] == 0U ||
                next <= previous || next - previous < step - WRJ_MIN(step, tolerance) ||
                next - previous > step + tolerance) break;
            ++run_last;
        }
        for (i = run_first; i < run_last; ++i) {
            const uint16_t packet = refs[i].packet;
            const uint32_t length = result->packet_lengths[packet];
            if (length > M4_WIDEBAND_STREAM_CAPACITY - byte_count) break;
            memcpy(stream + byte_count, result->packet_bytes[packet], length);
            byte_count += length;
            quality += result->packet_confidence[packet];
        }
        /* A CRC-verified frame is never accepted across a missing OFDM symbol. */
        if (byte_count > 0U)
            verified = m4_decode_dji_wideband_proxy_frame(stream, byte_count, parsed);
        if (verified != 0) {
            parsed->source_observation = (uint16_t)(refs[run_first].packet + 1U);
            {
                const uint32_t logical_length = 13U + parsed->payload_length;
                if (logical_length <= sizeof(result->verified_frame_bytes) &&
                    (uint32_t)parsed->byte_offset + logical_length <= byte_count) {
                    result->verified_frame_length = (uint16_t)logical_length;
                    memcpy(result->verified_frame_bytes,
                           stream + parsed->byte_offset, logical_length);
                }
            }
        }
        if ((verified != 0 && best_verified == 0) ||
            (best_verified == 0 && (byte_count > best_bytes ||
             (byte_count == best_bytes && quality > best_quality)))) {
            uint32_t offset = 0U;
            uint16_t packet;
            for (packet = 0U; packet < WRJ_MAX_PACKETS; ++packet)
                parsed->assembly_offsets[packet] = UINT32_MAX;
            for (i = run_first; i < run_last; ++i) {
                const uint16_t packet_index = refs[i].packet;
                parsed->assembly_offsets[packet_index] = offset;
                offset += result->packet_lengths[packet_index];
            }
            best_bytes = byte_count;
            best_quality = quality;
            best_verified = verified;
            parsed->assembled_observation_count = (uint16_t)(run_last - run_first);
            parsed->assembly_start_observation = (uint16_t)(refs[run_first].packet + 1U);
            parsed->assembly_end_observation = (uint16_t)(refs[run_last - 1U].packet + 1U);
            parsed->assembly_continuous = 1U;
        }
        if (verified != 0) break;
        run_first = run_last;
    }
    parsed->assembled_byte_count = best_bytes;
    snprintf(parsed->assembly_method, sizeof(parsed->assembly_method),
             "%s", parsed->assembled_observation_count > 1U ?
             "time_contiguous_symbols" : "single_symbol_only");
}
