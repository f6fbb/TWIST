#ifndef TWCALL_H
#define TWCALL_H

#include <stdint.h>

/*
 * twcall.h - Callsigns, shared by every protocol: normalization to 6
 * characters, packing into 28 bits, and the plausibility check of the
 * ITU structure (twcall.c).
 */

/* Normalizes a callsign the way the reference encoder does:
 * upper-cases it, prepends a space when the 3rd character is not a
 * digit (so that the digit always lands in position 2 - "F5OEO"
 * becomes " F5OEO"), pads to 6 characters with spaces.
 * Returns 0 if the result is encodable, -1 otherwise (too long, digit
 * not found where the format needs it, portable suffix such as "/P",
 * digit in the 3-letter suffix...). out must hold 7 bytes. */
int tw_normalize_call(const char *in, char out[7]);

/* Packs a normalized 6-character callsign into 28 bits.
 * Returns 0 on success, -1 if a character is outside the format. */
int tw_pack_call(const char norm[7], uint32_t *packed);

/* Unpacks 28 bits into a 6-character callsign (with the padding
 * spaces kept). Returns 0 if every field is in range, -1 otherwise
 * (a value that no encoder could have produced). */
int tw_unpack_call(uint32_t packed, char out[7]);

/* Rough check that a decoded (normalized, 6-char) callsign has the
 * structure of a real amateur callsign: a non-empty suffix, and a
 * prefix from an allocated ITU block (no Q, no 0/1 except 1A, only
 * the single letters B F G I K M N R W as one-character prefixes, only
 * known letter+digit prefixes such as A6, E7, T7...). The classic frame's own
 * error detection is weaker than its 19 CRC bits suggest - the code
 * structure lets a frame with two or three wrong 7-bit groups pass
 * the CRC far more often than 1 in 2^19 (about 1e-3 for two groups)
 * - so this structural check usefully removes about half of the
 * false decodes that slip through near the sensitivity limit.
 * Returns 1 if plausible, 0 otherwise. */
int tw_call_plausible(const char norm[7]);

#endif /* TWCALL_H */
