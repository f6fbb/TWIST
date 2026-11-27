#ifndef PI4SNR_H
#define PI4SNR_H

#include "pi4.h"

/*
 * pi4_estimate_snr() - Estimates the SNR (dB, 2500 Hz reference
 * bandwidth, WSJT convention) from the demodulated signal and the
 * DT/DF found by pi4_sync().
 *
 * Principle: for each symbol, coherently correlate (over the 2000
 * samples of the symbol) the signal against the 4 candidate tone
 * frequencies. The strongest tone estimates signal+noise; the average
 * of the other 3 estimates noise alone (only one tone is physically
 * present at a time in PI4 4-FSK). The difference gives a signal-
 * power estimate, converted to the 2500 Hz reference SNR convention.
 *
 * Calibrated and verified via Monte-Carlo against known injected SNR
 * values (see tests/snr_estimate_calib.c): residual bias < 0.2 dB
 * over the -3 to -19 dB range, rising to ~+0.7 dB near the decoding
 * threshold (-21 dB). A factor-of-2 error in the initial derivation
 * of the noise power (E[|noise_correlator|^2] = N*sigma^2, not
 * N*sigma^2/2) produced a constant -3.0 dB bias before correction -
 * detected and fixed thanks to this same calibration.
 *
 *   dat   : audio data, sampled at PI4_FS
 *   npts  : number of samples in dat
 *   dtx   : time offset found by pi4_sync() (s)
 *   dfx   : tone-0 frequency found by pi4_sync() (Hz)
 *
 * Returns: estimated SNR in dB (2500 Hz reference bandwidth). No
 * separate error code - a very negative value (< -30 dB) signals an
 * unreliable estimate rather than a genuinely extremely weak signal.
 */
double pi4_estimate_snr(const float *dat, int npts, double dtx, double dfx);

#endif /* PI4SNR_H */
