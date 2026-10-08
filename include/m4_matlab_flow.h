#ifndef M4_MATLAB_FLOW_H
#define M4_MATLAB_FLOW_H

#include "wrj_types.h"

/* Byte-producing helpers corresponding to Module4.m's validation-frame
 * construction. Presentation figures and MATLAB table serialization are
 * deliberately outside the ARM receiver interface. */
typedef struct {
    uint8_t bytes[64];
    uint8_t labels[64];
    uint8_t count;
} m4_matlab_frame_t;

typedef enum {
    M4_MATLAB_PAYLOAD = 0,
    M4_MATLAB_HEADER,
    M4_MATLAB_VERSION,
    M4_MATLAB_RESERVED,
    M4_MATLAB_LENGTH,
    M4_MATLAB_TYPE,
    M4_MATLAB_SEQUENCE,
    M4_MATLAB_STATUS,
    M4_MATLAB_TIMESTAMP,
    M4_MATLAB_IDENTIFIER,
    M4_MATLAB_CHECKSUM,
    M4_MATLAB_RID_PREFIX,
    M4_MATLAB_DELIMITER,
    M4_MATLAB_UAS_ID,
    M4_MATLAB_LATITUDE,
    M4_MATLAB_LONGITUDE,
    M4_MATLAB_ALTITUDE,
    M4_MATLAB_SPEED,
    M4_MATLAB_HEADING,
    M4_MATLAB_CANDIDATE_HEADER,
    M4_MATLAB_PROTOCOL_FINGERPRINT,
    M4_MATLAB_CANDIDATE_LENGTH,
    M4_MATLAB_CANDIDATE_TYPE,
    M4_MATLAB_CANDIDATE_SEQUENCE,
    M4_MATLAB_CANDIDATE_STATE,
    M4_MATLAB_CANDIDATE_TIME,
    M4_MATLAB_OPAQUE_PAYLOAD,
    M4_MATLAB_CANDIDATE_CHECKSUM,
    M4_MATLAB_CANDIDATE_UNKNOWN
} m4_matlab_label_t;

typedef struct {
    uint32_t state[624];
    uint32_t index;
} m4_matlab_rng_t;

uint32_t m4_matlab_stable_string_hash(const char *source_file);
wrj_status_t m4_matlab_build_validation_frame(const char *category,
    const uint8_t *raw, uint16_t raw_count, const char *source_file,
    uint8_t sequence, uint8_t message_type, uint8_t state_index,
    uint32_t timestamp, uint32_t observation_index, m4_matlab_frame_t *out);
wrj_status_t m4_matlab_build_remoteid_frame(const m4_result_t *remote,
    m4_matlab_frame_t *out);
int m4_matlab_crc16_recover_unique_single_bit(uint8_t *bytes, uint16_t count);
void m4_matlab_rng_seed(m4_matlab_rng_t *rng, uint32_t seed);
double m4_matlab_rand(m4_matlab_rng_t *rng);
void m4_matlab_apply_validation_channel(m4_matlab_frame_t *frame,
    double sync_confidence, const char *category, m4_matlab_rng_t *rng);
const char *m4_matlab_label_name(uint8_t label);
const char *m4_matlab_category(const m3_result_t *m3);
wrj_status_t m4_matlab_attach_observations(const wrj_candidate_t *candidate,
    const m3_result_t *m3, m4_result_t *m4, m4_matlab_rng_t *rng);

#endif
