/*
 * twcore.h - libtwcore, the part of TWIST Console that knows no
 * protocol: live audio (capture and playback), transmission (a
 * sequence of frames, carriers, pauses and CW rendered as a keyed or
 * shifted tone), the keying detector, and the arbitration between the
 * spots of several libraries. Types: twltypes.h.
 *
 * Built as libtwcore.dll / libtwcore.so / libtwcore.dylib.
 */

#ifndef TWCORE_H
#define TWCORE_H

#include "twltypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a structure of twltypes.h or a signature below
 * changes: the application checks it at start-up. */
#define TWL_CORE_API_VERSION 1

TWL_API int         twl_api_version(void);
TWL_API const char *twl_version(void);      /* "1.0.0-rc.1"                  */

/* Duration of one chip of the OOK frames, s (0.128). */
TWL_API double twl_chip_seconds(void);

/* A keyed signal present now (frame in progress): the on/off keying
 * detector of the wideband search (twscan.h), its chip-to-chip energy
 * jumps measured over the last window_s seconds (8 s: ~60 chips) of
 * the samples, in the search zone of s (f_center +/- tol). A steady
 * carrier is not reported (keying index), noise rarely above z = 5.
 * Writes up to max signals, strongest first, 15 Hz apart at least;
 * returns their number, or -1. */
TWL_API int twl_keyed(const float *samples, int n, int fs, const twl_settings_t *s,
                      double window_s, double z_min, twl_keyed_t *out, int max);

/* Spots of several libraries, decoded from the same audio: keeps one
 * of each pair that cannot both be real - the same callsign in
 * overlapping frames (one station, one frame at a time: the first one
 * is kept), and frames of DIFFERENT protocols at the same frequency
 * (within 3 chip widths) overlapping by more than half of the shorter
 * one (the larger contrast is kept: its pattern explains the received
 * chips). Spots of one library are already arbitrated among
 * themselves. Compacts spots in place and returns how many are left. */
TWL_API int twl_arbitrate(twl_spot_t *spots, int n);

/* ------------------------------------------------------------------ */
/* Transmission (twltypes.h: steps, settings, status)                  */
/* ------------------------------------------------------------------ */

typedef struct twl_tx twl_tx;

TWL_API void    twl_tx_settings_default(twl_tx_settings_t *s);
TWL_API twl_tx *twl_tx_create(void);
TWL_API void    twl_tx_destroy(twl_tx *t);

/* Prepares a sequence (replacing any previous one). Returns 0, or -1
 * with the reason in twl_tx_last_error (a frame without symbols, text
 * without Morse, duration or setting out of range...). */
TWL_API int twl_tx_start(twl_tx *t, const twl_tx_settings_t *s,
                         const twl_step_t *steps, int nsteps);

/* Writes the next n samples into out. Returns how many belong to the
 * sequence (the rest of out is zeroed): n while it lasts, fewer on the
 * last block, 0 once it is finished or stopped. */
TWL_API int twl_tx_render(twl_tx *t, float *out, int n);

/* Immediate stop: fades out over one ramp, then the sequence ends. */
TWL_API void twl_tx_stop(twl_tx *t);

/* Changes the level (0 < level <= 1) while the sequence plays, from
 * any thread: the amplitude follows with a 10 ms time constant, no
 * click (Tune: adjusting the drive while listening). 0, or -1. */
TWL_API int twl_tx_set_level(twl_tx *t, double level);

TWL_API void        twl_tx_status(const twl_tx *t, twl_tx_status_t *st);
TWL_API const char *twl_tx_last_error(const twl_tx *t);

/* Duration of one step, s (a frame: its symbols; CW: its text at its
 * speed), or -1 if it cannot be sent (no symbols, text that has no
 * Morse, duration or speed out of range). */
TWL_API double twl_tx_step_seconds(const twl_step_t *st);

/* ------------------------------------------------------------------ */
/* Live audio (twltypes.h: devices, statistics)                        */
/* ------------------------------------------------------------------ */

typedef struct twl_audio twl_audio;

/* 1 if this library was built with PortAudio (live audio), else 0. */
TWL_API int twl_audio_available(void);

/* Audio devices: count, and the description of device 0..count-1.
 * twl_audio_device_info returns 0, or -1 for a bad index. */
TWL_API int twl_audio_device_count(void);
TWL_API int twl_audio_device_info(int index, twl_audio_device_t *d);

TWL_API twl_audio *twl_audio_create(void);
TWL_API void       twl_audio_destroy(twl_audio *a);  /* stops everything */

/* Starts capturing from a device (index, TWL_AUDIO_DEFAULT or
 * TWL_AUDIO_NONE) at `rate` Hz (0: the device's default rate;
 * required with TWL_AUDIO_NONE), keeping the last keep_s seconds
 * (60 is enough for passes over 45 s). Returns 0 or -1. */
TWL_API int twl_audio_start_input(twl_audio *a, int device, int rate, double keep_s);

/* Copies the newest min(n, captured) samples (mono, at
 * twl_audio_stats_t.rate) into out, oldest first; returns their
 * number. */
TWL_API int twl_audio_last(twl_audio *a, float *out, int n);

/* Feeds `frames` interleaved frames of `channels` channels into the
 * capture path, as the sound card would (device TWL_AUDIO_NONE). */
TWL_API int twl_audio_push(twl_audio *a, const float *x, int frames, int channels);

TWL_API void twl_audio_input_stats(const twl_audio *a, twl_audio_stats_t *st);
TWL_API void twl_audio_stop_input(twl_audio *a);

/* Plays a prepared transmit sequence on a device (index or
 * TWL_AUDIO_DEFAULT) at the sequence's sample rate. Returns 0 or -1.
 * The sequence must stay alive until twl_audio_output_active returns 0
 * or twl_audio_stop_output is called. twl_tx_stop on the sequence
 * fades out the sound as usual. */
TWL_API int  twl_audio_start_output(twl_audio *a, int device, twl_tx *tx);

/* Same, timed: the first sample of the sequence reaches the sound
 * card's converter at UTC time `utc` (seconds since 1970-01-01, with
 * fractions), silence before - a beacon cycle starting on its second,
 * whatever the output latency (often 0.1 s to 2 s). 0: at once.
 * twl_audio_output_started then gives the UTC time the first sample
 * actually reached the converter (0 while waiting), as the stream
 * reports it; twl_audio_output_latency the stream's latency, s. */
TWL_API int    twl_audio_start_output_at(twl_audio *a, int device, twl_tx *tx, double utc);
TWL_API double twl_audio_output_started(const twl_audio *a);
TWL_API double twl_audio_output_latency(const twl_audio *a);
TWL_API int  twl_audio_output_active(const twl_audio *a);
TWL_API void twl_audio_stop_output(twl_audio *a);

TWL_API const char *twl_audio_last_error(const twl_audio *a);

#ifdef __cplusplus
}
#endif

#endif /* TWCORE_H */
