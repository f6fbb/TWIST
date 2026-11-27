#ifndef PI4IQMIX_H
#define PI4IQMIX_H

/*
 * pi4iqmix.h - (from pi4d) Frequency-shift a wideband I/Q capture down to a real,
 * single-channel signal centered on a chosen candidate, at PI4's
 * native rate (12000 Hz) - so it can be fed, completely unmodified,
 * into the existing validated pipeline (pi4_sync, pi4_symbol_metrics,
 * pi4_estimate_snr, pi4_decode_message).
 *
 * Principle: for a real bandpass signal represented as a complex
 * baseband I/Q pair, Re( (I+jQ) * exp(j*2*pi*df*t) ) performs a clean
 * frequency shift by +df with no image/mirror artifact (unlike mixing
 * a real signal, which always produces a mirror at the opposite
 * frequency) - this is the same principle an SDR itself uses to turn
 * I/Q into single-sideband audio. Choosing df so that the candidate's
 * carrier lands exactly on nfqso_target (matching pi4d's own -f
 * convention, default 800 Hz) means the existing pipeline can then
 * search a narrow range around it, at full sensitivity, with zero
 * changes to any already-validated module.
 */

/*
 * iq_mix_to_audio()
 *
 *   I, Q         : wideband I/Q input, fs_in Hz
 *   npts         : number of samples
 *   fs_in        : input sample rate (Hz)
 *   carrier_freq_hz : baseband frequency (Hz, relative to the I/Q
 *                  center, may be negative) of the candidate's CW
 *                  carrier (not tone0 - same OZ2M convention as pi4d's
 *                  -f option)
 *   nfqso_target : where the carrier should land in the output audio
 *                  (Hz) - pass 800.0 to match pi4d's own default
 *   fs_out       : desired output sample rate (Hz), typically 12000
 *                  (PI4_FS) so the result is ready for the existing
 *                  pipeline with no further resampling
 *   out          : [out] real signal, malloc()'d, caller frees
 *   n_out        : [out] number of output samples
 *
 * Returns: 0 on success, -1 on error (allocation failure or
 * resampling failure).
 */
int iq_mix_to_audio(const double *I, const double *Q, long npts, int fs_in,
                     double carrier_freq_hz, double nfqso_target, int fs_out,
                     double **out, long *n_out);

#endif /* PI4IQMIX_H */
