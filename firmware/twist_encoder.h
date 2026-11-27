/*
 * twist_encoder.h - TWIST v1 frame encoder (beacon side), standalone C.
 *
 * No dependency beyond <stdint.h>; no dynamic memory; about 2.7 kB of
 * constant tables (in flash on ESP32). Frame definition: PROTOCOL.md
 * of twistd. Check any port against twistd's "twistenc" tool: the
 * chips must be identical.
 *
 *   uint8_t chips[TWIST_MAX_CHIPS];
 *   int n = twist_encode("F6FBB", "JN18cs", chips);   // long frame: 321 chips
 *   int n = twist_encode("F6FBB", NULL, chips);       // short frame: 239 chips
 *   // n < 0: callsign or locator not encodable
 *   // then, for i = 0 .. n-1: carrier ON if chips[i], else OFF, for one
 *   // chip duration (0.128 s, Op05) - plain on/off keying.
 */
#ifndef TWIST_ENCODER_H
#define TWIST_ENCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWIST_SHORT_CHIPS 239     /* callsign only            */
#define TWIST_LONG_CHIPS  321     /* callsign + locator       */
#define TWIST_MAX_CHIPS   321
#define TWIST_CHIP_MS     128     /* Op05 chip duration (ms)  */

/* Encodes a TWIST frame. call: callsign (classic format: up to 6
 * characters, digit in 2nd or 3rd position, no "/P"); loc: 4 or 6
 * character Maidenhead locator for a long frame, NULL (or "") for a
 * short frame. chips[]: one byte per chip, 1 = carrier ON.
 * Returns the number of chips (239 or 321), or -1 if call/loc cannot
 * be encoded. */
int twist_encode(const char *call, const char *loc, uint8_t chips[TWIST_MAX_CHIPS]);

/* Same, packed 8 chips per byte (first chip = MSB of byte 0) - 41
 * bytes for a long frame. Read chip i with TWIST_CHIP(packed, i). */
int twist_encode_packed(const char *call, const char *loc, uint8_t packed[(TWIST_MAX_CHIPS + 7) / 8]);
#define TWIST_CHIP(p, i) (((p)[(i) >> 3] >> (7 - ((i) & 7))) & 1u)

#ifdef __cplusplus
}
#endif
#endif
