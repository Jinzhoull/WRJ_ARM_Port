#include "m4_matlab_statistics.h"
#include "m4_module4.h"

#include <float.h>
#include <stdlib.h>

static double m4_stat_entropy(const uint32_t *counts, uint32_t bins, uint32_t total)
{
    double entropy = 0.0;
    uint32_t i;
    if (total == 0U) return 0.0;
    for (i = 0U; i < bins; ++i) if (counts[i] != 0U) {
        const double p = (double)counts[i] / total;
        entropy -= p * log2(p);
    }
    return entropy;
}

static double m4_stat_nmi(const m4_matlab_record_view_t *records, uint32_t count,
                          uint32_t position, int mode, uint32_t *joint)
{
    uint32_t x[256] = {0U}, y[256] = {0U};
    uint32_t n = 0U, i, a, b;
    double mi = 0.0, hx, hy;
    memset(joint, 0, 256U * 256U * sizeof(*joint));
    for (i = 0U; i < count; ++i) {
        uint8_t yy;
        if (position >= records[i].length) continue;
        if (mode == 0) yy = records[i].state_index;
        else if (mode == 1) yy = records[i].message_type;
        else {
            if (position + 1U >= records[i].length) continue;
            yy = records[i].bytes[position + 1U];
        }
        ++joint[(uint32_t)records[i].bytes[position] * 256U + yy];
        ++x[records[i].bytes[position]];
        ++y[yy];
        ++n;
    }
    if (n < 2U) return 0.0;
    hx = m4_stat_entropy(x, 256U, n);
    hy = m4_stat_entropy(y, 256U, n);
    if (hx <= 0.0 || hy <= 0.0) return 0.0;
    for (a = 0U; a < 256U; ++a) if (x[a] != 0U)
        for (b = 0U; b < 256U; ++b) {
            const uint32_t frequency = joint[a * 256U + b];
            if (frequency != 0U) {
                const double pxy = (double)frequency / n;
                const double px = (double)x[a] / n, py = (double)y[b] / n;
                mi += pxy * log2(pxy / (px * py));
            }
        }
    return fmax(0.0, fmin(1.0, mi / fmin(hx, hy)));
}

static double m4_stat_pair_nmi(const m4_matlab_record_view_t *records,
                               uint32_t count, uint32_t left, uint32_t right,
                               uint32_t *joint)
{
    double px[256] = {0.0}, py[256] = {0.0};
    uint32_t n = 0U, i, a, b;
    double mi = 0.0, hx, hy;
    memset(joint, 0, 256U * 256U * sizeof(*joint));
    for (i = 0U; i < count; ++i) {
        if (left >= records[i].length || right >= records[i].length) continue;
        ++joint[(uint32_t)records[i].bytes[left] * 256U + records[i].bytes[right]];
        ++n;
    }
    if (n < 2U) return 0.0;
    /* MATLAB forms pXY first and obtains both marginals by floating-point
     * summation.  Do not short-circuit a constant column: its epsilon-level
     * MI is part of the original m4_normalized_mutual_information_pair. */
    for (a = 0U; a < 256U; ++a)
        for (b = 0U; b < 256U; ++b) {
            const double pxy = (double)joint[a * 256U + b] / n;
            px[a] += pxy;
            py[b] += pxy;
        }
    hx = 0.0; hy = 0.0;
    for (a = 0U; a < 256U; ++a) {
        if (px[a] > 0.0) hx -= px[a] * log2(px[a]);
        if (py[a] > 0.0) hy -= py[a] * log2(py[a]);
    }
    for (a = 0U; a < 256U; ++a) if (px[a] > 0.0)
        for (b = 0U; b < 256U; ++b) {
            const uint32_t frequency = joint[a * 256U + b];
            if (frequency != 0U) {
                const double pxy = (double)frequency / n;
                mi += pxy * log2(pxy / (px[a] * py[b]));
            }
        }
    return fmax(0.0, fmin(1.0, mi / fmax(fmin(hx, hy), DBL_EPSILON)));
}

