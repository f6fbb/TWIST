/*
 * spread_sweep.c - Decoding of frequency-spread ("rain scatter")
 * classic signals, with and without twistd's spread mode (-w).
 * usage: spread_sweep spread_rms_hz snr_from snr_to step trials w1 [w2 ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twcenc.h"
#include "twpipe.h"
#include "synth.h"

int main(int argc, char **argv)
{
    double sprd, s0, s1, ds, snr;
    int trials, nw, iw, fs = 12000;
    double W[16];
    long N;
    double *x, *clean;
    if (argc < 7) { fprintf(stderr, "usage: %s spread_rms snr_from snr_to step trials w1 [w2..]\n", argv[0]); return 2; }
    sprd = atof(argv[1]); s0 = atof(argv[2]); s1 = atof(argv[3]); ds = atof(argv[4]); trials = atoi(argv[5]);
    for (nw = 0; nw < argc - 6 && nw < 16; nw++) W[nw] = atof(argv[6 + nw]);
    N = (long)((twc_duration(0.5) + 6) * fs);
    x = malloc(sizeof(double) * N); clean = malloc(sizeof(double) * N);
    printf("Op05, Doppler spread %.1f Hz RMS, %d trials per point\n", sprd, trials);
    for (snr = s0; ds < 0 ? snr >= s1 - 1e-9 : snr <= s1 + 1e-9; snr += ds) {
        int ok[16] = {0}, bad[16] = {0}, t;
        synth_seed(4321);
        for (t = 0; t < trials; t++) {
            unsigned char chips[TWC_NCHIPS];
            double t0 = 1.0 + 3.0 * ((t * 7919) % 1000) / 1000.0, f = 1500 + 100 * (((t * 104729) % 1000) / 500.0 - 1);
            long k;
            twc_encode("ED5YAE", chips, NULL);
            memset(clean, 0, sizeof(double) * N);
            synth_add_classic_spread(clean, N, fs, chips, 0.128, t0, f, synth_amp_for_snr(snr, 0.1, fs), sprd);
            for (k = 0; k < N; k++) clean[k] += 0.1 * synth_gauss();
            for (iw = 0; iw < nw; iw++) {
                tw_params_t p; tw_spot_t sp[8]; int ns, j;
                memcpy(x, clean, sizeof(double) * N);
                tw_params_default(&p);
                p.spread_hz = W[iw];
                ns = tw_decode_real(x, N, fs, &p, sp, 8);
                for (j = 0; j < ns; j++) {
                    if (!strcmp(sp[j].call, "ED5YAE") && fabs(sp[j].dt - t0) < 0.2) ok[iw]++;
                    else bad[iw]++;
                }
            }
        }
        printf("SNR %6.1f dB :", snr);
        for (iw = 0; iw < nw; iw++) printf("  -w %-3g %3d%% (false %d)", W[iw], 100 * ok[iw] / trials, bad[iw]);
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
