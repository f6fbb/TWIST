#ifndef SYNTH_H
#define SYNTH_H
#include <stdint.h>
/*
 * synth.h - Test-signal synthesis for classic (tests only).
 *
 * SNR convention (same as pi4d's tests and WSJT): power of the
 * carrier while ON, over the noise power in a 2500 Hz bandwidth.
 * For real audio at fs with white Gaussian noise of standard
 * deviation sigma, noise power in 2500 Hz = sigma^2 * 2500 / (fs/2),
 * and a carrier A*cos() has power A^2/2.
 */
void synth_seed(uint64_t s);
double synth_gauss(void);
/* amplitude giving snr_db for noise standard deviation sigma at fs */
double synth_amp_for_snr(double snr_db, double sigma, int fs);
/* adds a classic transmission (rise/fall time trf seconds, 0 = hard
 * keying), linear frequency drift in Hz/s around the frame middle */
void synth_add_classic(double *x, long n, int fs, const unsigned char *chips, double tchip,
                     double t0, double freq, double amp, double phase, double drift, double trf);
void synth_add_noise(double *x, long n, double sigma);
/* Same as synth_add_classic for a frame of nch chips (TWIST: 239 or 321). */
void synth_add_frame(double *x, long n, int fs, const unsigned char *chips, int nch, double tchip,
                     double t0, double freq, double amp, double phase, double drift, double trf);


/* Same as synth_add_classic, but the carrier is spread in frequency like
 * rain scatter: a sum of 64 tones with Gaussian-distributed offsets
 * (RMS spread_hz) and random phases - Rayleigh fading with a Gaussian
 * Doppler spectrum. Mean power = amp^2 / 2, as for a plain carrier. */
void synth_add_classic_spread(double *x, long n, int fs, const unsigned char *chips, double tchip,
                            double t0, double freq, double amp, double spread_hz);
/* PI4: message msg (8 characters, padded), tone 0 at carrier - 117.19 Hz,
 * starting at t0 s, amplitude amp (SNR: synth_amp_for_snr, the 4-FSK
 * signal being always on) - needs core/pi4enc.c and fano.c. */
void synth_add_pi4(double *x, long n, int fs, const char *msg, double t0, double carrier,
                   double amp, double phase);
#endif
