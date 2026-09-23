#include "wrj_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define WRJ_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define WRJ_MKDIR(path) mkdir(path, 0755)
#endif

#define WRJ_CSV_COLUMNS 40
#define WRJ_CSV_FIELD_CAPACITY 512

static int wrj_csv_split(char *line, char fields[][WRJ_CSV_FIELD_CAPACITY], int maximum)
{
    int column = 0;
    int quoted = 0;
    size_t length = 0U;
    char *cursor = line;
    if (line == NULL || fields == NULL || maximum <= 0) {
        return 0;
    }
    memset(fields, 0, (size_t)maximum * WRJ_CSV_FIELD_CAPACITY);
    while (*cursor != '\0' && column < maximum) {
        const char current = *cursor++;
        if (current == '"') {
            if (quoted != 0 && *cursor == '"') {
                if (length + 1U < WRJ_CSV_FIELD_CAPACITY) {
                    fields[column][length++] = '"';
                }
                ++cursor;
            } else {
                quoted = quoted == 0;
            }
        } else if (current == ',' && quoted == 0) {
            fields[column][length] = '\0';
            ++column;
            length = 0U;
        } else if (current != '\r' && current != '\n') {
            if (length + 1U < WRJ_CSV_FIELD_CAPACITY) {
                fields[column][length++] = current;
            }
        }
    }
    if (column < maximum) {
        fields[column][length] = '\0';
        ++column;
    }
    return column;
}

static int wrj_column_index(char fields[][WRJ_CSV_FIELD_CAPACITY], int count, const char *name)
{
    int index;
    for (index = 0; index < count; ++index) {
        if (strcmp(fields[index], name) == 0) {
            return index;
        }
    }
    return -1;
}

static void wrj_copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == NULL || capacity == 0U) {
        return;
    }
    snprintf(destination, capacity, "%s", source == NULL ? "" : source);
}

static float wrj_parse_float(const char *value)
{
    char *end = NULL;
    if (value == NULL || value[0] == '\0') {
        return 0.0f;
    }
    errno = 0;
    return strtof(value, &end);
}

wrj_status_t wrj_load_handoff_candidate(const char *csv_path, const char *candidate_id,
                                        wrj_candidate_t *candidate)
{
    FILE *file;
    char line[16384];
    char headers[WRJ_CSV_COLUMNS][WRJ_CSV_FIELD_CAPACITY];
    char fields[WRJ_CSV_COLUMNS][WRJ_CSV_FIELD_CAPACITY];
    int header_count;
    int id_col;
    int source_col;
    int link_col;
    int protocol_col;
    int fs_col;
    int fc_col;
    int offset_col;
    int bandwidth_col;
    int coarse_cfo_col;
    int strategy_col;
    int parser_col;
    int artifact_col;
    int period_col;
    int structure_col;
    int preamble_col;
    int guard_col;

    if (csv_path == NULL || candidate_id == NULL || candidate == NULL) {
        return WRJ_ERR_ARGUMENT;
    }
    file = fopen(csv_path, "rb");
    if (file == NULL || fgets(line, (int)sizeof(line), file) == NULL) {
        if (file != NULL) {
            fclose(file);
        }
        return WRJ_ERR_IO;
    }
    header_count = wrj_csv_split(line, headers, WRJ_CSV_COLUMNS);
    id_col = wrj_column_index(headers, header_count, "candidateId");
    source_col = wrj_column_index(headers, header_count, "sourceFile");
    link_col = wrj_column_index(headers, header_count, "predictedLinkType");
    protocol_col = wrj_column_index(headers, header_count, "predictedProtocolFamily");
    fs_col = wrj_column_index(headers, header_count, "sampleRateHz");
    fc_col = wrj_column_index(headers, header_count, "centerFrequencyHz");
    offset_col = wrj_column_index(headers, header_count, "candidateCenterOffsetHz");
    bandwidth_col = wrj_column_index(headers, header_count, "bandwidthHz");
    coarse_cfo_col = wrj_column_index(headers, header_count, "coarseCfoHz");
    strategy_col = wrj_column_index(headers, header_count, "syncStrategy");
    parser_col = wrj_column_index(headers, header_count, "parserTemplateId");
    artifact_col = wrj_column_index(headers, header_count, "candidateIqArtifact");
    period_col = wrj_column_index(headers, header_count, "framePeriodEstimateSec");
    structure_col = wrj_column_index(headers, header_count, "frameStructureScore");
    preamble_col = wrj_column_index(headers, header_count, "preambleRepeatScore");
    guard_col = wrj_column_index(headers, header_count, "recommendedGuardSec");
    if (id_col < 0) {
        fclose(file);
        return WRJ_ERR_DATA;
    }
    while (fgets(line, (int)sizeof(line), file) != NULL) {
        const int count = wrj_csv_split(line, fields, WRJ_CSV_COLUMNS);
        if (id_col >= count || strcmp(fields[id_col], candidate_id) != 0) {
            continue;
        }
        memset(candidate, 0, sizeof(*candidate));
#define WRJ_COPY_FIELD(member, column) do { if ((column) >= 0 && (column) < count) { \
    wrj_copy_text(candidate->member, sizeof(candidate->member), fields[column]); } } while (0)
