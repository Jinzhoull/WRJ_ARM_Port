#ifndef M4_MATLAB_STATISTICS_H
#define M4_MATLAB_STATISTICS_H

#include "m4_matlab_flow.h"

typedef struct {
    const uint8_t *bytes;
    uint8_t length;
    uint32_t source_hash;
    uint16_t observation_index;
    uint8_t state_index;
    uint8_t message_type;
    const uint8_t *labels;
} m4_matlab_record_view_t;

typedef struct {
    double support, entropy, change_rate, unique_ratio, value_range;
    double sequence_score, delta_stability, state_association;
    double type_association, length_match_score, neighbor_mutual_info;
    double checksum_evidence;
    uint8_t cluster_id;
    uint8_t behavior_class;
    char semantic[32];
    double semantic_confidence;
    char evidence[128];
} m4_matlab_position_t;

typedef struct {
    uint8_t length;
    uint32_t observation_count;
    double crc_pass_rate;
    m4_matlab_position_t positions[WRJ_MATLAB_MAX_FRAME_BYTES];
} m4_matlab_category_stats_t;

typedef struct {
    uint8_t start_byte, end_byte, length_bytes, cluster_id, behavior_class;
    double confidence;
    char semantic[32];
    char parser_data_type[32];
    char evidence[128];
    char value_range[24];
    char truth_semantic[32];
    uint8_t truth_available;
    uint8_t validation_match;
} m4_matlab_template_field_t;

typedef struct {
    uint8_t count;
    m4_matlab_template_field_t fields[WRJ_MATLAB_MAX_FRAME_BYTES];
} m4_matlab_field_template_t;

typedef struct {
    m4_matlab_category_stats_t stats;
    m4_matlab_field_template_t field_template;
    uint32_t num_candidates;
    uint32_t num_observations;
    double mean_semantic_confidence;
    double boundary_precision, boundary_recall, boundary_f1;
    double byte_semantic_accuracy, high_confidence_byte_rate;
    uint8_t truth_available;
} m4_matlab_category_analysis_t;

typedef struct {
    m4_matlab_record_view_t view;
    uint8_t bytes[WRJ_MATLAB_MAX_FRAME_BYTES];
    uint8_t labels[WRJ_MATLAB_MAX_FRAME_BYTES];
    char category[32];
} m4_matlab_stored_record_t;

typedef struct {
    m4_matlab_stored_record_t *records;
    uint32_t count, capacity;
} m4_matlab_record_store_t;

wrj_status_t m4_matlab_compute_position_features(const m4_matlab_record_view_t *records,
    uint32_t count, m4_matlab_category_stats_t *out);
wrj_status_t m4_matlab_cluster_byte_positions(m4_matlab_category_stats_t *stats,
    uint8_t requested_clusters);
wrj_status_t m4_matlab_infer_semantics(const m4_matlab_record_view_t *records,
    uint32_t count, const char *category, double confidence_threshold,
    m4_matlab_category_stats_t *stats);
wrj_status_t m4_matlab_merge_field_template(const m4_matlab_record_view_t *records,
    uint32_t count, const m4_matlab_category_stats_t *stats,
    m4_matlab_field_template_t *out);
wrj_status_t m4_matlab_analyze_category(const m4_matlab_record_view_t *records,
    uint32_t count, const char *category, double confidence_threshold,
    m4_matlab_category_analysis_t *out);
wrj_status_t m4_matlab_make_record_views(const wrj_candidate_t *candidate,
    const m4_result_t *result, m4_matlab_record_view_t *out,
    uint32_t capacity, uint32_t *out_count);
wrj_status_t m4_matlab_record_store_init(m4_matlab_record_store_t *store,
    uint32_t capacity);
void m4_matlab_record_store_release(m4_matlab_record_store_t *store);
wrj_status_t m4_matlab_record_store_append(m4_matlab_record_store_t *store,
    const wrj_candidate_t *candidate, const m3_result_t *m3,
    const m4_result_t *result);
wrj_status_t m4_matlab_record_store_analyze(const m4_matlab_record_store_t *store,
    const char *category, double confidence_threshold,
    m4_matlab_category_analysis_t *out);

#endif