wrj_status_t m4_matlab_compute_position_features(const m4_matlab_record_view_t *records,
    uint32_t count, m4_matlab_category_stats_t *out)
{
    uint32_t *joint;
    uint32_t i, position, max_length = 0U, crc_valid = 0U, crc_pass = 0U;
    if (records == NULL || out == NULL || count == 0U) return WRJ_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    for (i = 0U; i < count; ++i)
        max_length = WRJ_MAX(max_length, records[i].length);
    if (max_length > WRJ_MATLAB_MAX_FRAME_BYTES) return WRJ_ERR_ARGUMENT;
    out->length = (uint8_t)max_length;
    out->observation_count = count;
    joint = (uint32_t *)malloc(256U * 256U * sizeof(*joint));
    if (joint == NULL) return WRJ_ERR_CAPACITY;
    for (i = 0U; i < count; ++i) if (records[i].length >= 3U) {
        const uint8_t *bytes = records[i].bytes;
        const uint32_t n = records[i].length;
        const uint16_t actual = (uint16_t)(((uint16_t)bytes[n - 2U] << 8U) | bytes[n - 1U]);
        ++crc_valid;
        if (m4_crc16_ccitt(bytes, n - 2U) == actual) ++crc_pass;
    }
    out->crc_pass_rate = crc_valid != 0U ? (double)crc_pass / crc_valid : NAN;
    for (position = 0U; position < max_length; ++position) {
        uint32_t histogram[256] = {0U}, delta_histogram[256] = {0U};
        uint32_t support = 0U, unique = 0U, minimum = 255U, maximum = 0U;
        uint32_t changes = 0U, increments = 0U, differences = 0U, nonzero = 0U;
        uint32_t matched_lengths = 0U, most_common = 0U, d;
        m4_matlab_position_t *feature = &out->positions[position];
        for (i = 0U; i < count; ++i) if (position < records[i].length) {
            const uint8_t value = records[i].bytes[position];
            ++histogram[value]; ++support;
            minimum = WRJ_MIN(minimum, value); maximum = WRJ_MAX(maximum, value);
            if (value == records[i].length) ++matched_lengths;
            if (i > 0U && position < records[i - 1U].length &&
                records[i].source_hash == records[i - 1U].source_hash &&
                records[i].observation_index == records[i - 1U].observation_index + 1U) {
                const uint8_t delta = (uint8_t)(value - records[i - 1U].bytes[position]);
                ++differences;
                if (delta != 0U) {
                    ++changes; ++nonzero; ++delta_histogram[delta];
                }
                if (delta == 1U) ++increments;
            }
        }
        for (d = 0U; d < 256U; ++d) {
            if (histogram[d] != 0U) ++unique;
            if (delta_histogram[d] > most_common) most_common = delta_histogram[d];
        }
        feature->support = (double)support / count;
        feature->entropy = m4_stat_entropy(histogram, 256U, support) / 8.0;
        if (support != 0U) {
            feature->unique_ratio = (double)unique / support;
            feature->value_range = (double)(maximum - minimum) / 255.0;
            feature->length_match_score = (double)matched_lengths / support;
        }
        if (differences != 0U) {
            feature->change_rate = (double)changes / differences;
            feature->sequence_score = (double)increments / differences;
        }
        if (nonzero != 0U) feature->delta_stability = (double)most_common / nonzero;
        feature->state_association = m4_stat_nmi(records, count, position, 0, joint);
        feature->type_association = m4_stat_nmi(records, count, position, 1, joint);
    }
    for (position = 0U; position < max_length; ++position) {
        double nmi = 0.0;
        uint32_t neighbors = 0U;
        if (position > 0U) {
            nmi += m4_stat_pair_nmi(records, count, position, position - 1U, joint);
            ++neighbors;
        }
        if (position + 1U < max_length) {
            nmi += m4_stat_pair_nmi(records, count, position, position + 1U, joint);
            ++neighbors;
        }
        if (neighbors != 0U) out->positions[position].neighbor_mutual_info = nmi / neighbors;
    }
    if (max_length >= 2U) {
        out->positions[max_length - 2U].checksum_evidence = out->crc_pass_rate;
        out->positions[max_length - 1U].checksum_evidence = out->crc_pass_rate;
    }
    free(joint);
    return WRJ_OK;
}

