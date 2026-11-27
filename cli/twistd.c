/*
 * twistd.c - The OOK decoders' command-line tool, built twice:
 *
 *   twistd (TW_WITH_TWIST)    the TWIST frames:
 *     TWS  TWIST short: callsign, LDPC (119,44) + CRC-16, 239 chips
 *     TWL  TWIST long: callsign + locator, LDPC (160,69) + CRC-16,
 *          321 chips (see PROTOCOL.md)
 *   operad (TW_WITH_CLASSIC)  the classic frame: callsign, 239 chips,
 *          printed under TW_CLASSIC_NAME (twnames.h) for
 *          compatibility, with the deep search for known callsigns
 *
 * Usage:
 *   twistd [options] file.wav        operad [options] file.wav
 *
 * One receiver (SSB audio or, with -i, I/Q; narrowband or wideband
 * scan). One line per spot:
 *
 *   HHMMSS SNR   DT   DF MODE SUB : CALLSIGN [LOCATOR] [*]
 *
 * MODE SUB = "TWIST S" (short), "TWIST L" (long), TW_CLASSIC_NAME
 * " 05" (Op05).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "twdefs.h"
#include "twpipe.h"
#include "twnames.h"
#include "wavread.h"
#include "iqread.h"

#if TW_WITH_CLASSIC
#include "twcdeep.h"
#define PROG "operad"
#define WHAT TW_CLASSIC_NAME
#else
#define PROG "twistd"
#define WHAT "TWIST"
#endif

#define MAX_SPOTS 64
#define MAX_KNOWN 256

/* Above this search half-width (Hz), the wideband path is used: a
 * fast pre-detection over the whole band, then the full decoder on a
 * narrow tile around each detection (twscan.h, tw_decode_wide). */
#define WIDE_TOL 1000.0

