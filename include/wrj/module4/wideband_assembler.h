#ifndef WRJ_MODULE4_WIDEBAND_ASSEMBLER_H
#define WRJ_MODULE4_WIDEBAND_ASSEMBLER_H

#include "wrj_types.h"

/* Assemble only observations whose IQ start times support adjacent OFDM
 * symbols. The parser itself supplies the header/length/CRC acceptance gate. */
void m4_assemble_dji_wideband(const m3_result_t *m3, m4_result_t *result);

#endif
