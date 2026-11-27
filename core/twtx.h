/*
 * twtx.h - Transmit audio: a sequence of steps (frames, carriers,
 * pauses, CW) rendered as a keyed audio tone (or a shifted one: the
 * 4-FSK of PI4), block by block, for a sound card (audio method of
 * docs/SPECIFICATION.md, section 3) or a WAV file. Knows no protocol:
 * a frame step carries its symbols, encoded by the protocol's library.
 * The public face is twcore.h (twl_tx_*); twistenc -w uses it
 * directly.
 *
 * Timing is counted in samples: the boundary of chip k of a step
 * starting at T is sample round((T + k * tchip) * fs), computed from
 * the exact time each time, so nothing drifts whatever fs (44100 Hz
 * included, where a chip is 5644.8 samples) - the chips are clocked
 * by the sound card that consumes the samples.
 *
 * Keying: the tone (continuous phase over the whole sequence) is
 * multiplied by an envelope that moves between 0 and 1 with a
 * raised-cosine ramp of ramp_s (5 ms) STARTING at each boundary where
 * the level changes - no click, no splatter. Every edge is late by
 * the same ramp_s / 2, so chip timing and energy are unchanged.
 */

#ifndef TWTX_H
#define TWTX_H

#include "twltypes.h"

typedef struct twtx twtx_t;

twtx_t *twtx_create(void);
void    twtx_destroy(twtx_t *t);

/* Prepares a sequence (frames encoded, boundaries computed). Returns
 * 0, or -1 with a message in twtx_error(). */
int twtx_start(twtx_t *t, const twl_tx_settings_t *s, const twl_step_t *steps, int nsteps);

/* Renders the next n samples into out. Returns the number of samples
 * that belong to the sequence (the rest of out is set to 0): n while
 * it lasts, less on the last block, 0 once finished or stopped. */
int twtx_render(twtx_t *t, float *out, int n);

/* Immediate stop: the tone fades out over one ramp, then the sequence
 * ends. Safe to call from another thread than twtx_render. */
void twtx_stop(twtx_t *t);

/* Changes the level while the sequence plays (Tune: setting the audio
 * level and the ALC by ear and on the meter): the amplitude moves to
 * it with a 10 ms time constant, no click. Safe from another thread.
 * 0, or -1 if the level is out of (0, 1]. */
int twtx_set_level(twtx_t *t, double level);

void twtx_status(const twtx_t *t, twl_tx_status_t *st);
const char *twtx_error(const twtx_t *t);

/* Duration of a step, s, or -1 if it cannot be sent (twl_tx_step_seconds). */
double twtx_step_seconds(const twl_step_t *st);

/* Sample rate of the prepared sequence (0 if none). */
int twtx_rate(const twtx_t *t);

#endif /* TWTX_H */
