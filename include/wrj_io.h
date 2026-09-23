#ifndef WRJ_IO_H
#define WRJ_IO_H

#include "wrj_types.h"

wrj_status_t wrj_load_handoff_candidate(const char *csv_path, const char *candidate_id,
                                        wrj_candidate_t *candidate);
wrj_status_t wrj_read_cf32(const char *path, wrj_cf32_t *samples, uint32_t capacity,
                            uint32_t *count);
wrj_status_t wrj_write_results(const char *output_dir, const wrj_candidate_t *candidate,
                               const m3_result_t *m3_result, const m4_result_t *m4_result);
wrj_status_t wrj_make_directory(const char *path);
const char *wrj_profile_name(wrj_profile_kind_t profile);
const char *wrj_m4_status_name(m4_status_t status);

#endif
