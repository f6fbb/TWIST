#ifndef TWSYNC_H
#define TWSYNC_H

#include "twdefs.h"
#include "twbb.h"

/*
 * twsync.h - Time/frequency search, refinement and chip energy
 * extraction for classic, working on the complex baseband signal made
 * by twbb.c.
 *
 * classic is plain on/off keying, and Manchester-coded: in every pair
 * of chips exactly one is "on". The natural receiver is therefore an
 * energy detector per chip (non-coherent: the carrier phase is not
 * needed, so slow phase drift, frequency jitter or a slightly
 * unstable receiver do not matter), comparing the two chips of each
 * pair. For a chip of duration T, the matched filter is a T-long
 * rectangular window - one DFT of exactly one chip's samples.
 *
 * 1. Spectrogram (tw_spectrogram): DFTs of one-chip windows
 *    (L samples, zero-padded x4 -> bins of 1/(4T) Hz) every L/16
 *    samples (1/16 chip). Each frequency bin is normalized by a
 *    robust noise estimate, so all later scores are in noise units.
 *
 * 2. Candidate search (tw_find_candidates): for every start
 *    position and every frequency bin, the soft-decision score of
 *    the best possible frame (the code's own score, tw_code_ops_t) is
 *    computed directly from the spectrogram - i.e. the search uses
 *    the full decoder likelihood, not a separate sync pattern
 *    (classic has no dedicated sync sequence: its 4 known "sync" bits
 *    sit inside the code). Local maxima clearly above the noise
 *    floor of that score map become candidates.
 *
 * 3. Refinement (tw_refine): around a candidate, chip energies are
 *    recomputed exactly (sample-accurate start, sub-bin frequency,
 *    optional linear frequency drift) and the best fit is kept.
 *
 * Frames are allowed to be partially outside the recording (start
 * cut off, or end cut off): missing chips are treated as erasures
 * (zero soft value), which the [7,3] code and the CRC list decoding
 * can often tolerate - see max_missing.
 */

/* Number of one-bit (2-chip) alignment shifts explored on each side
 * of a candidate - see tw_fit_shift(). */
#define TW_SHIFT_SPAN 3

typedef struct {
    double tchip;        /* chip duration (s)                           */
    double fs;           /* baseband sample rate (Hz)                   */
    int    L;            /* samples per chip                            */
    int    M;            /* spectrogram positions per chip              */
    int    step;         /* L / M                                       */
    int    nfft;         /* zero-padded DFT size                        */
    double df;           /* bin spacing (Hz)                            */
    int    nb;           /* number of frequency bins kept               */
    int    b0;           /* bin b <-> baseband frequency (b - b0) * df  */
    long   np;           /* number of time positions                    */
    float *E;            /* np x nb normalized energies (row = time)    */
    float *mu;           /* nb raw noise energy per chip window         */
    int    hs;           /* spread: energies summed over bins b-hs..b+hs
                            (0 = single bin, the normal case)            */
    double zscale;       /* normalized-energy rescaling so that, with a
                            spread, noise stays in the same units as
                            without (1.0 when hs = 0)                   */
    double snr_corr;     /* SNR estimate correction for the summed bins */
    /* Frame geometry used by the search and the fits (set to classic by
     * tw_spectrogram(); tw_spec_frame() selects another one -
     * on a COPY of the struct, which shares E and mu: free only the
     * original). */
    int    ncoded;       /* coded bits per frame (119 classic)            */
    int    nch;          /* chips per frame = 2 * ncoded + 1            */
    const struct tw_code_ops *ops;  /* the code's own scores (the classic
                            frame: Walsh groups, twcdec.c); NULL: any
                            code (sum of |Manchester decisions|)         */
} tw_spec_t;

typedef struct {
    long   p;            /* spectrogram position of chip 0 (may be < 0) */
    int    b;            /* frequency bin                               */
    double t0;           /* start time (s, from file start)             */
    double f0;           /* baseband frequency (Hz)                     */
    double score;        /* frame metric (noise units)                  */
    double z;            /* score in robust sigmas above the map median */
} tw_cand_t;

typedef struct {
    long   t_samp;       /* baseband sample index of chip 0's start     */
    double f;            /* baseband frequency at mid-frame (Hz)        */
    double drift;        /* Hz per second                               */
    double score;        /* frame metric                                */
    double mu;           /* noise reference used (raw energy units)     */
    int    nvalid;       /* chips inside the recording                  */
    double zscale;       /* copied from the spectrogram                 */
    double snr_corr;     /* copied from the spectrogram                 */
    int    nch;          /* chips of the frame geometry used            */
    float  e[TW_MAXCHIPS];   /* raw chip energies (<0 = missing)     */
    float  soft[TW_MAXCODED];/* soft bits (b - a) / noise            */
} tw_fit_t;