static void print_usage(const char *prog, FILE *out)
{
#if TW_WITH_CLASSIC
    fprintf(out, PROG " %s - %s - " TW_CLASSIC_NAME " decoder command-line tool (TWIST Console)\n",
            TW_VERSION, TW_BUILD_INFO);
#else
    fprintf(out, PROG " %s - %s - TWIST decoder command-line tool (TWIST Console)\n",
            TW_VERSION, TW_BUILD_INFO);
#endif
    fprintf(out, "\n");
    fprintf(out, "Usage: %s [options] file.wav\n", prog);
    fprintf(out, "\n");
    fprintf(out, "Options:\n");
#if TW_WITH_CLASSIC
    fprintf(out, "  -m MODE    Sub-mode: 05 (=0.5), 1, 2, 4, 8, 16, 32 (default 05).\n");
    fprintf(out, "             One chip lasts 0.256 x MODE s, a frame 239 chips\n");
    fprintf(out, "             (Op05: 30.6 s, Op1: 61.2 s, Op2: 2.04 min...)\n");
#else
    fprintf(out, "  -p LIST    Frames searched, comma-separated: tws (TWIST short,\n");
    fprintf(out, "             callsign), twl (TWIST long, callsign + locator), all\n");
    fprintf(out, "             (default: both). " TW_CLASSIC_NAME ": operad; PI4: pi4d\n");
#endif
    fprintf(out, "  -f FREQ    Expected carrier frequency, Hz (default 1500; with -i,\n");
    fprintf(out, "             relative to the I/Q center, default 0)\n");
    fprintf(out, "  -t TOL     Half-width of the frequency search, Hz (default 200)\n");
    fprintf(out, "  -d [MIN:]MAX  Window for the frame START, s from the file start;\n");
    fprintf(out, "             \"-d 59:\" = starting at 59 s or later\n");
    fprintf(out, "             (default: anywhere - " WHAT " is not time-synchronous)\n");
    fprintf(out, "  -e [MIN:]MAX  Window for the frame END, s from the file start:\n");
    fprintf(out, "             \"-e 42:45\" on a 45 s block = frames that ended in the\n");
    fprintf(out, "             last 3 s\n");
#if TW_WITH_CLASSIC
    fprintf(out, "  -c CALLS   Known callsigns: comma-separated list, or a file (one or\n");
    fprintf(out, "             more per line, '#' comments). Enables the deep search:\n");
    fprintf(out, "             a matched filter for each listed callsign, a few dB more\n");
    fprintf(out, "             sensitive than blind decoding. Such spots are marked\n");
    fprintf(out, "             with a trailing ' *'\n");
    fprintf(out, "  -k         Report only callsigns of the -c list\n");
    fprintf(out, "  -N         No deep search (keep -c for -k only)\n");
    fprintf(out, "  -F FA      Deep search: accepted false alarms per run (default 1e-4)\n");
    fprintf(out, "  -L N       List-decoder depth, frames checked per candidate\n");
    fprintf(out, "             (default 32; 1 = most conservative)\n");
    fprintf(out, "  -M MARGIN  Minimum lead of a decoded frame over its CRC twins,\n");
    fprintf(out, "             in noise units (default 10; 0 disables - see README)\n");
#else
    fprintf(out, "  -c CALLS   Known callsigns: comma-separated list, or a file (one or\n");
    fprintf(out, "             more per line, '#' comments), for -k\n");
    fprintf(out, "  -k         Report only callsigns of the -c list\n");
    fprintf(out, "  -G MARGIN  Minimum lead of a frame found by OSD over the next\n");
    fprintf(out, "             nearest codeword (default 2; 0 disables - see README)\n");
#endif
    fprintf(out, "  -O LIMIT   Pattern consistency: refuse a blind decode whose OFF\n");
    fprintf(out, "             chips hold another signal (LIMIT + 5 %% of the ON/OFF\n");
    fprintf(out, "             contrast, noise units; default 0.6, 0 disables)\n");
    fprintf(out, "  -P         Keep callsigns that fail the ITU-structure plausibility\n");
    fprintf(out, "             check (dropped by default - see README)\n");
    fprintf(out, "  -r DRIFT   Largest frequency drift searched, Hz/min (default: about\n");
    fprintf(out, "             +/- 1/T over a frame, e.g. 7.7 Hz/min in Op05)\n");
    fprintf(out, "  -w WIDTH   Spread mode: chip energy summed over WIDTH Hz, for signals\n");
    fprintf(out, "             smeared in frequency (rain/aircraft scatter at microwave\n");
    fprintf(out, "             frequencies). Default 0 = clean carrier\n");
#if TW_WITH_CLASSIC
    fprintf(out, "  -C         SNR = carrier power while keyed ON (default: average\n");
    fprintf(out, "             power over the frame, 3 dB lower, as the official " TW_CLASSIC_NAME "\n");
    fprintf(out, "             software reports it)\n");
#else
    fprintf(out, "  -C         SNR = carrier power while keyed ON (default: average\n");
    fprintf(out, "             power over the frame, about 3 dB lower)\n");
#endif
    fprintf(out, "  -T HHMMSS  UTC timestamp to display (default: system time)\n");
    fprintf(out, "  -i         I/Q mode: file.wav is a stereo I/Q capture (RIFF or RF64,\n");
    fprintf(out, "             left=I, right=Q), not SSB audio\n");
    fprintf(out, "  -Z SIGMA   Wideband scan (-t above 1000 Hz, e.g. a 100 kHz I/Q\n");
    fprintf(out, "             capture with -i -t 50000): pre-detection threshold, robust\n");
    fprintf(out, "             sigmas (default 6)\n");
    fprintf(out, "  -S N       Wideband scan: at most N frequencies decoded (default 40)\n");
    fprintf(out, "  -D         Diagnostic mode: all diagnostics on stderr\n");
    fprintf(out, "  -v         Prints \"" PROG " %s - %s\" and exits\n", TW_VERSION, TW_BUILD_INFO);
    fprintf(out, "  -V         Prints the version number and exits\n");
    fprintf(out, "  -h         Prints this help and exits\n");
    fprintf(out, "\n");
    fprintf(out, "Output (stdout): HHMMSS SNR   DT   DF MODE SUB : CALLSIGN [LOCATOR] [*],\n");
    fprintf(out, "one line per spot, sorted by DT (start of the frame, s from the start\n");
#if TW_WITH_CLASSIC
    fprintf(out, "of the file). MODE SUB: " TW_CLASSIC_NAME " 05 (sub-mode OpN). SNR in dB in\n");
    fprintf(out, "2500 Hz, average power over the frame (-C: carrier-on power).\n");
    fprintf(out, "\n");
    fprintf(out, "Examples:\n");
    fprintf(out, "  %s beacon.wav\n", prog);
    fprintf(out, "  %s -c F5ZZO,F6FBB -f 1500 -t 20 -d 1:3 beacon.wav\n", prog);
#else
    fprintf(out, "of the file). MODE SUB: TWIST S (short, callsign) or TWIST L (long,\n");
    fprintf(out, "callsign + LOCATOR). SNR in dB in 2500 Hz, average power over the\n");
    fprintf(out, "frame (-C: carrier-on power).\n");
    fprintf(out, "\n");
    fprintf(out, "Examples:\n");
    fprintf(out, "  %s beacon.wav\n", prog);
    fprintf(out, "  %s -p twl beacon.wav\n", prog);
#endif
    fprintf(out, "  %s -i -t 50000 band_100kHz_iq.wav\n", prog);
}

