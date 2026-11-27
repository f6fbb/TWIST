/* snr_agc.c - the SNR reported for strong signals, with and without a
 * receiver AGC (testers: 10 to 15 dB below the OPERA program on strong
 * signals, 1 to 2 dB on weak ones). The AGC: a peak detector, fast
 * attack (5 ms), release 0.1 s (an HF receiver's "fast" AGC), the gain
 * lowered so that the carrier stays at a few times the noise - between
 * two ON chips it rises again and lifts the noise.
 * Prints "PASS/FAIL" lines; exit status 0 when all pass. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twcenc.h"
#include "twpipe.h"
#include "synth.h"

static double run(double snr, int agc, unsigned long seed)
{
    int fs = 12000, k, ns;
    double n = 0.5, t0 = 2.0, f = 1520.0, sigma = 0.1;
    double len = twc_duration(n) + 6;
    long N = (long)(len * fs), i;
    double *x = calloc((size_t)N, sizeof(double)), got = -99;
    unsigned char chips[TWC_NCHIPS];
    tw_params_t p; tw_spot_t sp[8];

    twc_encode("F5ZZO", chips, NULL);
    synth_seed(seed);
    synth_add_classic(x, N, fs, chips, tw_tchip(n), t0, f, synth_amp_for_snr(snr, sigma, fs), 0.3, 0.0, 0.005);
    synth_add_noise(x, N, sigma);
    if (agc) {
        double att = exp(-1.0 / (0.005 * fs)), rel = exp(-1.0 / (0.1 * fs)), e = 0, floor = 3 * sigma;
        for (i = 0; i < N; i++) {
            double a = fabs(x[i]);
            e = a > e ? a + att * (e - a) : a + rel * (e - a);
            if (e > floor) x[i] *= floor / e;
        }
    }
    tw_params_default(&p); p.snr_avg = 0; p.mode_n = n;
    ns = tw_decode_real(x, N, fs, &p, sp, 8);
    for (k = 0; k < ns; k++) if (!strcmp(sp[k].call, "F5ZZO")) { got = sp[k].snr; break; }
    free(x);
    return got;
}

int main(void)
{
    static const double snrs[] = { -15, 0, 10, 20 };
    int fail = 0, a, s;
    for (a = 0; a <= 1; a++)
        for (s = 0; s < 4; s++) {
            double got = run(snrs[s], a, 7 + s), tol = snrs[s] < -10 ? 2.0 : 1.5;
            int ok = fabs(got - snrs[s]) <= tol;
            printf("%s  SNR %+.0f dB%s: reported %+.1f dB\n", ok ? "PASS" : "FAIL", snrs[s], a ? ", fast AGC" : "", got);
            if (!ok) fail = 1;
        }
    return fail;
}
