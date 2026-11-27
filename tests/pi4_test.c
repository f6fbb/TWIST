/* pi4_test.c - PI4 frames through libtwist's pipeline (core/pi4pipe.c).
 *   pi4_test            the checks of make test (PASS/FAIL lines, exit 0)
 *   pi4_test sweep N    decode rate from -26 to -18 dB, N frames per point
 *   pi4_test noise N    N short passes on noise: anything decoded is false
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twpipe.h"
#include "pi4pipe.h"
#include "synth.h"

static int fs = 12000;
static int fail = 0;
static void check(int ok, const char *what) { printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fail = 1; }

/* one block of len s: noise sigma 0.1, a PI4 frame at t0 */
static double *block(double len, const char *msg, double t0, double carrier, double snr, unsigned long seed, long *n)
{
    double *x;
    *n = (long)(len * fs);
    x = calloc((size_t)*n, sizeof(double));
    synth_seed(seed);
    if (msg) synth_add_pi4(x, *n, fs, msg, t0, carrier, synth_amp_for_snr(snr, 0.1, fs), 0.7);
    synth_add_noise(x, *n, 0.1);
    return x;
}

static int decode(const double *x, long n, double end_min, double end_max, tw_spot_t *sp, int max)
{
    tw_params_t p;
    tw_params_default(&p);
    p.protocols = TW_PROTO_PI4;
    p.end_min = end_min; p.end_max = end_max;
    return tw_decode_real(x, n, fs, &p, sp, max);
}

int main(int argc, char **argv)
{
    tw_spot_t sp[8];
    long n;
    int ns, k;
    double *x;

    if (argc > 2 && !strcmp(argv[1], "sweep")) {
        int trials = atoi(argv[2]);
        double snr;
        for (snr = -26; snr <= -18; snr += 1) {
            int ok = 0, bad = 0, t;
            for (t = 0; t < trials; t++) {
                x = block(30, "F5ZZO", 1.0 + 0.37 * (t % 5), 700 + 23 * (t % 9), snr, 1000 + t, &n);
                ns = decode(x, n, -1e9, -1, sp, 8);
                for (k = 0; k < ns; k++) { if (!strcmp(sp[k].call, "F5ZZO")) ok++; else bad++; }
                free(x);
            }
            printf("SNR %5.1f dB: %3d/%d decoded (%.0f%%), %d false\n", snr, ok, trials, 100.0 * ok / trials, bad);
            fflush(stdout);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "noise")) {
        int passes = atoi(argv[2]), t, bad = 0;
        for (t = 0; t < passes; t++) {
            x = block(45, NULL, 0, 0, 0, 5000 + t, &n);
            ns = decode(x, n, 45 - 3.5, 45, sp, 8);          /* a short pass of the application */
            for (k = 0; k < ns; k++) { printf("FALSE pass %d: \"%s\" \"%s\" SNR %.1f\n", t, sp[k].call, sp[k].loc, sp[k].snr); bad++; }
            free(x);
        }
        printf("%d passes on noise, %d false decodes\n", passes, bad);
        return 0;
    }

    /* 1. a callsign, then a locator message, at -18 dB */
    x = block(30, "F5ZZO", 1.4, 800, -18, 1, &n);
    ns = decode(x, n, -1e9, -1, sp, 8);
    check(ns == 1 && !strcmp(sp[0].call, "F5ZZO") && !sp[0].loc[0] && sp[0].proto == TW_PROTO_PI4
          && sp[0].dt > 1.35 && sp[0].dt < 1.45 && sp[0].df > 797 && sp[0].df < 803 && sp[0].snr > -20 && sp[0].snr < -16,
          "PI4 at -18 dB: F5ZZO, its start, carrier and SNR");
    free(x);
    x = block(30, "/JN12AX", 2.0, 760, -18, 2, &n);
    ns = decode(x, n, -1e9, -1, sp, 8);
    check(ns == 1 && !sp[0].call[0] && !strcmp(sp[0].loc, "JN12AX"), "PI4 locator message /JN12AX: loc set, no callsign");
    free(x);

    /* 2. two frames one minute apart in a 90 s block: both */
    {
        double *y;
        long ny = 90L * fs;
        y = calloc((size_t)ny, sizeof(double));
        synth_seed(3);
        synth_add_pi4(y, ny, fs, "F5ZZO", 1.0, 800, synth_amp_for_snr(-15, 0.1, fs), 0.1);
        synth_add_pi4(y, ny, fs, "/JN12AX", 61.0, 800, synth_amp_for_snr(-15, 0.1, fs), 0.2);
        synth_add_noise(y, ny, 0.1);
        ns = decode(y, ny, -1e9, -1, sp, 8);
        check(ns == 2 && !strcmp(sp[0].call, "F5ZZO") && !strcmp(sp[1].loc, "JN12AX"), "two PI4 frames in 90 s, in order");
        /* 3. the short passes of the application (45 s blocks every 3 s,
         * frames ended in the last 3.5 s): each frame exactly once */
        {
            int count = 0;
            long end;
            for (end = 3L * fs; end <= ny; end += 3L * fs) {
                long start = end > 45L * fs ? end - 45L * fs : 0;
                double len = (double)(end - start) / fs;
                ns = decode(y + start, end - start, len - 3.5, len, sp, 8);
                count += ns;
            }
            check(count == 2, "short passes: each PI4 frame once");
        }
        free(y);
    }

    /* 4. the message checks */
    {
        char c[12], l[7];
        check(tw_pi4_message("F5ZZO   ", c, l) == 1 && !strcmp(c, "F5ZZO"), "message F5ZZO: a callsign");
        check(tw_pi4_message("F5ZZO/P ", c, l) == 1 && !strcmp(c, "F5ZZO/P"), "message F5ZZO/P: a callsign");
        check(tw_pi4_message("EA8/F5ZZ", c, l) == 1, "message EA8/F5ZZ: a callsign");
        check(tw_pi4_message("/JN12AX ", c, l) == 2 && !strcmp(l, "JN12AX"), "message /JN12AX: a locator");
        check(tw_pi4_message("Q7XKZ.A ", c, l) == 0 && tw_pi4_message("/ZZ99   ", c, l) == 0
              && tw_pi4_message("AB CD   ", c, l) == 0 && tw_pi4_message("12345678", c, l) == 0,
              "messages refused: Q7XKZ.A, /ZZ99, AB CD, 12345678");
    }
    return fail;
}
