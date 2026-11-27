/*
 * snr_sweep.c - Monte-Carlo sensitivity of the full pipeline.
 * usage: snr_sweep mode_n snr_from snr_to snr_step trials [list_max] [drift_hz_s] [len_extra_s]
 * Each trial: a random callsign, random start (0.5..4 s), random
 * carrier (1500 +/- 150 Hz), random phase, 5 ms keying edges, white
 * Gaussian noise, decoded with the default parameters.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twcenc.h"
#include "twpipe.h"
#include "synth.h"
#include "twcdeep.h"

/* KNOWN="CALL1,CALL2" enables the deep search for those callsigns;
 * KNOWN_ONLY=1 restricts reports to them; DEEP_FA=x sets the target. */
static twc_known_t g_known[32];
static int g_nknown = 0;
static void setup_known(tw_params_t *p)
{
    const char *k = getenv("KNOWN");
    if (k && !g_nknown) {
        char buf[512], *tok;
        strncpy(buf, k, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        for (tok = strtok(buf, ","); tok && g_nknown < 32; tok = strtok(NULL, ","))
            if (twc_known_init(tok, &g_known[g_nknown]) == 0) g_nknown++;
    }
    p->known = g_known; p->nknown = g_nknown;
    p->known_only = getenv("KNOWN_ONLY") != NULL;
    if (getenv("DEEP_FA")) p->deep_fa = atof(getenv("DEEP_FA"));
    if (getenv("NODEEP")) p->deep = 0;
    if (getenv("SPREAD")) p->spread_hz = atof(getenv("SPREAD"));
}

static const char *calls[] = { "F6FBB","F1HDI","F5ZZO","OZ7IGY","EA5XYZ","G4JNT","DL0ABC","K1JT","9A1A","VK2ABC" };

int main(int argc, char **argv)
{
    double n = argc > 1 ? atof(argv[1]) : 0.5;
    double s0 = argc > 2 ? atof(argv[2]) : -14, s1 = argc > 3 ? atof(argv[3]) : -22, ds = argc > 4 ? atof(argv[4]) : -1;
    int trials = argc > 5 ? atoi(argv[5]) : 50;
    int list_max = argc > 6 ? atoi(argv[6]) : -1;
    double drift = argc > 7 ? atof(argv[7]) : 0.0;
    double extra = argc > 8 ? atof(argv[8]) : 6.0;
    int fs = 12000;
    double tchip = tw_tchip(n), len = twc_duration(n) + extra;
    long N = (long)(len * fs);
    double *x = malloc(sizeof(double) * N);
    double snr;
    synth_seed(getenv("SEED") ? strtoull(getenv("SEED"), NULL, 10) : 12345);
    printf("Op%g, %d trials per point, file %.1f s\n", n, trials, len);
    for (snr = s0; ds < 0 ? snr >= s1 - 1e-9 : snr <= s1 + 1e-9; snr += ds) {
        int ok = 0, bad = 0, t, ndeep = 0; double snr_sum = 0, snr_sq = 0;
        for (t = 0; t < trials; t++) {
            const char *c = calls[t % 10];
            unsigned char chips[TWC_NCHIPS];
            double t0 = 0.5 + 3.5 * (synth_gauss() * 0 + (double)((t * 7919) % 1000) / 1000.0);
            double f = 1500 + 150 * (2.0 * ((t * 104729) % 1000) / 1000.0 - 1.0);
            tw_params_t p; tw_spot_t sp[8]; int k, ns, got = 0;
            twc_encode(c, chips, NULL);
            memset(x, 0, sizeof(double) * N);
            synth_add_classic(x, N, fs, chips, tchip, t0, f, synth_amp_for_snr(snr, 0.1, fs),
                            fabs(synth_gauss()) * 3, drift, 0.005);
            synth_add_noise(x, N, 0.1);
            tw_params_default(&p); p.snr_avg = 0; /* compare with the injected carrier-on SNR */ p.mode_n = n; setup_known(&p);
            if (list_max > 0) p.list_max = list_max;
            if (getenv("NARROW")) {   /* beacon timing and frequency known */
                p.f_center = f + 2.0; p.tol = 8.0; p.dt_min = t0 - 0.5; p.dt_max = t0 + 0.5;
            }
            ns = tw_decode_real(x, N, fs, &p, sp, 8);
            for (k = 0; k < ns; k++) {
                char norm[7], trimmed[7]; int a = 0, b;
                tw_normalize_call(c, norm);
                for (b = 0; b < 6; b++) if (norm[b] != ' ') trimmed[a++] = norm[b];
                trimmed[a] = 0;
                if (!strcmp(sp[k].call, trimmed) && fabs(sp[k].dt - t0) < tchip / 2) {
                    ndeep += sp[k].deep && !got;
                    got = 1; snr_sum += sp[k].snr; snr_sq += sp[k].snr * sp[k].snr;
                    if (getenv("SHOWRANK")) printf("  deep=%d ", sp[k].deep), printf("  ok rank=%d loss=%.1f shift=%d z=%.1f score=%.1f\n", sp[k].rank, sp[k].loss, sp[k].shift, sp[k].z, sp[k].score);
                } else {
                    bad++;
                    printf("  FALSE %s (true %s) dt=%.2f (true %.2f) df=%.1f (true %.1f) rank=%d loss=%.1f shift=%d z=%.1f score=%.1f\n",
                           sp[k].call, c, sp[k].dt, t0, sp[k].df, f, sp[k].rank, sp[k].loss, sp[k].shift, sp[k].z, sp[k].score);
                }
            }
            ok += got;
        }
        printf("SNR %6.1f dB : %3d/%d decoded (%5.1f%%, %d by deep search), false %d, mean est. SNR %6.2f (sd %.2f)\n",
               snr, ok, trials, 100.0 * ok / trials, ndeep, bad, ok ? snr_sum / ok : 0,
               ok ? sqrt(fmax(0, snr_sq / ok - (snr_sum / ok) * (snr_sum / ok))) : 0);
        fflush(stdout);
    }
    free(x);
    return 0;
}
