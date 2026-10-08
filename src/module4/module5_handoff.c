#include "m4_module5_handoff.h"
#include "wrj_io.h"

void m4_build_module5_handoff(const wrj_candidate_t *candidate,
                              const m3_result_t *m3, const m4_result_t *m4,
                              m4_to_m5_handoff_t *handoff)
{
    uint16_t index;
    if (candidate == NULL || m3 == NULL || m4 == NULL || handoff == NULL) return;
    memset(handoff, 0, sizeof(*handoff));
    snprintf(handoff->candidate_id, sizeof(handoff->candidate_id), "%s", candidate->candidate_id);
    snprintf(handoff->source_file, sizeof(handoff->source_file), "%s", candidate->source_file);
    handoff->candidate_start_sec = candidate->candidate_start_sec;
    handoff->candidate_end_sec = candidate->candidate_end_sec;
    snprintf(handoff->protocol, sizeof(handoff->protocol), "%s", m4->protocol_type);
    snprintf(handoff->m3_status, sizeof(handoff->m3_status), "%s", m3->status);
    snprintf(handoff->m4_status, sizeof(handoff->m4_status), "%s", wrj_m4_status_name(m4->status));
    snprintf(handoff->byte_recovery_status, sizeof(handoff->byte_recovery_status),
             "%s", m4->byte_recovery_status);
    handoff->real_iq_only = (uint8_t)(m4->byte_source != M4_BYTE_SOURCE_SYNTHETIC_TEST);
    handoff->parse_complete = m4->parse_complete;
    handoff->synced_frame_count = m3->num_frames;
    handoff->sync_confidence = m3->sync_confidence;
    handoff->cfo_hz = m3->estimated_cfo_hz;
    handoff->crc_valid_count = m4->crc_valid_count;
    handoff->latitude_deg = m4->latitude_deg;
    handoff->longitude_deg = m4->longitude_deg;
    handoff->altitude_m = m4->altitude_m;
    handoff->speed_mps = m4->speed_mps;
    handoff->heading_deg = m4->heading_deg;
    snprintf(handoff->uas_id, sizeof(handoff->uas_id), "%s", m4->uas_id);
    snprintf(handoff->operator_id, sizeof(handoff->operator_id), "%s", m4->operator_id);
    handoff->field_count = WRJ_MIN(m4->field_count, WRJ_MAX_FIELDS);
    memcpy(handoff->fields, m4->fields,
           (size_t)handoff->field_count * sizeof(handoff->fields[0]));
    handoff->matlab_observation_count = m4->matlab_observation_count;
    memcpy(handoff->matlab_observation_length, m4->matlab_observation_length,
           sizeof(handoff->matlab_observation_length));
    memcpy(handoff->matlab_observation_bytes, m4->matlab_observation_bytes,
           sizeof(handoff->matlab_observation_bytes));
    memcpy(handoff->matlab_observation_labels, m4->matlab_observation_labels,
           sizeof(handoff->matlab_observation_labels));
    memcpy(handoff->matlab_observation_physical_index, m4->matlab_observation_physical_index,
           sizeof(handoff->matlab_observation_physical_index));
    memcpy(handoff->matlab_observation_state_index, m4->matlab_observation_state_index,
           sizeof(handoff->matlab_observation_state_index));
    memcpy(handoff->matlab_observation_message_type, m4->matlab_observation_message_type,
           sizeof(handoff->matlab_observation_message_type));
    memcpy(handoff->matlab_observation_sequence, m4->matlab_observation_sequence,
           sizeof(handoff->matlab_observation_sequence));
    memcpy(handoff->matlab_observation_timestamp, m4->matlab_observation_timestamp,
           sizeof(handoff->matlab_observation_timestamp));
    handoff->packet_count = WRJ_MIN(m4->packet_count, WRJ_MAX_PACKETS);
    for (index = 0U; index < handoff->packet_count; ++index) {
        m4_m5_packet_t *packet = &handoff->packets[index];
        const uint16_t length = WRJ_MIN(m4->packet_lengths[index], M4_M5_MAX_PACKET_BYTES);
        packet->kind = m3->profile == WRJ_PROFILE_REMOTEID_BLE ?
            M4_M5_BLE_PDU : M4_M5_SYMBOL_OBSERVATION;
        packet->crc_kind = packet->kind == M4_M5_BLE_PDU ? M4_M5_CRC24_BLE : M4_M5_CRC_NONE;
        packet->observation_index = (uint16_t)(index + 1U);
        packet->frame_index = 0U;
        packet->start_sample = m4->packet_source_start[index];
        packet->observation_time_sec = candidate->sample_rate_hz > 0.0f ?
            (double)packet->start_sample / candidate->sample_rate_hz : NAN;
        packet->byte_count = length;
        memcpy(packet->bytes, m4->packet_bytes[index], length);
        packet->confidence = m4->packet_confidence[index];
        packet->message_type = packet->kind == M4_M5_BLE_PDU && length >= 15U ?
            (int16_t)(packet->bytes[14] >> 4U) : -1;
        packet->sequence_number = -1;
        if (packet->kind == M4_M5_SYMBOL_OBSERVATION) {
            uint32_t frame;
            for (frame = 0U; frame < m3->num_frames; ++frame) {
                if (m3->frame_start_samples_0based[frame] == packet->start_sample) {
                    packet->frame_index = (uint16_t)(frame + 1U);
                    break;
                }
            }
        }
        if (packet->kind == M4_M5_BLE_PDU) {
            packet->crc_checked = 1U;
            packet->crc_valid = 1U; /* only CRC24-verified PDUs reach m4->packet_bytes */
            if (length >= 14U) {
                memcpy(packet->device_address, packet->bytes + 2U, 6U);
                packet->device_address_valid = 1U;
                packet->sequence_number = packet->bytes[13];
            }
        }
    }
    if (m3->profile == WRJ_PROFILE_REMOTEID_BLE) {
        for (index = 1U; index < handoff->packet_count; ++index) {
            m4_m5_packet_t current = handoff->packets[index];
            uint16_t position = index;
            while (position > 0U &&
                   handoff->packets[position - 1U].start_sample > current.start_sample) {
                handoff->packets[position] = handoff->packets[position - 1U];
                --position;
            }
            handoff->packets[position] = current;
        }
        for (index = 0U; index < handoff->packet_count; ++index) {
            handoff->packets[index].observation_index = index + 1U;
            handoff->packets[index].frame_index = index + 1U;
        }
    }
    if (m4->verified_frame_length > 0U && handoff->packet_count < M4_M5_MAX_PACKETS) {
        m4_m5_packet_t *packet = &handoff->packets[handoff->packet_count++];
        packet->kind = M4_M5_LOGICAL_FRAME;
        packet->crc_kind = M4_M5_CRC16_CCITT;
        packet->observation_index = m4->dji_wideband.source_observation;
        packet->frame_index = m4->dji_wideband.frame_id;
        packet->byte_count = WRJ_MIN(m4->verified_frame_length, M4_M5_MAX_PACKET_BYTES);
        memcpy(packet->bytes, m4->verified_frame_bytes, packet->byte_count);
        packet->crc_checked = 1U;
        packet->crc_valid = 1U;
        packet->polarity_inverted = m4->dji_wideband.polarity_inverted;
        packet->message_type = m4->dji_wideband.message_type;
        packet->sequence_number = m4->dji_wideband.frame_id;
        packet->confidence = m4->semantic_confidence;
        if (packet->observation_index > 0U && packet->observation_index <= m4->packet_count) {
            packet->start_sample = m4->packet_source_start[packet->observation_index - 1U];
            packet->observation_time_sec = candidate->sample_rate_hz > 0.0f ?
                (double)packet->start_sample / candidate->sample_rate_hz : NAN;
        }
    }
}
