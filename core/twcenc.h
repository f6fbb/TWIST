#ifndef TWCENC_H
#define TWCENC_H

#include <stdint.h>
#include "twcdefs.h"

/*
 * twcenc.h - classic message coding: callsign packing/unpacking,
 * CRCs, and the full transmit chain down to the 239 on/off chips.
 *
 * Every function here is a faithful port of the F4GCB/7L1RLL
 * reference code (see twdefs.h for provenance), validated bit-exact
 * against the three reference transmissions those sources carry
 * (tests/test_encoder.c). The decoder (twcdec.c) reuses the same
 * functions to re-check a candidate frame, so transmit and receive
 * can never drift apart.
 */

#include "twcall.h"

/* Builds the 51-bit pre-scrambling vector "0000" | 28 call bits |
 * CRC16 | CRC3, one bit per byte, MSB first. */
void twc_build_vector(uint32_t packed, unsigned char vec[TWC_NVEC]);

/* Checks a 51-bit (descrambled) vector: sync bits 0000, CRC16, CRC3.
 * Returns 0 and fills *packed if all checks pass, -1 otherwise. */
int twc_check_vector(const unsigned char vec[TWC_NVEC], uint32_t *packed);

/* Full chain: callsign -> 239 chips (1 = carrier on).
 * Returns 0 on success, -1 if the callsign cannot be encoded.
 * norm_out (may be NULL) receives the normalized callsign. */
int twc_encode(const char *call, unsigned char chips[TWC_NCHIPS], char norm_out[7]);

/* Position, in the 119-bit (pre-interleaving) coded vector, of the
 * bit carried by interleaved slot idx (0..118). Chip pair idx is
 * chips[1 + 2*idx], chips[2 + 2*idx]. */
static inline int twc_deinterleave_pos(int idx)
{
    return (idx / TWC_NGROUPS) + 7 * (idx % TWC_NGROUPS);
}

#endif /* TWCENC_H */
