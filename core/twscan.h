#ifndef TWSCAN_H
#define TWSCAN_H

/*
 * twscan.h - Wideband pre-detection of classic signals.
 *
 * Decoding a whole 100 kHz beacon band with the full classic pipeline
 * would mean evaluating every start time at every one of ~50 000
 * frequency bins: far too slow. Instead, a cheap first stage finds the
 * frequencies where "something keyed like classic" is present, and the
 * full decoder (twpipe.c) is then run only on narrow tiles around
 * them - the same two-stage idea as pi4d's I/Q scan.
 *
 * Detector: a short-time spectrum with one-chip windows (the matched
 * filter of a chip), bins 1/(2T) apart, a new window every 1/4 chip.
 * In each bin, the energy is normalized by its median over time, and
 * the detector measures how much the energy jumps from one chip to the
 * next: mean |e(k+1) - e(k)| over one frame length, at each of the 4
 * chip phases, maximum over time. Manchester coding guarantees a
 * transition in every pair of chips, so a keyed classic carrier makes
 * this jump large; noise gives about 1 (difference of two normalized
 * exponential energies). A strong steady carrier (birdie, unkeyed
 * beacon) also has sizeable jumps, but only in proportion to the
 * square root of its level: it is rejected by a "keying index" (mean
 * jump / mean energy over the window: ~1 for noise, 1 to 1.5 for on/
 * off keying, well below 1 for a steady carrier). Other keyed signals (CW identification of beacons, other OOK
 * modes) do trigger it, and are then simply rejected by the decoder.
 * The per-bin values are turned into robust z-scores across the band.
 *
 * Measured (tests/wide_sweep.sh): the wideband path decodes half of
 * the frames at -21.3 dB, the narrowband decoder at -21.4 dB - the
 * pre-detection does not limit what can be found. On pure noise, the
 * largest z over a 100 kHz band is about 5 (default threshold 6).
 */

typedef struct {
    double freq;      /* Hz, same reference as the input (I/Q: relative
                         to the center; real audio: audio frequency)   */
    double z;         /* detector strength, robust sigmas              */
} tw_scanhit_t;

/*
 * Scans [f_lo, f_hi] (Hz). Q == NULL means real input (then only
 * positive frequencies are meaningful). Returns the number of hits
 * written to out (strongest first, at least min_sep Hz apart, z >=
 * z_min), or -1 on error. *n_bins (may be NULL) gets the number of
 * frequency bins examined.
 */
int tw_scan(const double *I, const double *Q, long n, int fs, double tchip,
               double f_lo, double f_hi, double z_min, double min_sep,
               int max_hits, tw_scanhit_t *out, long *n_bins, int verbose);

/*
 * Same, the jumps measured over win_jumps chips instead of one frame:
 * a keyed signal found after a few seconds of it (the "frame in
 * progress" of the application: twl_keyed).
 */
int tw_scan_w(const double *I, const double *Q, long n, int fs, double tchip,
              double f_lo, double f_hi, double z_min, double min_sep,
              int max_hits, tw_scanhit_t *out, long *n_bins, int win_jumps,
              double min_var_ratio, int verbose);
/* min_var_ratio > 0: also rejects the bins whose energy varies less
 * than that fraction of its squared mean over the window - steady
 * carriers, and the leakage of a strong one into its neighbours. */

#endif /* TWSCAN_H */
