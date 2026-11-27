/*
 * libpi4.h - libpi4: PI4 (OZ2M, PharusIgnis4: 4-FSK, 146 symbols of
 * 1/6 s, synchronous on the minute), receive in its own zone
 * (pi4_f_center +/- pi4_tol of the settings) and encode. The decoder
 * of pi4d, after JT4 of WSJT-X (K1JT) - CREDITS.md.
 *
 * Built as libpi4.dll / libpi4.so / libpi4.dylib.
 * Types: twltypes.h; audio and transmission: twcore.h (libtwcore).
 * Depends on no other library of TWIST Console.
 */

#ifndef LIBPI4_H
#define LIBPI4_H

#include "twltypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a signature below or a type of twltypes.h changes:
 * the application checks it at start-up. */
#define PI4L_API_VERSION 1

TWL_API int         pi4l_api_version(void);
TWL_API const char *pi4l_version(void);          /* "1.0.0-rc.1" */

/* The default settings (all of twl_settings_t, the same for every
 * library: one settings structure can be given to all three). */
TWL_API void pi4l_settings_default(twl_settings_t *s);

typedef struct pi4l_decoder pi4l_decoder;

/* Creates / destroys a decoder; pi4l_create returns NULL if out of memory. */
TWL_API pi4l_decoder *pi4l_create(void);
TWL_API void pi4l_destroy(pi4l_decoder *d);

/* Known callsigns: comma- or space-separated list ("F5ZZO,F1HDI"),
 * replacing the previous one; NULL or "" clears it. Used by
 * known_only. Returns the number of callsigns kept, or -1 on
 * error (pi4l_last_error). */
TWL_API int pi4l_set_known(pi4l_decoder *d, const char *calls);

/* One decoding pass over a block of mono audio samples (float, any
 * scale: only the signal-to-noise ratio matters), for the protocols
 * of this library among s->protocols (TWL_PROTO_PI4). Fills at most
 * max_spots entries of spots, sorted by start time, and returns their
 * number, or -1 on error (pi4l_last_error). */
TWL_API int pi4l_decode(pi4l_decoder *d, const float *samples, int n, int fs,
                      const twl_settings_t *s, twl_spot_t *spots, int max_spots);

/* Same, for frames that ended during the last `lookback` seconds of
 * the block - the short pass of the continuous decoder: every few
 * seconds, the last 45 s of audio with a lookback of a little more
 * than the pass period. The end window of s is replaced. */
TWL_API int pi4l_decode_recent(pi4l_decoder *d, const float *samples, int n, int fs,
                             const twl_settings_t *s, double lookback,
                             twl_spot_t *spots, int max_spots);

/* Message of the last error of this decoder ("" if none). */
TWL_API const char *pi4l_last_error(const pi4l_decoder *d);

/* Display label of a protocol of this library, as printed by the
 * command-line tools and published to the spot database ("PI4");
 * "" if not one of its protocols. */
TWL_API const char *pi4l_proto_name(int proto);

/* A transmit step for a PI4 frame (twcore.h, twl_tx_start): the
 * message is call, or "/" + loc when call is empty (a beacon
 * alternates both); 8 characters at most of A-Z 0-9 space and "/".
 * Fills *st - kind TWL_STEP_FRAME, 146 symbols of 1/6 s (24.33 s),
 * 4-FSK, symbol k at tone - 117.1875 + k * 234.375 Hz - leaving
 * tone_hz at 0: set it to the PI4 nominal frequency, where the CW
 * identification and the carrier of a PI4 beacon are sent too.
 * 0, or -1 if the message cannot be encoded. */
TWL_API int pi4l_frame_step(const char *call, const char *loc, twl_step_t *st);

/* Duration of a PI4 frame, s (146 / 6). */
TWL_API double pi4l_frame_seconds(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBPI4_H */