#define WRJ_NUMBER_FIELD(member, column) do { if ((column) >= 0 && (column) < count) { \
    candidate->member = wrj_parse_float(fields[column]); } } while (0)
        WRJ_COPY_FIELD(candidate_id, id_col);
        WRJ_COPY_FIELD(source_file, source_col);
        WRJ_COPY_FIELD(predicted_link_type, link_col);
        WRJ_COPY_FIELD(predicted_protocol_family, protocol_col);
        WRJ_COPY_FIELD(sync_strategy, strategy_col);
        WRJ_COPY_FIELD(parser_template_id, parser_col);
        WRJ_COPY_FIELD(candidate_iq_artifact, artifact_col);
        WRJ_NUMBER_FIELD(sample_rate_hz, fs_col);
        WRJ_NUMBER_FIELD(center_frequency_hz, fc_col);
        WRJ_NUMBER_FIELD(candidate_center_offset_hz, offset_col);
        WRJ_NUMBER_FIELD(bandwidth_hz, bandwidth_col);
        WRJ_NUMBER_FIELD(coarse_cfo_hz, coarse_cfo_col);
        WRJ_NUMBER_FIELD(frame_period_estimate_sec, period_col);
        WRJ_NUMBER_FIELD(frame_structure_score, structure_col);
        WRJ_NUMBER_FIELD(preamble_repeat_score, preamble_col);
        WRJ_NUMBER_FIELD(recommended_guard_sec, guard_col);
#undef WRJ_COPY_FIELD
#undef WRJ_NUMBER_FIELD
        fclose(file);
        return WRJ_OK;
    }
    fclose(file);
    return WRJ_ERR_DATA;
}

wrj_status_t wrj_read_cf32(const char *path, wrj_cf32_t *samples, uint32_t capacity,
                            uint32_t *count)
{
    FILE *file;
    uint32_t index = 0U;
    float pair[2];
    if (path == NULL || samples == NULL || count == NULL) {
        return WRJ_ERR_ARGUMENT;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return WRJ_ERR_IO;
    }
    while (index < capacity && fread(pair, sizeof(pair[0]), 2U, file) == 2U) {
        samples[index].re = pair[0];
        samples[index].im = pair[1];
        ++index;
    }
    if (!feof(file) && index == capacity) {
        fclose(file);
        return WRJ_ERR_CAPACITY;
    }
    fclose(file);
    *count = index;
    return index == 0U ? WRJ_ERR_DATA : WRJ_OK;
}

wrj_status_t wrj_make_directory(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return WRJ_ERR_ARGUMENT;
    }
    if (WRJ_MKDIR(path) != 0 && errno != EEXIST) {
        return WRJ_ERR_IO;
    }
    return WRJ_OK;
}

const char *wrj_profile_name(wrj_profile_kind_t profile)
{
    switch (profile) {
        case WRJ_PROFILE_DJI_WIDEBAND_CP: return "DJI_Wideband_CP";
        case WRJ_PROFILE_AUTEL_WIDEBAND_CP: return "Autel_Wideband_CP";
        case WRJ_PROFILE_DRONEID_ZC: return "DJI_DroneID_ZC_CP";
        case WRJ_PROFILE_REMOTEID_BLE: return "RemoteID_BLE_GFSK";
        case WRJ_PROFILE_CONTROL_BURST: return "Control_Burst";
        case WRJ_PROFILE_AUTEL_CONTROL_CP: return "Autel_Control_CP_Hop";
        case WRJ_PROFILE_DJI_CONTROL_BLIND: return "DJI_Control_Template_Mismatch_Blind_CP";
        default: return "Unknown_Blind_Repetition";
    }
}

