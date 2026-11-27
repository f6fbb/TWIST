/*
 * interf.c - False decodes on non-TWIST keyed signals (the wideband
 * scan sends every keyed carrier of the band to the decoder).
 * usage: interf type trials seed snr_lo snr_hi
 *   type 0: Morse CW (20 wpm) with long carrier periods
 *   type 1: PI4-like: one 4-FSK tone seen alone, 166.7 ms symbols, 25 % duty
 *   type 2: random on/off keying at 128 ms (classic-like chip rate)
 *   type 3: random on/off keying at 256 ms (an Op1 frame seen as Op05)
 *   type 4: a true Op1 frame, decoded as Op05
 *   type 9: true Op05 frame (reference: metrics of good decodes)
 * Prints one line per decode: type call snr contrast off_excess z score rank loss good
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twcenc.h"
#include "twpipe.h"
#include "synth.h"

static double urand(void) { return 0.5 + 0.5 * erf(synth_gauss() / sqrt(2.0)); }

static void add_keyed(double *x, long n, int fs, const unsigned char *on, int nsym, double tsym,
                      double t0, double f, double amp)
{
    long k; double ph = 6.28 * urand();
    for (k = 0; k < n; k++) {
        double t = (double)k / fs - t0; int s;
        if (t < 0) continue;
        s = (int)(t / tsym);
        if (s >= nsym) break;
        if (on[s]) x[k] += amp * cos(2 * M_PI * f * k / fs + ph);
    }
}

static const char *calls[] = { "F6FBB","F1HDI","F5ZZO","OZ7IGY","EA5XYZ","G4JNT","DL0ABC","K1JT","9A1A","VK2ABC" };

int main(int argc, char **argv)
{
    int type, trials;
    unsigned long seed;
    double slo, shi;
    int fs = 12000, t, k;
    long N = (long)(40.0 * fs);
    double *x = malloc(sizeof(double) * N);
    static unsigned char on[4000];
    if (argc < 6) { fprintf(stderr, "usage: interf type trials seed snr_lo snr_hi\n"); return 1; }
    type = atoi(argv[1]); trials = atoi(argv[2]);
    seed = strtoul(argv[3], 0, 10);
    slo = atof(argv[4]); shi = atof(argv[5]);
    synth_seed(seed * 7777 + 3);
    for (t = 0; t < trials; t++) {
        double snr = slo + (shi - slo) * urand();
        double amp = synth_amp_for_snr(snr, 0.1, fs);
        double f = 1500 + 10 * (urand() - 0.5);
        tw_params_t p; tw_spot_t sp[16]; int ns, nsym = 0;
        double tsym = 0.128;
        const char *truth = "";
        memset(x, 0, sizeof(double) * N);
        if (type == 0) {
            /* 15 s steady carrier, then Morse, repeated */
            double u = 0.06; int i = 0;
            tsym = u;
            while (i < 600) {
                int j;
                for (j = 0; j < 250 && i < 600; j++) on[i++] = 1;   /* 15 s carrier */
                for (j = 0; j < 7 && i < 600; j++) on[i++] = 0;
                for (j = 0; j < 60 && i < 600; j++) {              /* ~ 60 elements */
                    int dash = urand() < 0.5, m, gap = urand() < 0.3 ? 3 : 1;
                    for (m = 0; m < (dash ? 3 : 1) && i < 600; m++) on[i++] = 1;
                    for (m = 0; m < gap && i < 600; m++) on[i++] = 0;
                }
            }
            nsym = 600;
            add_keyed(x, N, fs, on, nsym, tsym, 0.0, f, amp);
        } else if (type == 1) {
            tsym = 1.0 / 6.0; nsym = 146;
            for (k = 0; k < nsym; k++) on[k] = urand() < 0.25;
            add_keyed(x, N, fs, on, nsym, tsym, 1.0 + 5 * urand(), f, amp);
        } else if (type == 2 || type == 3) {
            tsym = type == 2 ? 0.128 : 0.256; nsym = (int)(38.0 / tsym);
            for (k = 0; k < nsym; k++) on[k] = urand() < 0.5;
            add_keyed(x, N, fs, on, nsym, tsym, 0.5 * urand(), f, amp);
        } else if (type == 4 || type == 9) {
            unsigned char chips[TWC_NCHIPS];
            truth = calls[t % 10];
            twc_encode(truth, chips, NULL);
            tsym = type == 4 ? 0.256 : 0.128;
            synth_add_classic(x, N, fs, chips, tsym, type == 4 ? -10.0 * urand() : 1.0 + 4 * urand(),
                            f, amp, 6.28 * urand(), 0.0, 0.005);
        }
        synth_add_noise(x, N, 0.1);
        tw_params_default(&p); p.snr_avg = 0; p.mode_n = 0.5;
        p.f_center = f + 3 * (urand() - 0.5); p.tol = 20.0;
        if (getenv("MAXCAND")) p.max_cand = atoi(getenv("MAXCAND"));
        ns = tw_decode_real(x, N, fs, &p, sp, 16);
        for (k = 0; k < ns; k++) {
            int good = 0;
            if (type == 9) {
                char norm[7], tr[7]; int a = 0, b;
                tw_normalize_call(truth, norm);
                for (b = 0; b < 6; b++) if (norm[b] != ' ') tr[a++] = norm[b];
                tr[a] = 0; good = !strcmp(tr, sp[k].call);
            }
            printf("%d %s inj=%.1f snr=%.1f con=%.2f offx=%.3f z=%.1f score=%.1f rank=%d loss=%.1f nv=%d %s\n",
                   type, sp[k].call, snr, sp[k].snr, sp[k].contrast, sp[k].off_excess, sp[k].z,
                   sp[k].score, sp[k].rank, sp[k].loss, sp[k].nvalid, type == 9 ? (good ? "GOOD" : "FALSE") : "FALSE");
        }
        fflush(stdout);
    }
    free(x);
    return 0;
}