wrj_status_t m4_matlab_cluster_byte_positions(m4_matlab_category_stats_t *stats,
    uint8_t requested_clusters)
{
    double values[WRJ_MATLAB_MAX_FRAME_BYTES][10];
    double centers[16][10], normalized[WRJ_MATLAB_MAX_FRAME_BYTES][10];
    double means[10] = {0.0}, scale[10] = {0.0};
    uint8_t ids[WRJ_MATLAB_MAX_FRAME_BYTES] = {0U};
    const uint32_t n = stats != NULL ? stats->length : 0U;
    const uint32_t clusters = WRJ_MIN(WRJ_MIN(requested_clusters, 16U), n);
    uint32_t i, j, k, iteration;
    if (stats == NULL || n == 0U || clusters == 0U) return WRJ_ERR_ARGUMENT;
    for (i = 0U; i < n; ++i) {
        const m4_matlab_position_t *f = &stats->positions[i];
        double *v = values[i];
        v[0] = f->entropy; v[1] = f->change_rate; v[2] = f->unique_ratio;
        v[3] = f->value_range; v[4] = f->sequence_score;
        v[5] = f->delta_stability; v[6] = f->state_association;
        v[7] = f->type_association; v[8] = f->length_match_score;
        v[9] = f->checksum_evidence;
        for (j = 0U; j < 10U; ++j) means[j] += v[j];
    }
    for (j = 0U; j < 10U; ++j) means[j] /= n;
    for (i = 0U; i < n; ++i)
        for (j = 0U; j < 10U; ++j) {
            const double delta = values[i][j] - means[j];
            scale[j] += delta * delta;
        }
    for (j = 0U; j < 10U; ++j) {
        scale[j] = n > 1U ? sqrt(scale[j] / (n - 1U)) : 1.0;
        if (!isfinite(scale[j]) || scale[j] < 1e-8) scale[j] = 1.0;
    }
    for (i = 0U; i < n; ++i)
        for (j = 0U; j < 10U; ++j) {
            double z = (values[i][j] - means[j]) / scale[j];
            normalized[i][j] = isfinite(z) ? z : 0.0;
        }
    {
        double best = INFINITY;
        uint32_t first = 0U;
        for (i = 0U; i < n; ++i) {
            double distance = 0.0;
            for (j = 0U; j < 10U; ++j) distance += normalized[i][j] * normalized[i][j];
            if (distance < best) { best = distance; first = i; }
        }
        memcpy(centers[0], normalized[first], sizeof(centers[0]));
    }
    for (k = 1U; k < clusters; ++k) {
        double farthest = -1.0;
        uint32_t farthest_index = 0U;
        for (i = 0U; i < n; ++i) {
            double closest = INFINITY;
            uint32_t c;
            for (c = 0U; c < k; ++c) {
                double distance = 0.0;
                for (j = 0U; j < 10U; ++j) {
                    const double delta = normalized[i][j] - centers[c][j];
                    distance += delta * delta;
                }
                closest = fmin(closest, distance);
            }
            if (closest > farthest) { farthest = closest; farthest_index = i; }
        }
        memcpy(centers[k], normalized[farthest_index], sizeof(centers[k]));
    }
    for (iteration = 0U; iteration < 100U; ++iteration) {
        uint32_t changed = 0U;
        for (i = 0U; i < n; ++i) {
            double best = INFINITY;
            uint8_t chosen = 0U;
            for (k = 0U; k < clusters; ++k) {
                double distance = 0.0;
                for (j = 0U; j < 10U; ++j) {
                    const double delta = normalized[i][j] - centers[k][j];
                    distance += delta * delta;
                }
                if (distance < best) { best = distance; chosen = (uint8_t)k; }
            }
            if (ids[i] != chosen) ++changed;
            ids[i] = chosen;
        }
        if (iteration > 0U && changed == 0U) break;
        for (k = 0U; k < clusters; ++k) {
            uint32_t members = 0U;
            double sum[10] = {0.0};
            for (i = 0U; i < n; ++i) if (ids[i] == k) {
                ++members;
                for (j = 0U; j < 10U; ++j) sum[j] += normalized[i][j];
            }
            if (members != 0U)
                for (j = 0U; j < 10U; ++j) centers[k][j] = sum[j] / members;
        }
    }
    for (i = 0U; i < n; ++i) stats->positions[i].cluster_id = (uint8_t)(ids[i] + 1U);
    for (k = 0U; k < clusters; ++k) {
        double checksum = 0.0, sequence = 0.0, delta = 0.0, entropy = 0.0;
        double change = 0.0, state = 0.0, type = 0.0;
        uint32_t members = 0U;
        uint8_t behavior;
        for (i = 0U; i < n; ++i) if (ids[i] == k) {
            const m4_matlab_position_t *f = &stats->positions[i];
            ++members;
            checksum += f->checksum_evidence; sequence += f->sequence_score;
            delta += f->delta_stability; entropy += f->entropy;
            change += f->change_rate; state += f->state_association;
            type += f->type_association;
        }
        if (members == 0U) continue;
        checksum /= members; sequence /= members; delta /= members;
        entropy /= members; change /= members; state /= members; type /= members;
        if (checksum > .5) behavior = 1U; /* 校验关系 */
        else if (sequence > .45 || delta > .65) behavior = 2U; /* 计数/时序 */
        else if (entropy > .62 && change > .70) behavior = 3U; /* 高熵载荷 */
        else if (fmax(state, type) > .35) behavior = 4U; /* 状态/枚举 */
        else if (change < .20) behavior = 5U; /* 固定结构 */
        else behavior = 6U; /* 结构过渡 */
        for (i = 0U; i < n; ++i) if (ids[i] == k)
            stats->positions[i].behavior_class = behavior;
    }
    return WRJ_OK;
}

static void m4_stat_assign(m4_matlab_category_stats_t *stats, uint32_t first,
                           uint32_t end, const char *semantic, double confidence,
                           uint8_t *assigned)
{
    uint32_t p;
    for (p = first; p < end && p < stats->length; ++p) {
        snprintf(stats->positions[p].semantic, sizeof(stats->positions[p].semantic),
                 "%s", semantic);
        stats->positions[p].semantic_confidence = confidence;
        if (assigned != NULL) assigned[p] = 1U;
    }
}

static void m4_stat_fill_evidence(m4_matlab_category_stats_t *stats,
                                  const char *category)
{
    uint32_t p;
    const int remote = strcmp(category, "RemoteID_BLE") == 0;
    const int unknown = strcmp(category, "Unknown_UAV_Link") == 0;
    for (p = 0U; p < stats->length; ++p) {
        m4_matlab_position_t *f = &stats->positions[p];
        const char *evidence = "统计行为尚不足以明确命名";
        if (remote) {
            evidence = "检测到RID|UAS|纬度|经度|高度|速度|航向字段顺序";
            if (strcmp(f->semantic, "RID_Prefix") == 0) evidence = "多帧固定RID前缀";
            else if (strcmp(f->semantic, "Delimiter") == 0) evidence = "ASCII竖线分隔符";
            else if (strcmp(f->semantic, "Checksum") == 0) evidence = "CRC16-CCITT多帧验证";
        } else {
            const char *s = f->semantic;
            if (unknown) {
                if (strcmp(s, "Candidate_Header") == 0) s = "Header";
                else if (strcmp(s, "Protocol_Fingerprint") == 0) s = "Version_Fingerprint";
                else if (strcmp(s, "Candidate_Length") == 0) s = "Length";
                else if (strcmp(s, "Candidate_Type") == 0) s = "Type";
                else if (strcmp(s, "Candidate_Sequence") == 0) s = "Sequence";
                else if (strcmp(s, "Candidate_State") == 0) s = "Status";
                else if (strcmp(s, "Candidate_Time") == 0) s = "Timestamp";
                else if (strcmp(s, "Opaque_Payload") == 0) s = "Payload";
                else if (strcmp(s, "Candidate_Checksum") == 0) s = "Checksum";
            }
            if (strcmp(s, "Checksum") == 0) evidence = "末尾两字节通过 CRC16-CCITT 多帧验证";
            else if (strcmp(s, "Header") == 0) evidence = "帧首低熵固定模式";
            else if (strcmp(s, "Length") == 0) evidence = "候选字段值与实际帧长一致";
            else if (strcmp(s, "Sequence") == 0) evidence = "同一候选内呈稳定逐帧递增";
            else if (strcmp(s, "Timestamp") == 0) evidence = "四字节数值呈稳定正增量";
            else if (strcmp(s, "Status") == 0) evidence = "字段变化与外部飞行状态高度关联";
            else if (strcmp(s, "Type") == 0) evidence = "低基数字段与消息类型标签高度关联";
            else if (strcmp(s, "Identifier_Fingerprint") == 0)
                evidence = "同一设备内稳定、跨设备变化，判为设备标识/协议指纹";
            else if (strcmp(s, "Version_Fingerprint") == 0)
                evidence = "跨帧低熵稳定字节，可作为版本或协议指纹";
            else if (strcmp(s, "Payload") == 0) evidence = "高熵且高变化，判为业务载荷或密文区域";
            else if (strcmp(s, "Unknown_Structural") == 0)
                evidence = "保留为未知结构字段，避免过度解释";
        }
        snprintf(f->evidence, sizeof(f->evidence), "%s%s",
                 unknown ? "未知协议：" : "", evidence);
    }
}