/* The known callsigns (-c): those of the classic frame are encoded for
 * its deep search; TWIST keeps their names, for -k. */
#if TW_WITH_CLASSIC
typedef twc_known_t known_t;
#else
typedef struct { char call[12]; } known_t;
#endif

/* Adds the callsigns found in text (separated by commas, blanks or
 * newlines; '#' starts a comment up to the end of the line). */
static int add_known_text(const char *text, known_t *known, int *nknown, const char *prog)
{
    const char *q = text;
    while (*q) {
        char tok[16];
        int n = 0;
        if (*q == '#') { while (*q && *q != '\n') q++; continue; }
        if (*q == ',' || isspace((unsigned char)*q)) { q++; continue; }
        while (*q && *q != ',' && *q != '#' && !isspace((unsigned char)*q)) {
            if (n < 15) tok[n++] = *q;
            q++;
        }
        tok[n] = '\0';
        if (*nknown >= MAX_KNOWN) {
            fprintf(stderr, "%s: too many known callsigns (max %d)\n", prog, MAX_KNOWN);
            return -1;
        }
#if TW_WITH_CLASSIC
        if (twc_known_init(tok, &known[*nknown]) != 0) {
            fprintf(stderr, "%s: '%s' cannot be encoded in " TW_CLASSIC_NAME ", ignored\n", prog, tok);
            continue;
        }
#else
        {
            int i;
            if (n > 11) { fprintf(stderr, "%s: '%s' is not a callsign, ignored\n", prog, tok); continue; }
            for (i = 0; i <= n; i++) known[*nknown].call[i] = (char)toupper((unsigned char)tok[i]);
        }
#endif
        (*nknown)++;
    }
    return 0;
}

static int load_known(const char *arg, known_t *known, int *nknown, const char *prog)

{
    FILE *f = fopen(arg, "r");
    if (f) {
        char *buf;
        long len;
        int rc;
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len < 0) len = 0;
        buf = (char *)malloc((size_t)len + 1);
        if (!buf) { fclose(f); return -1; }
        len = (long)fread(buf, 1, (size_t)len, f);
        buf[len] = '\0';
        fclose(f);
        rc = add_known_text(buf, known, nknown, prog);
        free(buf);
        return rc;
    }
    return add_known_text(arg, known, nknown, prog);
}

