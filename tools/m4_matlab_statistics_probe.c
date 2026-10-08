#include "m4_matlab_statistics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    m4_matlab_record_view_t view;
    uint8_t bytes[WRJ_MATLAB_MAX_FRAME_BYTES];
    uint8_t labels[WRJ_MATLAB_MAX_FRAME_BYTES];
} m4_probe_record_t;

static int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int main(int argc, char **argv)
{
    m4_probe_record_t *records;
    m4_matlab_record_view_t *views;
    m4_matlab_category_stats_t stats;
    uint32_t count = 0U, capacity = 2048U, p;
    char line[512];
    records = (m4_probe_record_t *)calloc(capacity, sizeof(*records));
    views = (m4_matlab_record_view_t *)calloc(capacity, sizeof(*views));
    if (records == NULL || views == NULL) return 1;
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *field[6], *token;
        size_t k = 0U, n;
        m4_probe_record_t *record;
        if (count >= capacity) return 2;
        token = strtok(line, "\t\r\n");
        while (token != NULL && k < 6U) {
            field[k++] = token; token = strtok(NULL, "\t\r\n");
        }
        if (k != 5U && k != 6U) return 2;
        record = &records[count];
        record->view.source_hash = (uint32_t)strtoul(field[0], NULL, 10);
        record->view.observation_index = (uint16_t)strtoul(field[1], NULL, 10);
        record->view.state_index = (uint8_t)strtoul(field[2], NULL, 10);
        record->view.message_type = (uint8_t)strtoul(field[3], NULL, 10);
        n = strlen(field[4]);
        if ((n & 1U) != 0U || n / 2U > WRJ_MATLAB_MAX_FRAME_BYTES) return 2;
        for (k = 0U; k < n / 2U; ++k) {
            int hi = digit(field[4][2U * k]), lo = digit(field[4][2U * k + 1U]);
            if (hi < 0 || lo < 0) return 2;
            record->bytes[k] = (uint8_t)((hi << 4) | lo);
        }
        record->view.bytes = record->bytes;
        record->view.length = (uint8_t)(n / 2U);
        if (k == 6U) {
            size_t q;
            if (strlen(field[5]) != n) return 2;
            for (q = 0U; q < n / 2U; ++q) {
                int hi = digit(field[5][2U * q]), lo = digit(field[5][2U * q + 1U]);
                if (hi < 0 || lo < 0) return 2;
                record->labels[q] = (uint8_t)((hi << 4) | lo);
            }
            record->view.labels = record->labels;
        }
        views[count] = record->view;
        ++count;
    }
    if ((argc != 2 && argc != 3) ||
        m4_matlab_compute_position_features(views, count, &stats) != WRJ_OK ||
        m4_matlab_cluster_byte_positions(&stats, 5U) != WRJ_OK ||
        m4_matlab_infer_semantics(views, count, argv[1], .55, &stats) != WRJ_OK) return 1;
    if (argc == 3 && strcmp(argv[2], "--template") == 0) {
        m4_matlab_field_template_t template_result;
        if (m4_matlab_merge_field_template(views, count, &stats, &template_result) != WRJ_OK)
            return 1;
        puts("fieldIndex,startByte,endByte,lengthBytes,inferredSemantic,parserDataType,confidence,behaviorClass,clusterId,evidence,valueRange");
        for (p = 0U; p < template_result.count; ++p) {
            const m4_matlab_template_field_t *f = &template_result.fields[p];
            printf("%u,%u,%u,%u,%s,%s,%.15g,%u,%u,%s,%s\n", p + 1U,
                   f->start_byte, f->end_byte, f->length_bytes, f->semantic,
                   f->parser_data_type, f->confidence, f->behavior_class,
                   f->cluster_id, f->evidence, f->value_range);
        }
        free(views); free(records);
        return 0;
    }
    puts("bytePosition,supportRate,entropyNormalized,changeRate,uniqueRatio,valueRangeNormalized,sequenceScore,deltaStability,stateAssociation,typeAssociation,lengthMatchScore,neighborMutualInfo,checksumEvidence,clusterId,behaviorClass,inferredSemantic,semanticConfidence");
    for (p = 0U; p < stats.length; ++p) {
        const m4_matlab_position_t *f = &stats.positions[p];
        printf("%u,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%u,%u,%s,%.15g\n",
               p + 1U, f->support, f->entropy, f->change_rate,
               f->unique_ratio, f->value_range, f->sequence_score,
               f->delta_stability, f->state_association, f->type_association,
               f->length_match_score, f->neighbor_mutual_info,
               f->checksum_evidence, f->cluster_id, f->behavior_class,
               f->semantic, f->semantic_confidence);
    }
    free(views); free(records);
    return 0;
}
