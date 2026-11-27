/* deep_shadow.c - the deep search next to a strong signal (testers,
 * 1.6.2: with F5ZAL and F5ZAS both in the list, a strong F5ZAL also gave
 * "F5ZAS" a few tens of Hz away - its weak spectral copy matched the
 * template of the near-identical callsign).
 *   1. a strong F5ZAL alone: F5ZAS must never be reported, whole block or
 *      short passes (frames ending in a 3.5 s window);
 *   2. a +10 dB F5ZAL plus a weak F5ZAS 120 Hz away (-21 dB, found by
 *      the deep search only): it must still be found.
 * Prints PASS/FAIL; exit status 0 if all pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twcenc.h"
#include "twcdeep.h"
#include "twpipe.h"
#include "synth.h"

static int has(const tw_spot_t *sp, int ns, const char *call)
{
    int k;
    for (k = 0; k < ns; k++) if (!strcmp(sp[k].call, call)) return 1;
    return 0;
}

int main(void)
{
    int fs = 12000, fail = 0, k, w, ghosts = 0, found = 0, runs = 0;
    double tchip = tw_tchip(0.5), len = twc_duration(0.5) + 8;
    long N = (long)(len * fs), i;
    double *x = malloc(sizeof(double) * (size_t)N), *y = malloc(sizeof(double) * (size_t)N);
    unsigned char a[TWC_NCHIPS], b[TWC_NCHIPS];
    twc_known_t kn[2];
    tw_params_t p; tw_spot_t sp[16]; int ns;

    twc_encode("F5ZAL", a, NULL);
    twc_encode("F5ZAS", b, NULL);
    twc_known_init("F5ZAL", &kn[0]);
    twc_known_init("F5ZAS", &kn[1]);
    (void)tchip;

    for (k = 0; k < 3; k++) {                            /* three noise draws */
        double strong = 10.0 + 5.0 * k;
        synth_seed(100 + k);
        memset(x, 0, sizeof(double) * (size_t)N);
        synth_add_classic(x, N, fs, a, tw_tchip(0.5), 3.0, 1500.0, synth_amp_for_snr(strong, 0.1, fs), 0.3, 0.0, 0.005);
        memcpy(y, x, sizeof(double) * (size_t)N);
        synth_add_classic(y, N, fs, b, tw_tchip(0.5), 3.5, 1620.0, synth_amp_for_snr(-21.0, 0.1, fs), 1.1, 0.0, 0.005);
        for (i = 0; i < N; i++) { double g = 0.1 * synth_gauss(); x[i] += g; y[i] += g; }

        /* 1. strong F5ZAL alone: whole block, then end windows every 0.5 s */
        for (w = -1; w < 12; w++) {
            tw_params_default(&p);
            p.known = kn; p.nknown = 2;
            if (w >= 0) { p.end_max = 30.0 + 0.5 * w; p.end_min = p.end_max - 3.5; }
            ns = tw_decode_real(x, N, fs, &p, sp, 16);
            ghosts += has(sp, ns, "F5ZAS");
            runs++;
        }
        /* 2. plus a weak F5ZAS 120 Hz away (next to the +10 dB F5ZAL only:
         * beside the +15 and +20 dB ones it is masked, before as now) */
        if (k > 0) continue;
        tw_params_default(&p);
        p.known = kn; p.nknown = 2;
        ns = tw_decode_real(y, N, fs, &p, sp, 16);
        found += has(sp, ns, "F5ZAS");
    }
    if (ghosts == 0) printf("PASS  strong F5ZAL, F5ZAS in the list: no ghost F5ZAS (%d passes)\n", runs);
    else { printf("FAIL  strong F5ZAL, F5ZAS in the list: ghost F5ZAS in %d of %d passes\n", ghosts, runs); fail = 1; }
    if (found == 1) printf("PASS  weak F5ZAS 120 Hz from a strong F5ZAL: found\n");
    else { printf("FAIL  weak F5ZAS 120 Hz from a strong F5ZAL: not found\n"); fail = 1; }
    free(x); free(y);
    return fail;
}
