/*
 * pi4d.c - PI4 decoder command-line tool
 *
 * Usage:
 *   pi4d [options] file.wav
 *
 * Options:
 *   -f FREQ   Nominal CW carrier frequency, in Hz (default 800,
 *             OZ2M convention). NOT the tone0 frequency, which is
 *             found automatically at FREQ - 0.5*tone_spacing.
 *   -t TOL    Half-width of the search range, in Hz (default 150)
 *   -d DTMAX  Upper bound of the time-offset search, in seconds, for
 *             the FIRST spot only (default 2.0) - see sync_pi4.h.
 *             Once a spot is found and decoded, pi4d automatically
 *             keeps searching the remainder of the file for further
 *             spots (e.g. a beacon repeating on its next cycle), each
 *             printed as its own result line - see "Multiple spots"
 *             below.
 *   -T HHMMSS UTC timestamp to display (6 digits, e.g. 223000).
 *             The WAV file itself does not carry this information;
 *             defaults to the system UTC time at decode time - pass
 *             it explicitly if the actual reception time is known
 *             (e.g. taken from the filename or a recording pipeline).
 *             Identifies the file/run - all spots found within one
 *             file share the same HHMMSS; DT (see below) is what
 *             distinguishes where each one actually falls.
 *   -D        Diagnostic mode: prints all diagnostics (file read,
 *             resampling, DT/DF/ccfpeak/SNR, Fano metric) on stderr.
 *             Without this option, minimal KA9Q-style output: just
 *             the result line(s) on stdout on success, a single error
 *             line on stderr otherwise.
 *   -v        Prints "pi4d <version> - F6FBB 2026" and exits. No
 *             .wav file needed.
 *   -V        Prints the version number and exits.
 *   -h        Prints this help and exits.
 *
 * Output (stdout), jt9-inspired format, one line per spot found:
 *   HHMMSS SNR   DT   DF : MESSAGE
 * where MESSAGE has its leading spaces replaced with underscores and
 * its trailing spaces stripped (truncated string). DT is always the
 * offset, in seconds, between the very start of the WAV file and that
 * spot's own start - not relative to any previous spot or search
 * window.
 *
 * Multiple spots: after decoding a spot, pi4d searches the rest of
 * the file (from the end of that spot onward) for another one, and
 * repeats until nothing more is found or the file runs out - useful
 * for a recording that spans more than one beacon cycle. Each spot
 * found gets its own output line, in order.
 *
 * A failure to find or decode a spot is a normal, expected outcome
 * when no PI4 message is present in the search window - not a fault.
 *
 * Build (see also the provided Makefile):
 *   gcc -O2 -Wall -o pi4d pi4d.c wavread.c sync_pi4.c pi4demod.c \
 *       pi4enc.c pi4dec.c pi4mettab.c pi4snr.c fano.c tab.c -lfftw3 -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "twdefs.h"
#include "pi4.h"
#include "pi4sync.h"
#include "pi4demod.h"
#include "pi4dec.h"
#include "pi4snr.h"
#include "wavread.h"
#include "pi4resample.h"
#include "iqread.h"
#include "pi4iqscan.h"
#include "pi4iqmix.h"

static void print_usage(const char *prog, FILE *out)
{
    fprintf(out, "pi4d %s - %s - PI4 (PharusIgnis4) decoder command-line tool\n",
            TW_VERSION, TW_BUILD_INFO);
    fprintf(out, "\n");
    fprintf(out, "Usage: %s [options] file.wav\n", prog);
    fprintf(out, "\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -f FREQ    Nominal CW carrier frequency, in Hz (default 800)\n");
    fprintf(out, "  -t TOL     Half-width of the search range, in Hz (default 150)\n");
    fprintf(out, "  -d DTMAX   Upper bound of the time-offset search, in seconds, for\n");
    fprintf(out, "             the first spot (default 2.0) - further spots later in the\n");
    fprintf(out, "             file are found automatically, see below\n");
    fprintf(out, "  -T HHMMSS  UTC timestamp to display (default: system time)\n");
    fprintf(out, "  -D         Diagnostic mode: prints all diagnostics on stderr\n");
    fprintf(out, "             (without -D, minimal KA9Q-style output: just the\n");
    fprintf(out, "             result line(s), or one error line on failure)\n");
    fprintf(out, "  -i         I/Q mode: file.wav is a wideband stereo I/Q capture\n");
    fprintf(out, "             (RIFF or RF64, left=I, right=Q), not SSB audio - use\n");
    fprintf(out, "             -t to cover the captured bandwidth (e.g. -t 27000 for\n");
    fprintf(out, "             a +/-27 kHz capture). -f is the baseband carrier\n");
    fprintf(out, "             frequency to center the search on (0 = I/Q center).\n");
    fprintf(out, "  -s         Scan mode (with -i): report every PI4 candidate found\n");
    fprintf(out, "             across the search band, not just the strongest -\n");
    fprintf(out, "             for a capture that may hold more than one station\n");
    fprintf(out, "  -v         Prints \"pi4d %s - %s\" and exits (no file needed)\n",
            TW_VERSION, TW_BUILD_INFO);
    fprintf(out, "  -V         Prints the version number and exits\n");
    fprintf(out, "  -h         Prints this help and exits\n");
    fprintf(out, "\n");
    fprintf(out, "Output (stdout): HHMMSS SNR   DT   DF : MESSAGE, one line per spot.\n");
    fprintf(out, "DT is always the offset from the very start of the file. After a\n");
    fprintf(out, "spot is decoded, the rest of the file is searched automatically for\n");
    fprintf(out, "another one (e.g. the beacon's next cycle); each gets its own line.\n");
    fprintf(out, "\n");
    fprintf(out, "Example:\n");
    fprintf(out, "  %s beacon.wav\n", prog);
    fprintf(out, "  %s -D -f 800 -t 150 -d 2.0 -T 190500 beacon.wav\n", prog);
}

int main(int argc, char *argv[])
{
    const char *path = NULL;
    double nfqso = 800.0;
    double ntol = 150.0;
    double dt_max = 2.0;
    double dt_step = PI4_TSYM / 10.0;
    char hhmmss[7];
    int have_hhmmss = 0;
    int verbose = 0;
    int iq_mode = 0;
    int scan_mode = 0;
    wavfile_t w;
    float *fdat;
    float sym[PI4_NSYM];
    char msg[PI4_MSGLEN + 1];
    long metric;
    long i;
    int rc;
    int argi;

    /* Hand-rolled, order-independent argument parser (no getopt): a
     * flag or valued option is recognized wherever it appears in
     * argv, in any order relative to the .wav filename or to other
     * options - e.g. "pi4d file.wav -v" and "pi4d -v file.wav" behave
     * identically. This also keeps behavior byte-identical across
     * platforms without depending on a system getopt() (absent on
     * MSVC) or a hand-written replacement having to replicate its
     * less obvious semantics (e.g. glibc's default argv permutation)
     * correctly. Options taking a value accept either the POSIX
     * glued form ("-f800") or a separate next argument ("-f 800"). */
    for (argi = 1; argi < argc; argi++) {
        const char *arg = argv[argi];

        if (arg[0] == '-' && arg[1] != '\0') {
            char opt = arg[1];
            const char *val = NULL;

            if (opt == 'f' || opt == 't' || opt == 'd' || opt == 'T') {
                if (arg[2] != '\0') {
                    val = arg + 2;
                } else if (argi + 1 < argc) {
                    val = argv[++argi];
                } else {
                    fprintf(stderr, "%s: option -%c requires an argument\n", argv[0], opt);
                    return 2;
                }
            }

            switch (opt) {
            case 'f':
                nfqso = atof(val);
                break;
            case 't':
                ntol = atof(val);
                break;
            case 'd':
                dt_max = atof(val);
                break;
            case 'T':
                if (strlen(val) != 6) {
                    fprintf(stderr, "%s: -T expects 6 digits (HHMMSS), got '%s'\n",
                            argv[0], val);
                    return 2;
                }
                memcpy(hhmmss, val, 6);
                hhmmss[6] = '\0';
                have_hhmmss = 1;
                break;
            case 'D':
                verbose = 1;
                break;
            case 'i':
                iq_mode = 1;
                break;
            case 's':
                scan_mode = 1;
                break;
            case 'v':
                printf("pi4d %s - %s\n", TW_VERSION, TW_BUILD_INFO);
                return 0;
            case 'V':
                printf("pi4d %s\n", TW_VERSION);
                return 0;
            case 'h':
                print_usage(argv[0], stdout);
                return 0;
            default:
                fprintf(stderr, "%s: unknown option '%s'\n\n", argv[0], arg);
                print_usage(argv[0], stderr);
                return 2;
            }
        } else {
            if (path != NULL) {
                fprintf(stderr, "%s: unexpected extra argument '%s'\n\n", argv[0], arg);
                print_usage(argv[0], stderr);
                return 2;
            }
            path = arg;
        }
    }

    if (path == NULL) {
        fprintf(stderr, "%s: missing .wav file\n\n", argv[0]);
        print_usage(argv[0], stderr);
        return 2;
    }

    if (!have_hhmmss) {
        time_t now = time(NULL);
#ifdef _WIN32
        struct tm tmv_buf;
        gmtime_s(&tmv_buf, &now);           /* MSVC: thread-safe variant, avoids C4996 */
        struct tm *tmv = &tmv_buf;
#else
        struct tm *tmv = gmtime(&now);
#endif
        snprintf(hhmmss, sizeof(hhmmss), "%02d%02d%02d",
                 tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    }

    if (iq_mode) {
        /*
         * Wideband I/Q path: locate candidate(s) with the coarse,
         * non-coherent iq_scan(), then hand each one to the SAME
         * validated, fully-coherent pipeline used by the SSB path
         * below (pi4_sync/pi4_symbol_metrics/pi4_estimate_snr/
         * pi4_decode_message), via a frequency-shifted, resampled
         * real-audio version of just that candidate's neighborhood -
         * see iqmix.h. None of those modules are modified for this:
         * the -22 dB sensitivity validated for SSB carries over
         * unchanged to each candidate decoded this way.
         */
        iqfile_t iq;
        iq_candidate_t candidates[64];
        int max_candidates = scan_mode ? 20 : 1;
        int min_quality_pct = 55;
        int n_cand, c, spot_count = 0;

        if (iq_read(path, &iq) != 0) {
            fprintf(stderr, "pi4d: failed to read '%s' as I/Q\n", path);
            return 1;
        }
        if (verbose) {
            fprintf(stderr, "I/Q file: %s (%d Hz, %d-bit, %ld samples = %.1f s)\n",
                    path, iq.sample_rate, iq.bits_per_sample, iq.nsamples,
                    (double)iq.nsamples / iq.sample_rate);
        }

        if (max_candidates > 64) max_candidates = 64;
        n_cand = iq_scan(iq.i_samples, iq.q_samples, iq.nsamples, iq.sample_rate,
                          nfqso, ntol, dt_max, min_quality_pct, max_candidates, candidates);
        if (n_cand <= 0) {
            fprintf(stderr,
                    "pi4d: no PI4 candidate found in the I/Q search window "
                    "(carrier %.0f +/- %.0f Hz, first %.1f s)\n", nfqso, ntol, dt_max);
            iq_free(&iq);
            return 1;
        }
        if (verbose) fprintf(stderr, "%d wideband candidate(s) above threshold.\n", n_cand);

        for (c = 0; c < n_cand; c++) {
            double margin = 2.0;
            long native_offset, native_len;
            double *resampled = NULL;
            long n_resampled = 0;
            float *fdat2;
            long k;

            native_offset = (long)lround((candidates[c].dt_hint - margin) * iq.sample_rate);
            if (native_offset < 0) native_offset = 0;
            native_len = iq.nsamples - native_offset;
            if (native_len <= 0) continue;

            if (iq_mix_to_audio(iq.i_samples + native_offset, iq.q_samples + native_offset,
                                 native_len, iq.sample_rate,
                                 candidates[c].carrier_freq_hz, 800.0, (int)PI4_FS,
                                 &resampled, &n_resampled) != 0) {
                if (verbose) fprintf(stderr, "Candidate %d: mix/resample failed, skipped.\n", c + 1);
                continue;
            }

            fdat2 = (float *)malloc(sizeof(float) * (size_t)n_resampled);
            for (k = 0; k < n_resampled; k++) fdat2[k] = (float)resampled[k];

            {
                /* 800.0 here matches the FIXED remix target above (not
                 * the user's -f/nfqso, which only centers the WIDE scan) -
                 * every candidate is remixed to land at the same standard,
                 * safely-positive position for this narrow re-sync. */
                double tone0_center = 800.0 - 0.5 * PI4_TONESPACING;
                double dtx_local, dfx_local, ccfpeak_local;
                int flip_local;
                double narrow_tol = 150.0;
                double narrow_dtmax = 2.0 * margin;

                rc = pi4_sync(resampled, (int)n_resampled, tone0_center, narrow_tol,
                              narrow_dtmax, dt_step, &dtx_local, &dfx_local,
                              &ccfpeak_local, &flip_local);

                if (rc == 0) {
                    double snr_db = pi4_estimate_snr(fdat2, (int)n_resampled, dtx_local, dfx_local);
                    rc = pi4_symbol_metrics(fdat2, (int)n_resampled, dtx_local, dfx_local,
                                             flip_local, sym);
                    if (verbose) {
                        fprintf(stderr,
                                "Candidate %d: carrier=%.1f Hz (scan hint), "
                                "DT(local)=%+.3f s  DF(tone0)=%.2f Hz  SNR=%.1f dB\n",
                                c + 1, candidates[c].carrier_freq_hz, dtx_local, dfx_local, snr_db);
                    }
                    if (rc == 0) {
                        rc = pi4_decode_message(sym, msg, &metric);
                        if (rc == 0) {
                            double dt_abs_file = (double)native_offset / iq.sample_rate + dtx_local;
                            char display[PI4_MSGLEN + 1];
                            int end, start_nonspace, kk;

                            end = PI4_MSGLEN;
                            while (end > 0 && msg[end - 1] == ' ') end--;
                            start_nonspace = 0;
                            while (start_nonspace < end && msg[start_nonspace] == ' ') start_nonspace++;
                            for (kk = 0; kk < end; kk++)
                                display[kk] = (kk < start_nonspace) ? '_' : msg[kk];
                            display[end] = '\0';

                            if (verbose) fprintf(stderr, "(Fano metric = %ld)\n", metric);

                            printf("%s %3.0f %5.1f %+8.1f : %s\n",
                                   hhmmss, snr_db, dt_abs_file,
                                   candidates[c].carrier_freq_hz, display);
                            spot_count++;
                        } else if (verbose) {
                            fprintf(stderr, "Candidate %d: no valid message decoded.\n", c + 1);
                        }
                    }
                } else if (verbose) {
                    fprintf(stderr, "Candidate %d: re-sync failed after mixing, skipped.\n", c + 1);
                }
            }

            free(fdat2);
            free(resampled);
        }

        iq_free(&iq);
        return spot_count > 0 ? 0 : 1;
    }

    if (wav_read(path, &w) != 0) {
        fprintf(stderr, "pi4d: failed to read '%s'\n", path);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "File: %s (%d Hz, %d channel(s), %d-bit, %ld samples = %.1f s)\n",
                path, w.sample_rate, w.channels, w.bits_per_sample,
                w.nsamples, (double)w.nsamples / w.sample_rate);
    }

    if (w.sample_rate != (int)PI4_FS) {
        double *resampled;
        long n_resampled;
        int rc_rs;

        if (verbose) {
            fprintf(stderr, "File is at %d Hz, resampling to %d Hz...\n",
                    w.sample_rate, (int)PI4_FS);
        }

        rc_rs = resample_to_rate(w.samples, w.nsamples, w.sample_rate,
                                  (int)PI4_FS, &resampled, &n_resampled);
        if (rc_rs != 0) {
            fprintf(stderr, "pi4d: resampling failed\n");
            wav_free(&w);
            return 1;
        }

        free(w.samples);
        w.samples = resampled;
        w.nsamples = n_resampled;
        w.sample_rate = (int)PI4_FS;

        if (verbose) {
            fprintf(stderr, "Resampled: %ld samples at %d Hz (%.1f s)\n",
                    w.nsamples, w.sample_rate, (double)w.nsamples / w.sample_rate);
        }
    }

    /* Converted to float once for the whole file. pi4_symbol_metrics()
     * and pi4_estimate_snr() are always called with an offset measured
     * from the true start of the file (search_offset_samples/PI4_FS
     * plus whatever local dtx that pass's pi4_sync() call returned),
     * so the DT printed for every spot - the first or any later one -
     * is always relative to the file's own start, never to a previous
     * spot or search window. */
    fdat = (float *)malloc(sizeof(float) * (size_t)w.nsamples);
    if (!fdat) {
        fprintf(stderr, "pi4d: memory allocation failed\n");
        wav_free(&w);
        return 1;
    }
    for (i = 0; i < w.nsamples; i++) fdat[i] = (float)w.samples[i];

    {
        /* nfqso is the nominal CW carrier frequency (OZ2M convention,
         * typically ~800 Hz), not directly the tone0 frequency. Tone0
         * sits at carrier - 0.5*spacing (see the PI4 doc: "Tone0 is
         * located K/2*12000/2048 below the CW carrier"). We convert
         * here so the search covers the right range without the user
         * having to do this calculation. */
        double tone0_center = nfqso - 0.5 * PI4_TONESPACING;
        long search_offset_samples = 0;
        int spot_count = 0;
        const int max_spots = 20;   /* safety valve - not expected to be reached */

        while (spot_count < max_spots) {
            long npts_remaining = w.nsamples - search_offset_samples;
            double remaining_sec = (double)npts_remaining / PI4_FS;
            double this_dt_max;
            double dtx_local, dtx_abs, dfx_local, ccfpeak_local, df_carrier;
            int flip_local;

            if (remaining_sec < PI4_MSGDUR + 0.2) {
                break;   /* not enough of the file left for another spot */
            }

            /* First spot: respect the user's requested search window
             * (-d). Further spots: the rest of the file is cheap to
             * search in full (see the shared-grid optimization in
             * sync_pi4.c), so just cover whatever remains. */
            this_dt_max = (spot_count == 0) ? dt_max : (remaining_sec - PI4_MSGDUR - 0.1);

            rc = pi4_sync(w.samples + search_offset_samples, (int)npts_remaining,
                          tone0_center, ntol, this_dt_max, dt_step,
                          &dtx_local, &dfx_local, &ccfpeak_local, &flip_local);
            if (rc != 0) {
                if (spot_count == 0) {
                    fprintf(stderr,
                            "pi4d: no PI4 spot found in the search window "
                            "(carrier %.0f +/- %.0f Hz, first %.1f s of the file)\n",
                            nfqso, ntol, this_dt_max);
                }
                break;
            }

            dtx_abs = (double)search_offset_samples / PI4_FS + dtx_local;
            df_carrier = dfx_local + 0.5 * PI4_TONESPACING;  /* for display, see above */

            {
                double snr_db = pi4_estimate_snr(fdat, (int)w.nsamples, dtx_abs, dfx_local);

                rc = pi4_symbol_metrics(fdat, (int)w.nsamples, dtx_abs, dfx_local, flip_local, sym);

                if (verbose) {
                    fprintf(stderr, "Spot %d: DT=%+.3f s  DF(tone0)=%.2f Hz  DF(carrier)=%.2f Hz  "
                            "flip=%+d  ccfpeak=%.1f  SNR=%.1f dB\n",
                            spot_count + 1, dtx_abs, dfx_local, df_carrier,
                            flip_local, ccfpeak_local, snr_db);
                }

                if (rc != 0) {
                    if (spot_count == 0) {
                        fprintf(stderr, "pi4d: demodulation window runs past the end of the file "
                                "(signal too short after the sync point)\n");
                    }
                    break;
                }

                rc = pi4_decode_message(sym, msg, &metric);
                if (rc == 0) {
                    char display[PI4_MSGLEN + 1];
                    int end, start_nonspace, k;

                    /* LEADING spaces -> underscores; TRAILING spaces -> string truncated. */
                    end = PI4_MSGLEN;
                    while (end > 0 && msg[end - 1] == ' ') end--;

                    start_nonspace = 0;
                    while (start_nonspace < end && msg[start_nonspace] == ' ') start_nonspace++;

                    for (k = 0; k < end; k++) {
                        display[k] = (k < start_nonspace) ? '_' : msg[k];
                    }
                    display[end] = '\0';

                    if (verbose) {
                        fprintf(stderr, "(Fano metric = %ld)\n", metric);
                    }

                    /* jt9-inspired format: HHMMSS SNR   DT   DF : MESSAGE
                     * Displayed DF = carrier-equivalent frequency (~800 Hz),
                     * consistent with the input nfqso - not the internal
                     * tone0 used by the demodulation pipeline. DT is always
                     * measured from the start of the file (dtx_abs), never
                     * from a previous spot. The ':' separates the numeric
                     * fields from the payload, as in jt9's output format. */
                    printf("%s %3.0f %4.1f %4.0f : %s\n",
                           hhmmss, snr_db, dtx_abs, df_carrier, display);

                    spot_count++;
                    search_offset_samples += (long)lround((dtx_local + PI4_MSGDUR) * PI4_FS);
                    continue;
                } else {
                    if (spot_count == 0) {
                        fprintf(stderr,
                                "pi4d: no valid message decoded "
                                "(candidate at DT=%+.3f s, DF=%.0f Hz)\n",
                                dtx_abs, df_carrier);
                    }
                    break;
                }
            }
        }

        free(fdat);
        wav_free(&w);

        return (spot_count > 0) ? 0 : 1;
    }
}
