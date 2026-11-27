#ifndef TWPIPE_H
#define TWPIPE_H

#include "twdefs.h"

/* The protocols compiled into twpipe.c: each library (and each
 * command-line tool) builds it with its own (-DTW_WITH_TWIST=1...);
 * without any, all three (the test programs). */
#if !defined(TW_WITH_CLASSIC) && !defined(TW_WITH_TWIST) && !defined(TW_WITH_PI4)
#  define TW_WITH_CLASSIC 1
#  define TW_WITH_TWIST   1
#  define TW_WITH_PI4     1
#endif
#ifndef TW_WITH_CLASSIC
#  define TW_WITH_CLASSIC 0
#endif
#ifndef TW_WITH_TWIST
#  define TW_WITH_TWIST 0
#endif
#ifndef TW_WITH_PI4
#  define TW_WITH_PI4 0
#endif
#define TW_WITH_OOK (TW_WITH_CLASSIC || TW_WITH_TWIST)

struct twc_known;        /* twcdeep.h: a known callsign of the classic frame */

/*
 * twpipe.h - The whole receive pipeline in one call, shared by the
 * libraries, the command-line tools and the test programs:
 *
 *   input -> baseband (twbb) -> spectrogram + candidate search
 *   (twsync) -> refinement -> soft decoding with CRC (twcdec)
 *   -> re-encoding -> SNR
 */

typedef struct {
    double mode_n;       /* OpN sub-mode number: 0.5, 1, 2, 4, 8, 16, 32 */
    double f_center;     /* center of the search, Hz (carrier frequency;
                            for I/Q input, relative to the I/Q center) */
    double tol;          /* half-width of the frequency search, Hz       */
    double dt_min;       /* earliest frame start searched, s             */
    double dt_max;       /* latest frame start searched, s (<0: no limit) */
    double end_min;      /* earliest frame END searched, s               */
    double end_max;      /* latest frame end searched, s (<0: no limit).
                            The end window is per protocol (start = end -
                            frame duration), combined with dt_min/dt_max:
                            "frames that ended in the last 3 s" in one
                            call, whatever their length.                 */
    double drift_max;    /* largest drift tried, Hz/s (<0: automatic)    */
    double spread_hz;    /* >0: chip energy summed over this band (Hz),
                            for frequency-spread signals (rain scatter) */
    double max_missing;  /* fraction of a frame allowed outside the file */
    double z_min;        /* candidate threshold, sigmas of the score map */
    int    max_cand;     /* candidates tried at most                     */
    int    list_max;     /* frames tried per candidate by the decoder    */
    int    plausible_only; /* drop callsigns failing tw_call_plausible() */
    double twin_margin;  /* minimum lead over the frame's CRC twins
                            (twc_twin_margin), noise units            */
    double osd_margin;   /* TWIST: a frame found by OSD (belief
                            propagation failed) must lead the next
                            nearest codeword by this much (LLR units):
                            OSD always returns a codeword, and on noise
                            the CRC-16 alone let one in 65536 through.
                            0 = check disabled.                        */
    double off_max;      /* pattern consistency: a blind decode is
                            refused if its OFF chips hold more than
                            off_max + 0.05 x contrast noise units of
                            energy above the noise (another signal at
                            that frequency: CW beacon, birdie, PI4
                            tone...). 0 = check disabled.             */
    const struct twc_known *known;  /* known callsigns of the classic
                            frame, for its deep search (may be NULL)    */
    int    nknown;
    const char (*calls)[12];  /* known callsigns, any protocol, trimmed
                            ("F5ZZO"): for known_only (may be NULL)     */
    int    ncalls;
    int    known_only;   /* report only callsigns of the known lists     */
    int    deep;         /* deep search for the known callsigns          */
    double deep_fa;      /* accepted deep-search false alarms per run    */
    int    snr_avg;      /* 1 (default): SNR as AVERAGE power over the
                            frame, i.e. carrier-on power - 3 dB (classic
                            keys the carrier half of the time) - the
                            official software of the classic mode's convention.
                            0: carrier power while keyed ON.            */
    int    verbose;      /* diagnostics on stderr                        */
    int    protocols;    /* TW_PROTO_* bits searched (default: the OOK
                            frames, TW_PROTO_ALL; TW_PROTO_PI4 added on
                            request)                                   */
    double pi4_f_center; /* PI4: its "carrier" (OZ2M: half a tone spacing
                            above tone 0), Hz (default 800)           */
    double pi4_tol;      /* PI4: half-width of its search, Hz (150)    */
} tw_params_t;

/* Protocols (tw_params_t.protocols bits, tw_spot_t.proto). */
#define TW_PROTO_CLASSIC  1   /* classic frame: callsign, 239 chips      */
#define TW_PROTO_S    2   /* TWIST short: callsign, 239 chips       */
#define TW_PROTO_L    4   /* TWIST long: callsign + locator, 321 chips */
#define TW_PROTO_ALL    7   /* the OOK frames (the default)              */
#define TW_PROTO_PI4    8   /* PI4 (4-FSK, OZ2M): its own zone, pi4_f_center
                               +/- pi4_tol (core/pi4pipe.h)              */

typedef struct {
    char   call[12];     /* trimmed callsign (PI4: up to 8 characters,
                            "F5ZZO/P"; "" for a PI4 locator message)    */
    double dt;           /* start of chip 0, s from the file start       */
    double df;           /* carrier frequency, Hz                        */
    double snr;          /* dB, 2500 Hz bandwidth (see snr_avg)          */
    double drift;        /* Hz/s                                         */
    double z;            /* candidate strength (sigmas)                  */
    double score;        /* refined frame metric                         */
    int    rank;         /* list-decoder rank that passed the CRC        */
    double loss;         /* metric given up by the list decoder          */
    int    shift;        /* alignment shift (chips) that decoded         */
    int    nvalid;       /* chips inside the recording (239 = complete)  */
    int    deep;         /* 1 = found by deep search (known callsign
                            pattern detected, not decoded blindly)      */
    double contrast;     /* (mean ON-chip - mean OFF-chip energy) / noise */
    int    proto;        /* TW_PROTO_CLASSIC, _TWS or _TWL              */
    char   loc[7];       /* Maidenhead locator (TWIST long), else ""     */
    double off_excess;   /* mean OFF-chip energy / noise - 1: about 0
                            for a real classic frame (OFF chips hold only
                            noise), large when the energy at that
                            frequency belongs to another signal        */
} tw_spot_t;

/* Fills p with the defaults (Op05, 1500 Hz +/- 200 Hz, whole file...). */
void tw_params_default(tw_params_t *p);

/* Decodes real (SSB audio) samples. Returns the number of spots found
 * (0..max_spots, sorted by DT), or -1 on error. */
int tw_decode_real(const double *x, long n, int fs, const tw_params_t *p,
                      tw_spot_t *spots, int max_spots);

/* Same for complex I/Q samples. */
int tw_decode_iq(const double *I, const double *Q, long n, int fs,
                    const tw_params_t *p, tw_spot_t *spots, int max_spots);

/* Wideband: pre-detection over [f_center - tol, f_center + tol]
 * (twscan.h), then the full decoder on a narrow tile (+/- 20 Hz)
 * around each of at most scan_max detections at scan_z sigmas or
 * more. Q == NULL for real input. For a band much wider than a few
 * hundred Hz (an I/Q capture of a whole beacon band). */
int tw_decode_wide(const double *I, const double *Q, long n, int fs,
                      const tw_params_t *p, double scan_z, int scan_max,
                      tw_spot_t *spots, int max_spots);

#endif /* TWPIPE_H */
