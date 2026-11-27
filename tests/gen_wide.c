/*
 * gen_wide.c - Synthetic wideband I/Q capture (stereo 16-bit WAV,
 * left = I, right = Q) holding several classic beacons, plus optional
 * steady carriers (birdies) and Morse-like keyed carriers, to test
 * the wideband scan.
 *
 * usage: gen_wide FS LEN_S OUT.wav MODE SPEC [SPEC...]
 *   SPEC = CALL:FREQ:T0:SNR   classic beacon (FREQ relative to center, Hz;
 *                             SNR in 2500 Hz, carrier-on power)
 *        = TWS/CALL:FREQ:T0:SNR        TWIST short frame
 *        = TWL/CALL/LOC:FREQ:T0:SNR    TWIST long frame
 *        = CW:FREQ:SNR         steady carrier, whole file
 *        = KEY:FREQ:SNR        random on/off keying at 10 baud (CW-like)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "twcenc.h"
#include "twist.h"
#include "synth.h"

static void wr16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }
static void wr32(FILE *f, uint32_t v) { wr16(f, v & 0xffff); wr16(f, v >> 16); }

int main(int argc, char **argv)
{
    int fs, a;
    double len, mode, sigma = 0.05, peak = 0, scale;
    long N, i;
    double *x, *y;
    FILE *f;
    if (argc < 6) { fprintf(stderr, "usage: %s FS LEN OUT.wav MODE SPEC...\n", argv[0]); return 2; }
    fs = atoi(argv[1]); len = atof(argv[2]); mode = atof(argv[4]);
    N = (long)(len * fs);
    x = calloc(N, sizeof(double)); y = calloc(N, sizeof(double));
    synth_seed(getenv("SEED") ? strtoull(getenv("SEED"), NULL, 10) : 77);
    for (a = 5; a < argc; a++) {
        char buf[128], *t1, *t2, *t3, *t4;
        strncpy(buf, argv[a], 127); buf[127] = 0;
        t1 = strtok(buf, ":"); t2 = strtok(NULL, ":"); t3 = strtok(NULL, ":"); t4 = strtok(NULL, ":");
        if (!strcmp(t1, "CW") || !strcmp(t1, "KEY")) {
            double fr = atof(t2), snr = atof(t3);
            /* complex tone: |A|^2 = noise_per_component * 2 * snr_lin * 2500 / fs ... same
             * convention as gen_test_wav's I/Q branch */
            double amp = synth_amp_for_snr(snr, sigma, fs) / sqrt(2.0), ph = 0;
            int on = 1;
            for (i = 0; i < N; i++) {
                if (!strcmp(t1, "KEY") && i % (fs / 10) == 0) on = synth_gauss() > 0.3;
                if (on) { x[i] += amp * cos(ph); y[i] += amp * sin(ph); }
                ph += 2 * M_PI * fr / fs;
            }
            printf("%s at %.1f Hz, %.1f dB\n", t1, fr, snr);
        } else {
            unsigned char chips[TW_MAXCHIPS];
            double fr = atof(t2), t0 = atof(t3), snr = atof(t4);
            double amp = synth_amp_for_snr(snr, sigma, fs) / sqrt(2.0);
            int nch = TWC_NCHIPS, rc;
            if (!strncmp(t1, "TWS/", 4)) {
                rc = tw_encode(TW_TYPE_S, t1 + 4, NULL, chips); nch = 2 * tw_ncoded(TW_TYPE_S) + 1;
            } else if (!strncmp(t1, "TWL/", 4)) {
                char cs[16], *sl;
                snprintf(cs, sizeof cs, "%s", t1 + 4);
                sl = strchr(cs, '/');
                if (!sl) { fprintf(stderr, "TWL/CALL/LOC expected: %s\n", t1); return 1; }
                *sl = 0;
                rc = tw_encode(TW_TYPE_L, cs, sl + 1, chips); nch = 2 * tw_ncoded(TW_TYPE_L) + 1;
            } else rc = twc_encode(t1, chips, NULL);
            if (rc) { fprintf(stderr, "cannot encode %s\n", t1); return 1; }
            synth_add_frame(x, N, fs, chips, nch, tw_tchip(mode), t0, fr, amp, 0.3, 0.0, 0.005);
            synth_add_frame(y, N, fs, chips, nch, tw_tchip(mode), t0, fr, amp, 0.3 - M_PI / 2, 0.0, 0.005);
            printf("%s at %.1f Hz, t0 %.2f s, %.1f dB\n", t1, fr, t0, snr);
        }
    }
    synth_add_noise(x, N, sigma); synth_add_noise(y, N, sigma);
    for (i = 0; i < N; i++) { if (fabs(x[i]) > peak) peak = fabs(x[i]); if (fabs(y[i]) > peak) peak = fabs(y[i]); }
    scale = 30000.0 / peak;
    f = fopen(argv[3], "wb");
    fwrite("RIFF", 1, 4, f); wr32(f, (uint32_t)(36 + N * 4)); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16); wr16(f, 1); wr16(f, 2);
    wr32(f, (uint32_t)fs); wr32(f, (uint32_t)(fs * 4)); wr16(f, 4); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, (uint32_t)(N * 4));
    for (i = 0; i < N; i++) { wr16(f, (uint16_t)(int16_t)lround(x[i] * scale)); wr16(f, (uint16_t)(int16_t)lround(y[i] * scale)); }
    fclose(f);
    return 0;
}