/* case-insensitive prefix comparison (portable: no strncasecmp on MSVC) */
static int ieq(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

#if TW_WITH_CLASSIC
static int parse_mode(const char *s, double *n)
{
    double v;
    if ((s[0] == 'O' || s[0] == 'o') && (s[1] == 'P' || s[1] == 'p')) s += 2;
    if (!strcmp(s, "05") || !strcmp(s, "0.5")) { *n = 0.5; return 0; }
    v = atof(s);
    if (v == 1 || v == 2 || v == 4 || v == 8 || v == 16 || v == 32) { *n = v; return 0; }
    return -1;
}
#else
/* TWIST frames: Op05 only ("-m 05" accepted, as before) */
static int parse_mode(const char *s, double *n)
{
    if ((s[0] == 'O' || s[0] == 'o') && (s[1] == 'P' || s[1] == 'p')) s += 2;
    if (!strcmp(s, "05") || !strcmp(s, "0.5")) { *n = 0.5; return 0; }
    return -1;
}
#endif

int main(int argc, char *argv[])
{
    const char *path = NULL;
    tw_params_t p;
    static known_t known[MAX_KNOWN];
#if !TW_WITH_CLASSIC
    static char calls[MAX_KNOWN][12];
#endif
    int nknown = 0;
    tw_spot_t spots[MAX_SPOTS];
    char hhmmss[7];
    int have_hhmmss = 0, have_f = 0, iq_mode = 0;
    int nspots, k, argi;
    double scan_z = 6.0;
    int scan_max = 40;

    tw_params_default(&p);
#if TW_WITH_CLASSIC
    p.protocols = TW_PROTO_CLASSIC;
#else
    p.protocols = TW_PROTO_S | TW_PROTO_L;
#endif

    /* Same hand-rolled, order-independent parser as pi4d: options may
     * come before or after the file name, "-f800" or "-f 800". */
    for (argi = 1; argi < argc; argi++) {
        const char *arg = argv[argi];
        if (arg[0] == '-' && arg[1] != '\0') {
            char opt = arg[1];
            const char *val = NULL;
            if (strchr("mftdecFLMGOrTwZSp", opt)) {
                if (arg[2] != '\0') val = arg + 2;
                else if (argi + 1 < argc) val = argv[++argi];
                else {
                    fprintf(stderr, "%s: option -%c requires an argument\n", argv[0], opt);
                    return 2;
                }
            }
            switch (opt) {
            case 'm':
                if (parse_mode(val, &p.mode_n) != 0) {
#if TW_WITH_CLASSIC
                    fprintf(stderr, "%s: unknown mode '%s' (05, 1, 2, 4, 8, 16, 32)\n", argv[0], val);
#else
                    fprintf(stderr, "%s: unknown mode '%s' (TWIST: 05 only)\n", argv[0], val);
#endif
                    return 2;
                }
                break;
            case 'f': p.f_center = atof(val); have_f = 1; break;
            case 'p': {
                const char *q = val;
                p.protocols = 0;
                while (*q) {
                    int n = (int)strcspn(q, ",");
#if TW_WITH_CLASSIC
                    /* the classic frame: "classic", or its display name */
                    if ((n == 7 && ieq(q, "classic", 7)) || (n == 3 && ieq(q, "all", 3)) ||
                        (n == (int)strlen(TW_CLASSIC_NAME) && ieq(q, TW_CLASSIC_NAME, n))) p.protocols |= TW_PROTO_CLASSIC;
                    else if (n > 0) {
                        fprintf(stderr, "%s: '%.*s': " PROG " decodes the " TW_CLASSIC_NAME " frame only "
                                "(TWIST: twistd, PI4: pi4d)\n", argv[0], n, q);
                        return 2;
                    }
#else
                    if (n == 3 && ieq(q, "tws", 3)) p.protocols |= TW_PROTO_S;
                    else if (n == 3 && ieq(q, "twl", 3)) p.protocols |= TW_PROTO_L;
                    else if ((n == 5 && ieq(q, "twist", 5)) || (n == 3 && ieq(q, "all", 3))) p.protocols |= TW_PROTO_S | TW_PROTO_L;
                    else if (n > 0) {
                        fprintf(stderr, "%s: unknown frame '%.*s' (tws, twl, twist, all; "
                                TW_CLASSIC_NAME ": operad, PI4: pi4d)\n", argv[0], n, q);
                        return 2;
                    }
#endif
                    q += n;
                    if (*q == ',') q++;
                }
                if (!p.protocols) { fprintf(stderr, "%s: -p: no protocol\n", argv[0]); return 2; }
                break;
            }
            case 't': p.tol = atof(val); break;
            case 'd': {
                const char *colon = strchr(val, ':');
                if (colon) {
                    p.dt_min = atof(val);
                    /* "-d 59:" (nothing after the colon) = no upper limit */
                    p.dt_max = colon[1] ? atof(colon + 1) : -1.0;
                }
                else p.dt_max = atof(val);
                break;
            }
            case 'e': {
                const char *colon = strchr(val, ':');
                if (colon) {
                    p.end_min = atof(val);
                    p.end_max = colon[1] ? atof(colon + 1) : -1.0;
                }
                else p.end_max = atof(val);
                break;
            }
            case 'c':
                if (load_known(val, known, &nknown, argv[0]) != 0) return 2;
                break;
            case 'k': p.known_only = 1; break;
#if TW_WITH_CLASSIC
            case 'N': p.deep = 0; break;
            case 'F': p.deep_fa = atof(val); break;
            case 'L': p.list_max = atoi(val); break;
            case 'M': p.twin_margin = atof(val); break;
#else
            case 'G': p.osd_margin = atof(val); break;
#endif
            case 'P': p.plausible_only = 0; break;
            case 'O': p.off_max = atof(val); break;
            case 'r': p.drift_max = atof(val) / 60.0; break;
            case 'w': p.spread_hz = atof(val); break;
            case 'C': p.snr_avg = 0; break;
            case 'Z': scan_z = atof(val); break;
            case 'S': scan_max = atoi(val); break;
            case 'T':
                if (strlen(val) != 6) {
                    fprintf(stderr, "%s: -T expects 6 digits (HHMMSS), got '%s'\n", argv[0], val);
                    return 2;
                }
                memcpy(hhmmss, val, 6);
                hhmmss[6] = '\0';
                have_hhmmss = 1;
                break;
            case 'i': iq_mode = 1; break;
            case 'D': p.verbose = 1; break;
            case 'v': printf(PROG " %s - %s\n", TW_VERSION, TW_BUILD_INFO); return 0;
            case 'V': printf(PROG " %s\n", TW_VERSION); return 0;
            case 'h': print_usage(argv[0], stdout); return 0;
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
    if (p.known_only && nknown == 0) {
        fprintf(stderr, "%s: -k needs a list of known callsigns (-c)\n", argv[0]);
        return 2;
    }
    if (iq_mode && !have_f) p.f_center = 0.0;
    if (p.list_max < 1) p.list_max = 1;
#if TW_WITH_CLASSIC
    p.known = known;
    p.nknown = nknown;
#else
    for (k = 0; k < nknown; k++) memcpy(calls[k], known[k].call, 12);
    p.calls = (const char (*)[12])calls;
    p.ncalls = nknown;
#endif

    if (!have_hhmmss) {
        time_t now = time(NULL);
#ifdef _WIN32
        struct tm tmv_buf;
        gmtime_s(&tmv_buf, &now);
        struct tm *tmv = &tmv_buf;
#else
        struct tm *tmv = gmtime(&now);
#endif
        snprintf(hhmmss, sizeof(hhmmss), "%02d%02d%02d",
                 tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    }

    if (iq_mode) {
        iqfile_t iq;
        if (iq_read(path, &iq) != 0) {
            fprintf(stderr, PROG ": failed to read '%s' as I/Q\n", path);
            return 1;
        }
        if (p.verbose)
            fprintf(stderr, "I/Q file: %s (%d Hz, %d-bit, %ld samples = %.1f s)\n",
                    path, iq.sample_rate, iq.bits_per_sample, iq.nsamples,
                    (double)iq.nsamples / iq.sample_rate);
        if (p.tol > WIDE_TOL)
            nspots = tw_decode_wide(iq.i_samples, iq.q_samples, iq.nsamples, iq.sample_rate,
                                       &p, scan_z, scan_max, spots, MAX_SPOTS);
        else
            nspots = tw_decode_iq(iq.i_samples, iq.q_samples, iq.nsamples, iq.sample_rate,
                                     &p, spots, MAX_SPOTS);
        iq_free(&iq);
    } else {
        wavfile_t w;
        if (wav_read(path, &w) != 0) {
            fprintf(stderr, PROG ": failed to read '%s'\n", path);
            return 1;
        }
        if (p.verbose)
            fprintf(stderr, "File: %s (%d Hz, %d channel(s), %d-bit, %ld samples = %.1f s)\n",
                    path, w.sample_rate, w.channels, w.bits_per_sample, w.nsamples,
                    (double)w.nsamples / w.sample_rate);
        if (p.tol > WIDE_TOL)
            nspots = tw_decode_wide(w.samples, NULL, w.nsamples, w.sample_rate,
                                       &p, scan_z, scan_max, spots, MAX_SPOTS);
        else
            nspots = tw_decode_real(w.samples, w.nsamples, w.sample_rate, &p, spots, MAX_SPOTS);
        wav_free(&w);
    }

    if (nspots < 0) {
        fprintf(stderr, PROG ": '%s' could not be processed (shorter than one Op%g chip, "
                "or out of memory)\n", path, p.mode_n);
        return 1;
    }
    if (nspots == 0) {
        fprintf(stderr, PROG ": no " WHAT " spot found (Op%g, carrier %.0f +/- %.0f Hz)\n",
                p.mode_n, p.f_center, p.tol);
        return 1;
    }

    for (k = 0; k < nspots; k++) {
        char sub[16];
        const char *mode = spots[k].proto == TW_PROTO_CLASSIC ? TW_CLASSIC_NAME : "TWIST";
        if (spots[k].proto == TW_PROTO_S) strcpy(sub, "S");
        else if (spots[k].proto == TW_PROTO_L) strcpy(sub, "L");
        else if (p.mode_n == 0.5) strcpy(sub, "05");
        else snprintf(sub, sizeof sub, "%g", p.mode_n);
        if (iq_mode)
            printf("%s %3.0f %5.1f %+8.1f %s %s : %s%s%s%s\n", hhmmss, spots[k].snr, spots[k].dt,
                   spots[k].df, mode, sub, spots[k].call, spots[k].loc[0] ? " " : "", spots[k].loc,
                   spots[k].deep ? " *" : "");
        else
            printf("%s %3.0f %4.1f %4.0f %s %s : %s%s%s%s\n", hhmmss, spots[k].snr, spots[k].dt,
                   spots[k].df, mode, sub, spots[k].call, spots[k].loc[0] ? " " : "", spots[k].loc,
                   spots[k].deep ? " *" : "");
    }
    return 0;
}
