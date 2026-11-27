/*
 * twcl.h - The classic frame (callsign, 239 chips, OOK at 0.128 s a
 * chip in Op05): receive, with the deep search for known callsigns,
 * and encode. Its coding chain is the one published as open source
 * (GPL) by F4GCB and 7L1RLL (CREDITS.md). Spots are labelled with the
 * name its users know it by (twcl_proto_name).
 *
 * Built as libopera.dll / libopera.so / libopera.dylib.
 * Types: twltypes.h; audio and transmission: twcore.h (libtwcore).
 * Depends on no other library of TWIST Console.
 */

#ifndef TWCL_H
#define TWCL_H

#include "twltypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a signature below or a type of twltypes.h changes:
 * the application checks it at start-up. */
#define TWCL_API_VERSION 1

TWL_API int         twcl_api_version(void);
TWL_API const char *twcl_version(void);          /* "1.0.0-rc.1" */

/* The default settings (all of twl_settings_t, the same for every
 * library: one settings structure can be given to all three). */
TWL_API void twcl_settings_default(twl_settings_t *s);

typedef struct twcl_decoder twcl_decoder;

/* Creates / destroys a decoder; twcl_create returns NULL if out of memory. */
TWL_API twcl_decoder *twcl_create(void);
TWL_API void twcl_destroy(twcl_decoder *d);

/* Known callsigns: comma- or space-separated list ("F5ZZO,F1HDI"),
 * replacing the previous one; NULL or "" clears it. Used by
 * known_only, and by the deep search (s->deep): a matched filter for each callsign, a few dB more sensitive than blind decoding. Returns the number of callsigns kept, or -1 on
 * error (twcl_last_error). */
TWL_API int twcl_set_known(twcl_decoder *d, const char *calls);

/* One decoding pass over a block of mono audio samples (float, any
 * scale: only the signal-to-noise ratio matters), for the protocols
 * of this library among s->protocols (TWL_PROTO_CLASSIC). Fills at most
 * max_spots entries of spots, sorted by start time, and returns their
 * number, or -1 on error (twcl_last_error). */
TWL_API int twcl_decode(twcl_decoder *d, const float *samples, int n, int fs,
                      const twl_settings_t *s, twl_spot_t *spots, int max_spots);

/* Same, for frames that ended during the last `lookback` seconds of
 * the block - the short pass of the continuous decoder: every few
 * seconds, the last 45 s of audio with a lookback of a little more
 * than the pass period. The end window of s is replaced. */
TWL_API int twcl_decode_recent(twcl_decoder *d, const float *samples, int n, int fs,
                             const twl_settings_t *s, double lookback,
                             twl_spot_t *spots, int max_spots);

/* Message of the last error of this decoder ("" if none). */
TWL_API const char *twcl_last_error(const twcl_decoder *d);

/* Display label of a protocol of this library, as printed by the
 * command-line tools and published to the spot database (the classic frame under the name its users know it by);
 * "" if not one of its protocols. */
TWL_API const char *twcl_proto_name(int proto);

/* Chips of a frame (1 = carrier on), for display: 239 values written
 * to chips (max_chips at least 239). Returns 239, or -1 if the
 * callsign cannot be encoded. */
TWL_API int twcl_encode(const char *call, unsigned char *chips, int max_chips);

/* A transmit step for a frame (twcore.h, twl_tx_start): fills *st -
 * kind TWL_STEP_FRAME, its 239 chips as on/off symbols of 0.128 s,
 * text - leaving tone_hz at 0 (the sequence's tone). 0, or -1. */
TWL_API int twcl_frame_step(const char *call, twl_step_t *st);

#ifdef __cplusplus
}
#endif

#endif /* TWCL_H */
