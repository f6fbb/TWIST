/* test_e2e.c - one synthetic transmission, through the full pipeline.
 * usage: test_e2e [call] [snr_db] [mode_n] [freq] [t0] [len_s] [drift Hz/s] [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twcenc.h"
#include "twpipe.h"
#include "synth.h"
int main(int argc, char **argv)
{
    const char *call = argc > 1 ? argv[1] : "F6FBB";
    double snr = argc > 2 ? atof(argv[2]) : -15;
    double n = argc > 3 ? atof(argv[3]) : 0.5;
    double f = argc > 4 ? atof(argv[4]) : 1523.4;
    double t0 = argc > 5 ? atof(argv[5]) : 3.3;
    double len = argc > 6 ? atof(argv[6]) : twc_duration(n) + 8;
    double drift = argc > 7 ? atof(argv[7]) : 0;
    unsigned long seed = argc > 8 ? strtoul(argv[8], 0, 10) : 1;
    int fs = 12000, k, ns;
    long N = (long)(len * fs);
    double *x = calloc(N, sizeof(double));
    unsigned char chips[TWC_NCHIPS];
    tw_params_t p; tw_spot_t sp[16];
    if (twc_encode(call, chips, NULL)) { printf("bad call\n"); return 2; }
    synth_seed(seed);
    synth_add_classic(x, N, fs, chips, tw_tchip(n), t0, f, synth_amp_for_snr(snr, 0.1, fs), 0.3, drift, 0.005);
    synth_add_noise(x, N, 0.1);
    tw_params_default(&p); p.snr_avg = 0; /* compare with the injected carrier-on SNR */ p.mode_n = n; p.verbose = 1;
    ns = tw_decode_real(x, N, fs, &p, sp, 16);
    for (k = 0; k < ns; k++)
        printf("%3.0f %5.2f %7.2f : %s (true t0=%.2f f=%.2f snr=%.1f)\n", sp[k].snr, sp[k].dt, sp[k].df, sp[k].call, t0, f, snr);
    if (ns <= 0) printf("no decode\n");
    free(x);
    return 0;
}