/* The scores of a code that knows better than "sum of |Manchester
 * decisions|" (the classic frame: the best codeword of each Walsh
 * group, twcdec.c):
 *   score:  adds, for each of nbs frequency bins, the score of one
 *           start position to Srow; D holds sp->ncoded rows of nbs
 *           Manchester decisions (second chip - first chip); acc and
 *           best are nbs floats of scratch;
 *   metric: the score of the soft bits of one fitted frame. */
typedef struct tw_code_ops {
    void   (*score)(const float *D, int nbs, float *Srow, float *acc, float *best);
    double (*metric)(const float *soft);
} tw_code_ops_t;

/* Selects the frame geometry of a spectrogram (or of a copy, sharing
 * E and mu): ncoded coded bits (Manchester, one start chip) and the
 * code's scores (NULL: any code). tw_spectrogram() starts with the
 * classic frame's geometry (119 coded bits) and ops = NULL. */
void tw_spec_frame(tw_spec_t *sp, int ncoded, const tw_code_ops_t *ops);

/* Builds the normalized spectrogram over |f| <= tol (+margin).
 *
 * spread_hz > 0: "spread" mode, for signals whose carrier is smeared
 * in frequency (rain scatter, aircraft scatter, aurora at microwave
 * frequencies): the energy of each chip is summed over a band of
 * spread_hz instead of being taken in one ~1/T-wide filter. Costs a
 * little sensitivity on a clean carrier (more noise in the detector),
 * gains a lot when the signal really is spread over that width.
 *
 * Returns 0 on success, -1 on error (allocation, recording shorter
 * than one chip...). */
int tw_spectrogram(const tw_bb_t *bb, double tchip, double tol, double spread_hz,
                      tw_spec_t *sp);
void tw_spectrogram_free(tw_spec_t *sp);

/* Scans start times in [dt_min, dt_max] (s, may extend beyond the
 * recording by up to max_missing of a frame on either side) and
 * frequencies in [-tol, +tol], returns up to max_cand candidates with
 * z >= z_min, strongest first, non-maximum suppressed. *map_median
 * and *map_sigma (may be NULL) receive the score map's robust
 * statistics. Returns the number of candidates, -1 on error. */
int tw_find_candidates(const tw_spec_t *sp, double dt_min, double dt_max,
                          double tol, double max_missing, double z_min,
                          int max_cand, tw_cand_t *out,
                          double *map_median, double *map_sigma);

/* Refines a candidate. drift_max: largest |drift| tried, Hz/s
 * (0 = no drift search). Returns 0, or -1 if too few chips are inside
 * the recording. */
int tw_refine(const tw_bb_t *bb, const tw_spec_t *sp,
                 const tw_cand_t *c, double drift_max, double max_missing,
                 tw_fit_t *fit);

/* Same fit, frame start moved by shift_chips chips (frequency and
 * drift kept). The classic frame's structure makes a shift by one Manchester bit
 * (2 chips) almost as likely as the true alignment - every group
 * then sees a valid codeword, only of its neighbouring group - so the
 * decoder tries a few such shifts and lets the CRC decide.
 * Returns 0, or -1 if too many chips fall outside the recording. */
int tw_fit_shift(const tw_bb_t *bb, const tw_spec_t *sp, const tw_fit_t *in,
                    int shift_chips, double max_missing, tw_fit_t *out);

/* Raw energy of every chip for a frame starting at baseband sample
 * t0, with carrier frequency f_mid (Hz, at mid-frame) and linear
 * drift (Hz/s). Chips outside the recording get -1. Returns the
 * number of chips inside. */
int tw_chip_energies(const tw_bb_t *bb, int L, int nch, long t0, double f_mid,
                        double drift, float *e);

/* Same, with the spectrogram's spread applied (energies summed over
 * the same band of bins as the spectrogram). Identical to
 * tw_chip_energies() when sp->hs == 0. */
int tw_chip_energies_sp(const tw_bb_t *bb, const tw_spec_t *sp, long t0,
                           double f_mid, double drift, float *e);

/* Noise reference (raw energy units) of the spectrogram bin nearest
 * to baseband frequency f. */
double tw_noise_at(const tw_spec_t *sp, double f);

/* SNR (dB, 2500 Hz reference bandwidth, power of the carrier while
 * ON) from a fit and the decoded frame's chips. */
double tw_estimate_snr(const tw_fit_t *fit, const unsigned char *chips,
                          double tchip);

/* Same, the noise measured at the same moments as the carrier (the ON
 * chips, the fitted carrier subtracted, far from it in frequency): the
 * OFF chips hold some of a strong carrier, and a receiver's AGC raises
 * the noise as soon as it stops - both made the SNR of strong signals
 * 10 dB and more too low. Falls back on tw_estimate_snr() in spread
 * mode or when the frame has too few ON-ON chip pairs. */
double tw_estimate_snr_bb(const tw_bb_t *bb, const tw_spec_t *sp, const tw_fit_t *fit,
                             const unsigned char *chips, double tchip);

#endif /* TWSYNC_H */