static uint64_t m4_stat_decode(const m4_matlab_record_view_t *r,
                               uint32_t start, uint32_t width, int little)
{
    uint64_t value = 0U;
    uint32_t k;
    for (k = 0U; k < width; ++k) {
        const uint32_t index = little ? start + width - 1U - k : start + k;
        value = (value << 8U) | r->bytes[index];
    }
    return value;
}

static int m4_stat_available(const m4_matlab_record_view_t *r,
                             uint32_t start, uint32_t width)
{
    return start + width <= r->length;
}

static double m4_stat_length_score(const m4_matlab_record_view_t *records,
                                   uint32_t count, uint32_t start,
                                   uint32_t width, int little)
{
    uint32_t i, correct = 0U;
    for (i = 0U; i < count; ++i)
        if (m4_stat_available(&records[i], start, width) &&
            m4_stat_decode(&records[i], start, width, little) == records[i].length)
            ++correct;
    return (double)correct / count;
}

static double m4_stat_increment_score(const m4_matlab_record_view_t *records,
                                      uint32_t count, uint32_t start,
                                      uint32_t width, int little)
{
    uint32_t i, valid = 0U, correct = 0U;
    const uint64_t modulus = UINT64_C(1) << (8U * width);
    for (i = 1U; i < count; ++i) {
        if (records[i].source_hash != records[i - 1U].source_hash ||
            records[i].observation_index != records[i - 1U].observation_index + 1U ||
            !m4_stat_available(&records[i], start, width) ||
            !m4_stat_available(&records[i - 1U], start, width)) continue;
        ++valid;
        if ((m4_stat_decode(&records[i], start, width, little) + modulus -
             m4_stat_decode(&records[i - 1U], start, width, little)) % modulus == 1U)
            ++correct;
    }
    return valid != 0U ? (double)correct / valid : 0.0;
}

static double m4_stat_time_score(const m4_matlab_record_view_t *records,
                                 uint32_t count, uint32_t start, int little,
                                 uint64_t *most_common)
{
    uint64_t *deltas;
    uint32_t i, n = 0U, best = 0U;
    double score = 0.0;
    *most_common = 0U;
    deltas = (uint64_t *)malloc((size_t)count * sizeof(*deltas));
    if (deltas == NULL) return 0.0;
    for (i = 1U; i < count; ++i) {
        uint64_t a, b;
        if (records[i].source_hash != records[i - 1U].source_hash ||
            records[i].observation_index != records[i - 1U].observation_index + 1U ||
            !m4_stat_available(&records[i], start, 4U) ||
            !m4_stat_available(&records[i - 1U], start, 4U)) continue;
        a = m4_stat_decode(&records[i], start, 4U, little);
        b = m4_stat_decode(&records[i - 1U], start, 4U, little);
        if (a > b) deltas[n++] = a - b;
    }
    for (i = 0U; i < n; ++i) {
        uint32_t j, frequency = 0U;
        for (j = 0U; j < n; ++j) if (deltas[j] == deltas[i]) ++frequency;
        if (frequency > best || (frequency == best && deltas[i] < *most_common)) {
            best = frequency; *most_common = deltas[i];
        }
    }
    if (n != 0U) score = (double)best / n;
    free(deltas);
    return score;
}

