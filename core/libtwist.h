/*
 * libtwist.h - libtwist: the TWIST frames - TWIST S (callsign, 239
 * chips) and TWIST L (callsign + locator, 321 chips), OOK at 0.128 s
 * a chip, LDPC-coded (docs/SPECIFICATION.md). Receive and encode.
 *
 * Built as libtwist.dll / libtwist.so / libtwist.dylib.
 * Types: twltypes.h; audio and transmission: twcore.h (libtwcore).
 * Depends on no other library of TWIST Console.
 */

#ifndef LIBTWIST_H
#define LIBTWIST_H

#include "twltypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a signature below or a type of twltypes.h changes:
 * the application checks it at start-up. */
#define TWT_API_VERSION 1

TWL_API int         twt_api_version(void);
TWL_API const char *twt_version(void);          /* "1.0.0-rc.1" */

/* The default settings (all of twl_settings_t, the same for every
 * library: one settings structure can be given to all three). */
TWL_API void twt_settings_default(twl_settings_t *s);

typedef struct twt_decoder twt_decoder;

/* Creates / destroys a decoder; twt_create returns NULL if out of memory. */
TWL_API twt_decoder *twt_create(void);
TWL_API void twt_destroy(twt_decoder *d);

/* Known callsigns: comma- or space-separated list ("F5ZZO,F1HDI"),
 * replacing the previous one; NULL or "" clears it. Used by
 * known_only. Returns the number of callsigns kept, or -1 on
 * error (twt_last_error). */
TWL_API int twt_set_known(twt_decoder *d, const char *calls);

/* One decoding pass over a block of mono audio samples (float, any
 * scale: only the signal-to-noise ratio matters), for the protocols
 * of this library among s->protocols (TWL_PROTO_TWS, TWL_PROTO_TWL). Fills at most
 * max_spots entries of spots, sorted by start time, and returns their
 * number, or -1 on error (twt_last_error). */
TWL_API int twt_decode(twt_decoder *d, const float *samples, int n, int fs,
                      const twl_settings_t *s, twl_spot_t *spots, int max_spots);

/* Same, for frames that ended during the last `lookback` seconds of
 * the block - the short pass of the continuous decoder: every few
 * seconds, the last 45 s of audio with a lookback of a little more
 * than the pass period. The end window of s is replaced. */
TWL_API int twt_decode_recent(twt_decoder *d, const float *samples, int n, int fs,
                             const twl_settings_t *s, double lookback,
                             twl_spot_t *spots, int max_spots);

/* Message of the last error of this decoder ("" if none). */
TWL_API const char *twt_last_error(const twt_decoder *d);

/* Display label of a protocol of this library, as printed by the
 * command-line tools and published to the spot database ("TWIST S", "TWIST L");
 * "" if not one of its protocols. */
TWL_API const char *twt_proto_name(int proto);

/* Chips of a frame (1 = carrier on), for display. proto: TWL_PROTO_TWS
 * or TWL_PROTO_TWL; loc is needed for TWIST L only (4 or 6
 * characters). Writes at most max_chips values (TWL_MAX_CHIPS is
 * always enough) and returns the number of chips, or -1 if the
 * callsign or locator cannot be encoded or max_chips is too small. */
TWL_API int twt_encode(int proto, const char *call, const char *loc,
                       unsigned char *chips, int max_chips);

/* Number of chips of a frame (239, 321); 0 for another protocol. */
TWL_API int twt_frame_chips(int proto);

/* A transmit step for a frame (twcore.h, twl_tx_start): fills *st -
 * kind TWL_STEP_FRAME, its chips as on/off symbols of 0.128 s, text -
 * leaving tone_hz at 0 (the sequence's tone). 0, or -1 if it cannot be
 * encoded. */
TWL_API int twt_frame_step(int proto, const char *call, const char *loc, twl_step_t *st);

#ifdef __cplusplus
}
#endif

#endif /* LIBTWIST_H */
