#ifndef PI4PIPE_H
#define PI4PIPE_H

#include "twpipe.h"

/*
 * pi4pipe.h - PI4 (PharusIgnis4, Bo Hansen OZ2M) frames in libtwist:
 * the decoder of pi4d (same author, core/pi4*.c, fano.c and tab.c from
 * WSJT-X), on the same blocks of SSB audio as the OOK frames.
 *
 * A PI4 frame is 146 4-FSK symbols (24.33 s), tones 234.375 Hz apart,
 * "carrier" (OZ2M convention: half a spacing above tone 0) usually near
 * 800 Hz - so its own search zone, p->pi4_f_center +/- p->pi4_tol.
 * Frames are searched whose start fits the start window of p (dt_min,
 * dt_max) and whose end fits its end window (end_min, end_max), as for
 * the other protocols: the short pass of the application finds each
 * frame once, just after it ends.
 *
 * A message is 8 characters: a callsign (with a /P or a prefix, e.g.
 * F5ZZO, F5ZZO/P), or "/" and a locator (/JN12AX: beacons alternate
 * the two). The Fano decoder says nothing of a wrong message, so a
 * decoded one is kept only if it is one of these and looks like a real
 * callsign (tw_call_plausible) or a valid locator. The spot's call is
 * the message (up to 8 characters), or "" with loc set for a locator.
 */

/* Decodes PI4 frames in x (n samples at fs, any rate: resampled to
 * 12 kHz). Returns the number of spots written (<= max), -1 on error. */
int tw_pi4_decode(const double *x, long n, int fs, const tw_params_t *p,
                  tw_spot_t *spots, int max);

/* Duration of a PI4 frame (s). */
double tw_pi4_duration(void);

/* The message checks above: 1 = callsign (call filled, normalized, up to
 * 8 characters), 2 = locator (loc filled), 0 = refused. */
int tw_pi4_message(const char msg[9], char call[12], char loc[7]);

#endif /* PI4PIPE_H */