wrj_status_t m4_matlab_infer_semantics(const m4_matlab_record_view_t *records,
    uint32_t count, const char *category, double confidence_threshold,
    m4_matlab_category_stats_t *stats)
{
    uint8_t assigned[WRJ_MATLAB_MAX_FRAME_BYTES] = {0U};
    uint32_t p, w, endian;
    double best;
    uint32_t best_start = 0U, best_width = 0U;
    if (records == NULL || count == 0U || category == NULL || stats == NULL)
        return WRJ_ERR_ARGUMENT;
    for (p = 0U; p < stats->length; ++p)
        m4_stat_assign(stats, p, p + 1U, "Unknown_Structural", .30, NULL);
    if (strcmp(category, "RemoteID_BLE") == 0) {
        uint32_t prefix_count = 0U, delimiter_count = 0U, delimiter_total = 0U, i;
        static const uint8_t delimiter[] = {3U, 13U, 23U, 34U, 41U, 47U};
        for (p = 0U; p < stats->length; ++p)
            m4_stat_assign(stats, p, p + 1U, "Payload", .78, NULL);
        m4_stat_assign(stats, 0U, 3U, "RID_Prefix", .92, NULL);
        m4_stat_assign(stats, 3U, 4U, "Delimiter", .92, NULL);
        m4_stat_assign(stats, 4U, 13U, "UAS_ID", .92, NULL);
        m4_stat_assign(stats, 13U, 14U, "Delimiter", .92, NULL);
        m4_stat_assign(stats, 14U, 23U, "Latitude", .92, NULL);
        m4_stat_assign(stats, 23U, 24U, "Delimiter", .92, NULL);
        m4_stat_assign(stats, 24U, 34U, "Longitude", .92, NULL);
        m4_stat_assign(stats, 34U, 35U, "Delimiter", .92, NULL);
        m4_stat_assign(stats, 35U, 41U, "Altitude", .92, NULL);
        m4_stat_assign(stats, 41U, 42U, "Delimiter", .92, NULL);
        m4_stat_assign(stats, 42U, 47U, "Speed", .92, NULL);
        m4_stat_assign(stats, 47U, 48U, "Delimiter", .92, NULL);
        m4_stat_assign(stats, 48U, 53U, "Heading", .92, NULL);
        m4_stat_assign(stats, 53U, 55U, "Checksum", .92, NULL);
        for (i = 0U; i < count; ++i) {
            if (records[i].length >= 3U && records[i].bytes[0] == 'R' &&
                records[i].bytes[1] == 'I' && records[i].bytes[2] == 'D') ++prefix_count;
            for (p = 0U; p < sizeof(delimiter); ++p)
                if (records[i].length > delimiter[p]) {
                    ++delimiter_total;
                    if (records[i].bytes[delimiter[p]] == '|') ++delimiter_count;
                }
        }
        m4_stat_assign(stats, 0U, 3U, "RID_Prefix",
                       .70 + .30 * (double)prefix_count / count, NULL);
        for (p = 0U; p < sizeof(delimiter); ++p)
            m4_stat_assign(stats, delimiter[p], delimiter[p] + 1U, "Delimiter",
                           .70 + .30 * (delimiter_total != 0U ?
                           (double)delimiter_count / delimiter_total : 0.0), NULL);
        if (stats->length >= 55U)
            m4_stat_assign(stats, 53U, 55U, "Checksum",
                           .55 + .45 * stats->crc_pass_rate, NULL);
        m4_stat_fill_evidence(stats, category);
        return WRJ_OK;
    }
    if (stats->length >= 2U && stats->crc_pass_rate >= .80)
        m4_stat_assign(stats, stats->length - 2U, stats->length, "Checksum",
                       .55 + .45 * stats->crc_pass_rate, assigned);
    if (stats->length >= 2U) {
        const m4_matlab_position_t *a = &stats->positions[0], *b = &stats->positions[1];
        const double header = .5 * ((1.0 - a->entropy) * (1.0 - a->change_rate) * a->support +
                                      (1.0 - b->entropy) * (1.0 - b->change_rate) * b->support);
        if (header >= .70) m4_stat_assign(stats, 0U, 2U, "Header", header, assigned);
    }
    best = 0.0; best_start = 0U; best_width = 0U;
    for (w = 1U; w <= 2U; ++w) for (p = 0U; p + w <= stats->length; ++p) {
        double score;
        if (assigned[p] || (w == 2U && assigned[p + 1U])) continue;
        for (endian = 0U; endian < 2U; ++endian) {
            score = m4_stat_length_score(records, count, p, w, (int)endian);
            if (score > best + 1e-12) {
                best = score; best_start = p; best_width = w;
            }
        }
    }
    if (best >= .80) m4_stat_assign(stats, best_start, best_start + best_width,
                                    "Length", best, assigned);
    best = 0.0; best_start = 0U; best_width = 0U;
    for (w = 1U; w <= 2U; ++w) for (p = 0U; p + w <= stats->length; ++p) {
        if (assigned[p] || (w == 2U && assigned[p + 1U])) continue;
        for (endian = 0U; endian < 2U; ++endian) {
            const double score = m4_stat_increment_score(records, count, p, w, (int)endian);
            if (score > best) { best = score; best_start = p; best_width = w; }
        }
    }
    if (best >= .65) m4_stat_assign(stats, best_start, best_start + best_width,
                                    "Sequence", best, assigned);
    best = 0.0; best_start = 0U;
    for (p = 0U; p + 4U <= stats->length; ++p) {
        uint64_t delta;
        if (assigned[p] || assigned[p + 1U] || assigned[p + 2U] || assigned[p + 3U]) continue;
        for (endian = 0U; endian < 2U; ++endian) {
            const double score = m4_stat_time_score(records, count, p, (int)endian, &delta);
            if (delta > 1U && score > best) { best = score; best_start = p; }
        }
    }
    if (best >= .65) m4_stat_assign(stats, best_start, best_start + 4U,
                                    "Timestamp", best, assigned);
    best = -INFINITY; best_start = 0U;
    for (p = 0U; p < stats->length; ++p) if (!assigned[p] &&
        stats->positions[p].state_association > best) {
        best = stats->positions[p].state_association; best_start = p;
    }
    if (best >= .45) m4_stat_assign(stats, best_start, best_start + 1U,
                                    "Status", best, assigned);
    best = -INFINITY; best_start = 0U;
    for (p = 0U; p < stats->length; ++p) if (!assigned[p] &&
        stats->positions[p].type_association > best) {
        best = stats->positions[p].type_association; best_start = p;
    }
    if (best >= .45) m4_stat_assign(stats, best_start, best_start + 1U,
                                    "Type", best, assigned);
    if (strcmp(category, "DJI_DroneID") == 0)
        for (p = 0U; p < stats->length; ++p) if (!assigned[p] &&
            stats->positions[p].change_rate <= .12 && stats->positions[p].entropy > .25) {
            const double conf = fmin(.96, .72 + .20 * (1.0 - stats->positions[p].change_rate));
            m4_stat_assign(stats, p, p + 1U, "Identifier_Fingerprint", conf, assigned);
        }
    for (p = 0U; p < stats->length; ++p) if (!assigned[p]) {
        const m4_matlab_position_t *f = &stats->positions[p];
        if (f->entropy <= .25 && f->change_rate <= .25)
            m4_stat_assign(stats, p, p + 1U, "Version_Fingerprint",
                           fmax(.55, .85 * (1.0 - f->entropy) * (1.0 - f->change_rate)), NULL);
        else if (f->entropy >= .45 && f->change_rate >= .45)
            m4_stat_assign(stats, p, p + 1U, "Payload",
                           fmin(.95, .45 + .35 * f->entropy + .20 * f->change_rate), NULL);
        else
            m4_stat_assign(stats, p, p + 1U, "Unknown_Structural",
                           fmax(confidence_threshold - .10,
                                .35 + .20 * f->neighbor_mutual_info), NULL);
    }
    if (strcmp(category, "Unknown_UAV_Link") == 0)
        for (p = 0U; p < stats->length; ++p) {
            m4_matlab_position_t *f = &stats->positions[p];
            const char *mapped = NULL;
            if (strcmp(f->semantic, "Header") == 0) mapped = "Candidate_Header";
            else if (strcmp(f->semantic, "Version_Fingerprint") == 0) mapped = "Protocol_Fingerprint";
            else if (strcmp(f->semantic, "Length") == 0) mapped = "Candidate_Length";
            else if (strcmp(f->semantic, "Type") == 0) mapped = "Candidate_Type";
            else if (strcmp(f->semantic, "Sequence") == 0) mapped = "Candidate_Sequence";
            else if (strcmp(f->semantic, "Status") == 0) mapped = "Candidate_State";
            else if (strcmp(f->semantic, "Timestamp") == 0) mapped = "Candidate_Time";
            else if (strcmp(f->semantic, "Payload") == 0) mapped = "Opaque_Payload";
            else if (strcmp(f->semantic, "Checksum") == 0) mapped = "Candidate_Checksum";
            if (mapped != NULL) snprintf(f->semantic, sizeof(f->semantic), "%s", mapped);
            f->semantic_confidence = fmin(f->semantic_confidence, .88);
        }
    m4_stat_fill_evidence(stats, category);
    return WRJ_OK;
}

