#ifndef PI4IQSCAN_H
#define PI4IQSCAN_H

/*
 * pi4iqscan.h - (from pi4d) Coarse wideband locator for PI4 signals in an I/Q
 * capture, ahead of the existing (narrowband, fully sensitive)
 * pi4_sync()/pi4_symbol_metrics()/pi4_decode_message() pipeline.
 *
 * This is deliberately NOT a sensitive decoder in itself - it is a
 * non-coherent, hard-decision correlator (argmax among the 4 tone
 * candidates per symbol, correlated against PI4_SYNCVEC), whose only
 * job is to narrow a potentially very wide search (tens of kHz) down
 * to a short list of promising (carrier frequency, time offset)
 * candidates. Each candidate is then handed to iq_mix_to_audio() and
 * decoded through the real, sensitivity-validated pipeline - so
 * iqscan's own precision only needs to be good enough to fall well
 * within that later stage's much narrower search range (a similar
 * division of labor to a receiver's coarse tuning step vs. its
 * demodulator).
 */

typedef struct {
    double carrier_freq_hz;  /* baseband frequency of the CW carrier (OZ2M
                                 convention, same as pi4d's -f), relative
                                 to the I/Q center - may be negative */
    double dt_hint;           /* rough time offset (s) of the candidate's
                                 start, from the beginning of the buffer */
    int quality_pct;          /* sync-bit correlation, 0-100 */
} iq_candidate_t;

/*
 * iq_scan()
 *
 *   I, Q            : wideband I/Q input, fs_in Hz
 *   npts            : number of samples
 *   fs_in           : input sample rate (Hz)
 *   freq_center_hz  : center of the search range (carrier convention),
 *                     relative to the I/Q center (0.0 = tuned frequency)
 *   freq_tol_hz     : half-width of the search range (Hz) - can be up
 *                     to the available bandwidth (tens of kHz)
 *   dt_max          : upper bound of the time-offset search (s)
 *   min_quality_pct : minimum sync-bit correlation (0-100) for a
 *                     candidate to be reported
 *   max_candidates  : size of the out[] array
 *   out             : [out] up to max_candidates candidates, strongest
 *                     first, non-maximum-suppressed (candidates closer
 *                     than 2 tone-spacings apart are merged)
 *
 * Returns: number of candidates found (0 if none), -1 on error.
 */
int iq_scan(const double *I, const double *Q, long npts, int fs_in,
            double freq_center_hz, double freq_tol_hz, double dt_max,
            int min_quality_pct, int max_candidates, iq_candidate_t *out);

#endif /* PI4IQSCAN_H */
