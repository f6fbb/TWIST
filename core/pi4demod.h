#ifndef PI4DEMOD_H
#define PI4DEMOD_H

#include "pi4.h"

/* Extracts PI4_NSYM symbol metrics (one per coded bit, in transmission
 * order) from audio data sampled at PI4_FS.
 * Returns: 0 on success, -1 if the required window runs past the buffer. */
int pi4_symbol_metrics(const float *dat, int npts, double dtx,
                        double nfreq, int flip, float sym[PI4_NSYM]);

#endif /* PI4DEMOD_H */