static const char *m4_stat_parser_data_type(const char *semantic, uint32_t width)
{
    const char *s = semantic;
    if (strncmp(s, "Candidate_Header", 16U) == 0) s = "Header";
    else if (strncmp(s, "Protocol_Fingerprint", 20U) == 0) s = "Version_Fingerprint";
    else if (strncmp(s, "Candidate_Length", 16U) == 0) s = "Length";
    else if (strncmp(s, "Candidate_Type", 14U) == 0) s = "Type";
    else if (strncmp(s, "Candidate_Sequence", 18U) == 0) s = "Sequence";
    else if (strncmp(s, "Candidate_State", 15U) == 0) s = "Status";
    else if (strncmp(s, "Candidate_Time", 14U) == 0) s = "Timestamp";
    else if (strncmp(s, "Opaque_Payload", 14U) == 0) s = "Payload";
    else if (strncmp(s, "Candidate_Checksum", 18U) == 0) s = "Checksum";
    if (strcmp(s, "Header") == 0 || strcmp(s, "Version_Fingerprint") == 0 ||
        strcmp(s, "Identifier_Fingerprint") == 0) return "bytes/fingerprint";
    if (strcmp(s, "RID_Prefix") == 0) return "ascii[3]";
    if (strcmp(s, "Delimiter") == 0) return "ascii_separator";
    if (strcmp(s, "UAS_ID") == 0) return "ascii[9]";
    if (strcmp(s, "Latitude") == 0 || strcmp(s, "Longitude") == 0 ||
        strcmp(s, "Altitude") == 0 || strcmp(s, "Speed") == 0 ||
        strcmp(s, "Heading") == 0) return "ascii_numeric";
    if (strcmp(s, "Length") == 0 || strcmp(s, "Type") == 0 ||
        strcmp(s, "Sequence") == 0 || strcmp(s, "Status") == 0)
        return width == 1U ? "uint8" : (width == 2U ? "uint16_be" : "uint_be");
    if (strcmp(s, "Timestamp") == 0) return "uint32_be";
    if (strcmp(s, "Checksum") == 0) return "crc16_ccitt_be";
    if (strcmp(s, "Payload") == 0) return "opaque_bytes";
    return "unknown_bytes";
}

static int m4_stat_mode_label(const m4_matlab_record_view_t *records,
    uint32_t count, uint32_t position)
{
    uint32_t counts[32] = {0U}, first[32], i, best = 0U;
    for (i = 0U; i < 32U; ++i) first[i] = UINT32_MAX;
    for (i = 0U; i < count; ++i) if (records[i].labels != NULL &&
        position < records[i].length && records[i].labels[position] < 32U) {
        const uint8_t label = records[i].labels[position];
        ++counts[label];
        if (first[label] == UINT32_MAX) first[label] = i;
    }
    for (i = 0U; i < 32U; ++i) if (counts[i] > counts[best] ||
        (counts[i] == counts[best] && first[i] < first[best])) best = i;
    return counts[best] != 0U ? (int)best : -1;
}

