/*
 * twcdefs.h - Constants of the "classic" frame (TW_PROTO_CLASSIC: callsign only, 239 chips).
 *
 * The classic frame's coding chain is the one published as open source
 * (GPL) by F4GCB and 7L1RLL - see CREDITS.md for the provenance and
 * the authors. Those sources also carry three complete reference
 * transmissions (AA1AA, F5OEO, 7L1RLL) against which twcenc.c is
 * validated bit-exact - see tests/test_encoder.c.
 *
 * Coding chain (transmit side):
 *
 *   callsign (6 chars, normalized)  -> 28 bits   (pack)
 *   + CRC16 over those 28 bits      -> 44 bits
 *   + 3-bit CRC over the 44 bits    -> 47 bits
 *   "0000" + 47 bits                -> 51 bits   (4 leading sync bits)
 *   XOR pseudo-random sequence      -> 51 bits   (scramble)
 *   17 x [7,3] Walsh/simplex code   -> 119 bits
 *   7 x 17 block interleaver        -> 119 bits
 *   Manchester (0->"10", 1->"01"),
 *   preceded by one "1" start chip  -> 239 chips
 *
 * Each chip is sent as carrier ON (1) or OFF (0) - plain CW on/off
 * keying - for 0.256 * n seconds, n being the sub-mode number (OpN).
 */

#ifndef TWCDEFS_H
#define TWCDEFS_H

#include "twdefs.h"

/* -------------------------------------------------------------- */
/* Frame sizing                                                     */
/* -------------------------------------------------------------- */
#define TWC_NCALLBITS  28     /* packed callsign                      */
#define TWC_NCRC1      16     /* CRC over the 28 callsign bits         */
#define TWC_NCRC2       3     /* CRC over callsign + CRC1 (44 bits)    */
#define TWC_NSYNCBITS   4     /* leading "0000"                        */
#define TWC_NVEC       51     /* 4 + 28 + 16 + 3                       */
#define TWC_NGROUPS    17     /* 51 / 3 Walsh groups                   */
#define TWC_NCODED    119     /* 17 * 7                                */
#define TWC_NCHIPS    239     /* 1 start chip + 2 * 119 Manchester     */

static inline double twc_duration(double n) { return TWC_NCHIPS * TW_TCHIP_BASE * n; }

/* Scrambling sequence (51 bits). */
static const unsigned char TWC_PSEUDO[TWC_NVEC] = {
    1,1,1,0,0,0,0,1,0,1, 0,1,0,1,1,1,1,1,1,0, 0,1,1,0,1,1,0,1,0,0,
    0,0,0,0,0,1,1,0,0,1, 0,0,0,1,0,1,0,1,0,1, 1
};

/* [7,3] code ("Walsh matrix" in the original sources) - in fact the
 * simplex code: every non-zero codeword has weight 4, any two differ
 * in exactly 4 positions. Row index = the 3 data bits, MSB first. */
static const unsigned char TWC_WALSH[8][7] = {
    {0,0,0,0,0,0,0},{1,0,1,0,1,0,1},{0,1,1,0,0,1,1},{1,1,0,0,1,1,0},
    {0,0,0,1,1,1,1},{1,0,1,1,0,1,0},{0,1,1,1,1,0,0},{1,1,0,1,0,0,1}
};

#endif /* TWCDEFS_H */
