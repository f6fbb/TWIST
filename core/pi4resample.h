#ifndef RESAMPLE_H
#define RESAMPLE_H

/*
 * resample_to_rate() - Resamples a signal via Fourier interpolation
 * (FFT), suited to a band-limited signal (the case for radio audio
 * captures, already filtered by the receiver).
 *
 *   in      : input signal, fs_in Hz
 *   n_in    : number of input samples
 *   fs_in   : input sample rate (Hz)
 *   fs_out  : desired output sample rate (Hz)
 *   out     : [out] buffer allocated via malloc(), to be freed by the caller
 *   n_out   : [out] number of output samples
 *
 * Returns: 0 on success, -1 on error.
 *
 * If fs_in == fs_out, a plain copy is returned (no FFT).
 */
int resample_to_rate(const double *in, long n_in, int fs_in, int fs_out,
                      double **out, long *n_out);

#endif /* RESAMPLE_H */