wrj_status_t m4_matlab_merge_field_template(const m4_matlab_record_view_t *records,
    uint32_t count, const m4_matlab_category_stats_t *stats,
    m4_matlab_field_template_t *out)
{
    uint32_t begin = 0U, end, i;
    if (records == NULL || count == 0U || stats == NULL || out == NULL)
        return WRJ_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    while (begin < stats->length) {
        m4_matlab_template_field_t *field = &out->fields[out->count++];
        uint32_t cluster_counts[256] = {0U}, behavior_counts[256] = {0U};
        uint32_t truth_counts[32] = {0U}, truth_first[32];
        uint8_t min_value = UINT8_MAX, max_value = 0U;
        uint32_t value_count = 0U, selected_truth = 0U;
        double confidence_sum = 0.0;
        for (i = 0U; i < 32U; ++i) truth_first[i] = UINT32_MAX;
        end = begin + 1U;
        while (end < stats->length &&
            strcmp(stats->positions[begin].semantic, stats->positions[end].semantic) == 0)
            ++end;
        field->start_byte = (uint8_t)(begin + 1U);
        field->end_byte = (uint8_t)end;
        field->length_bytes = (uint8_t)(end - begin);
        snprintf(field->semantic, sizeof(field->semantic), "%s", stats->positions[begin].semantic);
        snprintf(field->parser_data_type, sizeof(field->parser_data_type), "%s",
                 m4_stat_parser_data_type(field->semantic, end - begin));
        field->validation_match = 1U;
        for (i = begin; i < end; ++i) {
            const m4_matlab_position_t *position = &stats->positions[i];
            uint32_t r;
            confidence_sum += position->semantic_confidence;
            ++cluster_counts[position->cluster_id];
            ++behavior_counts[position->behavior_class];
            {
                const int truth_label = m4_stat_mode_label(records, count, i);
                if (truth_label >= 0) {
                    ++truth_counts[truth_label];
                    if (truth_first[truth_label] == UINT32_MAX) truth_first[truth_label] = i;
                    if (strcmp(field->semantic,
                               m4_matlab_label_name((uint8_t)truth_label)) != 0)
                        field->validation_match = 0U;
                }
            }
            for (r = 0U; r < count; ++r) {
                if (i < records[r].length) {
                    const uint8_t value = records[r].bytes[i];
                    if (value < min_value) min_value = value;
                    if (value > max_value) max_value = value;
                    ++value_count;
                }
            }
        }
        field->confidence = confidence_sum / (end - begin);
        {
            uint32_t best_count = 0U;
            for (i = begin; i < end; ++i) {
                uint32_t j, frequency = 0U;
                for (j = begin; j < end; ++j)
                    if (strcmp(stats->positions[i].evidence,
                               stats->positions[j].evidence) == 0) ++frequency;
                if (frequency > best_count) {
                    best_count = frequency;
                    snprintf(field->evidence, sizeof(field->evidence), "%s",
                             stats->positions[i].evidence);
                }
            }
        }
        for (i = 1U; i < 256U; ++i)
            if (cluster_counts[i] > cluster_counts[field->cluster_id]) field->cluster_id = (uint8_t)i;
        for (i = begin; i < end; ++i) {
            const uint8_t behavior = stats->positions[i].behavior_class;
            if (behavior_counts[behavior] > behavior_counts[field->behavior_class])
                field->behavior_class = behavior;
        }
        for (i = 0U; i < 32U; ++i)
            if (truth_counts[i] > truth_counts[selected_truth] ||
                (truth_counts[i] == truth_counts[selected_truth] &&
                 truth_first[i] < truth_first[selected_truth])) selected_truth = i;
        if (truth_counts[selected_truth] != 0U) {
            const char *truth = m4_matlab_label_name((uint8_t)selected_truth);
            snprintf(field->truth_semantic, sizeof(field->truth_semantic), "%s", truth);
            field->truth_available = 1U;
        } else field->validation_match = 0U;
        if (value_count != 0U)
            snprintf(field->value_range, sizeof(field->value_range), "%u..%u",
                     (unsigned)min_value, (unsigned)max_value);
        begin = end;
    }
    return WRJ_OK;
}