const char *wrj_m4_status_name(m4_status_t status)
{
    switch (status) {
        case M4_STATUS_PARTIAL: return "partial";
        case M4_STATUS_NO_BYTES: return "no_bytes";
        case M4_STATUS_DEFERRED_REMOTEID: return "deferred_remoteid";
        case M4_STATUS_UNSUPPORTED_PROFILE: return "unsupported_profile";
        case M4_STATUS_BYTE_RECOVERY_FAILED: return "byte_recovery_failed";
        case M4_STATUS_CRC_FAILED: return "crc_failed";
        case M4_STATUS_PARSED: return "parsed";
        default: return "not_run";
    }
}

wrj_status_t wrj_write_results(const char *output_dir, const wrj_candidate_t *candidate,
                               const m3_result_t *m3_result, const m4_result_t *m4_result)
{
    char path[WRJ_PATH_CAPACITY];
    FILE *file;
    uint16_t index;
    if (output_dir == NULL || candidate == NULL || m3_result == NULL || m4_result == NULL) {
        return WRJ_ERR_ARGUMENT;
    }
    if (wrj_make_directory(output_dir) != WRJ_OK) {
        return WRJ_ERR_IO;
    }
    snprintf(path, sizeof(path), "%s/module3_result.csv", output_dir);
    file = fopen(path, "wb");
    if (file == NULL) {
        return WRJ_ERR_IO;
    }
    fprintf(file, "candidateId,profile,status,syncMethod,estimatedCFOHz,totalCfoHz,spectralCorrectionHz,fractionalCFOHz,integerCfoIndex,integerCfoOffsetHz,residualCfoHz,syncConfidence,frameStart0,frameLength,numFrames,originalFrameStart,correctedFrameStart,frameOffsetCorrection,droneIdZcPeakPosition,offsetSearchScore,timingAlignmentImproved,estimatedSfoPpm,frameDriftSamples,timingSlope,sfoCorrectionApplied,accessAddressConfidence,bleSyncConfidence,symbolTimingOffset,crcAttemptCount,crcSuccessCount,cfoCandidateCount,selectedCfoScore,profilesTried,profileChangedByEvidence,recommendedProfile,attemptedProfiles\n");
    fprintf(file, "%s,%s,%s,%s,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.6f,%u,%.6f,%.6f,%.6f,%u,%.6f,%.6f,%.3f,%u,%u,%u,%.6f,%u,%u,%s,%s\n",
            candidate->candidate_id, m3_result->profile_name, m3_result->status,
            m3_result->sync_method, m3_result->estimated_cfo_hz, m3_result->estimated_cfo_hz,
            m3_result->spectral_correction_hz,
            m3_result->fractional_cfo_hz, m3_result->integer_cfo_index,
            m3_result->integer_cfo_offset_hz, m3_result->residual_cfo_hz,
            m3_result->sync_confidence,
            m3_result->num_frames == 0U ? 0U : m3_result->frame_start_samples_0based[0],
            m3_result->frame_length_samples, m3_result->num_frames,
            m3_result->original_frame_start, m3_result->corrected_frame_start,
            m3_result->frame_offset_correction, m3_result->droneid_zc_peak_position,
            m3_result->offset_search_score, m3_result->timing_alignment_improved,
            m3_result->estimated_sfo_ppm, m3_result->frame_drift_samples,
            m3_result->timing_slope, m3_result->sfo_correction_applied,
            m3_result->access_address_confidence, m3_result->ble_sync_confidence,
            m3_result->symbol_timing_offset, m3_result->crc_attempt_count,
            m3_result->crc_success_count, m3_result->cfo_candidate_count,
            m3_result->selected_cfo_score, m3_result->profiles_tried,
            m3_result->profile_changed_by_evidence, m3_result->recommended_profile,
            m3_result->attempted_profiles);
    fclose(file);

    snprintf(path, sizeof(path), "%s/module3_frames.csv", output_dir);
    file = fopen(path, "wb");
    if (file == NULL) {
        return WRJ_ERR_IO;
    }
    fprintf(file, "candidateId,frameIndex,frameStart0,frameStart1,frameConfidence\n");
    for (index = 0U; index < m3_result->num_frames; ++index) {
        fprintf(file, "%s,%u,%u,%u,%.6f\n", candidate->candidate_id, index + 1U,
                m3_result->frame_start_samples_0based[index],
                m3_result->frame_start_samples_0based[index] + 1U,
                m3_result->frame_confidence[index]);
    }
    fclose(file);

    snprintf(path, sizeof(path), "%s/module4_result.csv", output_dir);
    file = fopen(path, "wb");
    if (file == NULL) {
        return WRJ_ERR_IO;
    }
    fprintf(file, "candidateId,status,protocolType,byteSource,byteRecoveryStatus,parseStatus,parseComplete,byteCount,byteRecoveryConfidence,bitErrorEstimate,decoderMethod,crcCandidateStatus,crcChecked,crcPassed,packetCount,crcValidCount,messageTypesMask,uasId,operatorId,latitudeDeg,longitudeDeg,altitudeM,speedMps,headingDeg,fieldCount,diagnostics\n");
    fprintf(file, "%s,%s,%s,%s,%s,%s,%u,%u,%.6f,%.6f,%s,%s,%u,%u,%u,%u,%u,%s,%s,%.8f,%.8f,%.3f,%.3f,%.3f,%u,%s\n",
            candidate->candidate_id, wrj_m4_status_name(m4_result->status), m4_result->protocol_type,
            m4_result->byte_source == M4_BYTE_SOURCE_REAL_IQ ? "REAL_IQ" :
                (m4_result->byte_source == M4_BYTE_SOURCE_SYNTHETIC_TEST ? "SYNTHETIC_TEST" : "NONE"),
            m4_result->byte_recovery_status, m4_result->parse_status,
            m4_result->parse_complete, m4_result->byte_count, m4_result->byte_recovery_confidence,
            m4_result->bit_error_estimate, m4_result->decoder_method,
            m4_result->crc_candidate_status, m4_result->crc_checked, m4_result->crc_passed,
            m4_result->packet_count, m4_result->crc_valid_count,
            m4_result->message_types_mask, m4_result->uas_id, m4_result->operator_id,
            m4_result->latitude_deg, m4_result->longitude_deg, m4_result->altitude_m,
            m4_result->speed_mps, m4_result->heading_deg, m4_result->field_count,
            m4_result->diagnostics);
    fclose(file);

    snprintf(path, sizeof(path), "%s/module4_fields.csv", output_dir);
    file = fopen(path, "wb");
    if (file == NULL) return WRJ_ERR_IO;
    fprintf(file, "candidateId,fieldId,fieldOffset,fieldLength,fieldType,semantic,numericValue,stringValue,confidence\n");
    for (index = 0U; index < m4_result->field_count; ++index) {
        const m4_field_t *field = &m4_result->fields[index];
        fprintf(file, "%s,%u,%u,%u,%s,%s,%.8f,%s,%.6f\n", candidate->candidate_id,
                field->field_id, field->offset, field->length, field->field_type,
                field->semantic, field->numeric_value, field->string_value, field->confidence);
    }
    fclose(file);

    snprintf(path, sizeof(path), "%s/module4_packets.csv", output_dir);
    file = fopen(path, "wb");
    if (file == NULL) return WRJ_ERR_IO;
    fprintf(file, "candidateId,packetIndex,byteCount,crcStatus,hexBytes\n");
    for (index = 0U; index < m4_result->packet_count; ++index) {
        uint16_t byte;
        fprintf(file, "%s,%u,%u,%s,", candidate->candidate_id,
                index + 1U, m4_result->packet_lengths[index],
                strstr(m4_result->protocol_type, "RemoteID_BLE") != NULL ?
                    (m4_result->crc_passed != 0U ? "verified_crc24" : "crc24_failed") :
                    (m4_result->crc_passed != 0U ? "candidate_crc16_only" : "unverified_raw_bits"));
        for (byte = 0U; byte < m4_result->packet_lengths[index]; ++byte)
            fprintf(file, "%02X", m4_result->packet_bytes[index][byte]);
        fputc('\n', file);
    }
    fclose(file);

    snprintf(path, sizeof(path), "%s/module4_bytes.csv", output_dir);
    file = fopen(path, "wb");
    if (file == NULL) return WRJ_ERR_IO;
    fprintf(file, "candidateId,byteIndex,valueHex,confidence,byteSource\n");
    for (index = 0U; index < m4_result->byte_count; ++index)
        fprintf(file, "%s,%u,%02X,%.6f,REAL_IQ\n", candidate->candidate_id,
                index, m4_result->bytes[index], m4_result->byte_confidence[index]);
    fclose(file);
    return WRJ_OK;
}