wrj_status_t m4_matlab_analyze_category(const m4_matlab_record_view_t *records,
    uint32_t count, const char *category, double confidence_threshold,
    m4_matlab_category_analysis_t *out)
{
    uint32_t i, p, truth_boundaries = 0U, predicted_boundaries = 0U;
    uint32_t matched_boundaries = 0U, correct = 0U, confident = 0U;
    int previous_truth = -1;
    double confidence_sum = 0.0;
    if (records == NULL || count == 0U || category == NULL || out == NULL)
        return WRJ_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (m4_matlab_compute_position_features(records, count, &out->stats) != WRJ_OK ||
        m4_matlab_cluster_byte_positions(&out->stats, 5U) != WRJ_OK ||
        m4_matlab_infer_semantics(records, count, category, confidence_threshold,
                                  &out->stats) != WRJ_OK ||
        m4_matlab_merge_field_template(records, count, &out->stats,
                                       &out->field_template) != WRJ_OK)
        return WRJ_ERR_ARGUMENT;
    out->num_observations = count;
    for (i = 0U; i < count; ++i) {
        uint32_t j;
        int seen = 0;
        for (j = 0U; j < i; ++j)
            if (records[j].source_hash == records[i].source_hash) { seen = 1; break; }
        if (!seen) ++out->num_candidates;
    }
    for (p = 0U; p < out->stats.length; ++p) {
        const m4_matlab_position_t *position = &out->stats.positions[p];
        const int truth = m4_stat_mode_label(records, count, p);
        confidence_sum += position->semantic_confidence;
        if (position->semantic_confidence >= confidence_threshold) ++confident;
        if (truth >= 0) {
            out->truth_available = 1U;
            if (strcmp(position->semantic, m4_matlab_label_name((uint8_t)truth)) == 0)
                ++correct;
            if (p != 0U) {
                const int truth_boundary = truth != previous_truth;
                const int predicted_boundary = strcmp(position->semantic,
                                              out->stats.positions[p - 1U].semantic) != 0;
                truth_boundaries += (uint32_t)truth_boundary;
                predicted_boundaries += (uint32_t)predicted_boundary;
                matched_boundaries += (uint32_t)(truth_boundary && predicted_boundary);
            }
        }
        previous_truth = truth;
    }
    out->mean_semantic_confidence = confidence_sum / out->stats.length;
    out->high_confidence_byte_rate = (double)confident / out->stats.length;
    if (out->truth_available) {
        out->byte_semantic_accuracy = (double)correct / out->stats.length;
        out->boundary_precision = (double)matched_boundaries /
            (predicted_boundaries == 0U ? 1U : predicted_boundaries);
        out->boundary_recall = (double)matched_boundaries /
            (truth_boundaries == 0U ? 1U : truth_boundaries);
        if (out->boundary_precision + out->boundary_recall > 0.0)
            out->boundary_f1 = 2.0 * out->boundary_precision * out->boundary_recall /
                (out->boundary_precision + out->boundary_recall);
    }
    return WRJ_OK;
}

wrj_status_t m4_matlab_make_record_views(const wrj_candidate_t *candidate,
    const m4_result_t *result, m4_matlab_record_view_t *out,
    uint32_t capacity, uint32_t *out_count)
{
    const uint32_t source_hash = candidate != NULL ?
        m4_matlab_stable_string_hash(candidate->source_file) : 0U;
    uint32_t i;
    if (candidate == NULL || result == NULL || out == NULL || out_count == NULL ||
        capacity < result->matlab_observation_count) return WRJ_ERR_ARGUMENT;
    for (i = 0U; i < result->matlab_observation_count; ++i) {
        out[i].bytes = result->matlab_observation_bytes[i];
        out[i].labels = result->matlab_observation_labels[i];
        out[i].length = result->matlab_observation_length[i];
        out[i].source_hash = source_hash;
        out[i].observation_index = (uint16_t)(i + 1U);
        out[i].state_index = result->matlab_observation_state_index[i];
        out[i].message_type = result->matlab_observation_message_type[i];
    }
    *out_count = result->matlab_observation_count;
    return WRJ_OK;
}

wrj_status_t m4_matlab_record_store_init(m4_matlab_record_store_t *store,
    uint32_t capacity)
{
    if (store == NULL || capacity == 0U) return WRJ_ERR_ARGUMENT;
    memset(store, 0, sizeof(*store));
    store->records = (m4_matlab_stored_record_t *)calloc(capacity,
                                                          sizeof(*store->records));
    if (store->records == NULL) return WRJ_ERR_CAPACITY;
    store->capacity = capacity;
    return WRJ_OK;
}

void m4_matlab_record_store_release(m4_matlab_record_store_t *store)
{
    if (store == NULL) return;
    free(store->records);
    memset(store, 0, sizeof(*store));
}

wrj_status_t m4_matlab_record_store_append(m4_matlab_record_store_t *store,
    const wrj_candidate_t *candidate, const m3_result_t *m3,
    const m4_result_t *result)
{
    m4_matlab_record_view_t views[WRJ_MATLAB_MAX_OBSERVATIONS];
    uint32_t count, i;
    const char *category;
    if (store == NULL || store->records == NULL || candidate == NULL || m3 == NULL ||
        result == NULL) return WRJ_ERR_ARGUMENT;
    if (m4_matlab_make_record_views(candidate, result, views,
        WRJ_MATLAB_MAX_OBSERVATIONS, &count) != WRJ_OK) return WRJ_ERR_ARGUMENT;
    if (count > store->capacity - store->count) return WRJ_ERR_CAPACITY;
    category = m4_matlab_category(m3);
    for (i = 0U; i < count; ++i) {
        m4_matlab_stored_record_t *entry = &store->records[store->count++];
        entry->view = views[i];
        memcpy(entry->bytes, views[i].bytes, views[i].length);
        memcpy(entry->labels, views[i].labels, views[i].length);
        entry->view.bytes = entry->bytes;
        entry->view.labels = entry->labels;
        snprintf(entry->category, sizeof(entry->category), "%s", category);
    }
    return WRJ_OK;
}

wrj_status_t m4_matlab_record_store_analyze(const m4_matlab_record_store_t *store,
    const char *category, double confidence_threshold,
    m4_matlab_category_analysis_t *out)
{
    m4_matlab_record_view_t *views;
    uint32_t i, count = 0U;
    wrj_status_t status;
    if (store == NULL || store->records == NULL || category == NULL || out == NULL)
        return WRJ_ERR_ARGUMENT;
    views = (m4_matlab_record_view_t *)malloc((size_t)store->count * sizeof(*views));
    if (views == NULL) return WRJ_ERR_CAPACITY;
    for (i = 0U; i < store->count; ++i)
        if (strcmp(store->records[i].category, category) == 0)
            views[count++] = store->records[i].view;
    status = count != 0U ? m4_matlab_analyze_category(views, count, category,
        confidence_threshold, out) : WRJ_ERR_DATA;
    free(views);
    return status;
}
