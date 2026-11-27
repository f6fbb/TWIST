/*
 * gen_test_wav.c - Writes a synthetic classic recording as a 16-bit WAV.
 *
 * usage: gen_test_wav FRAME[,FRAME2...] MODE SNR_DB FREQ T0 LEN_S FS OUT.wav [iq] [drift_hz_s]
 *
 *   FRAME     CALL (classic), classic:CALL, TWS:CALL (TWIST short) or
 *             TWL:CALL:LOCATOR (TWIST long); comma-separated: the k-th
 *             is sent after the previous one + 2 s
 *   MODE      0.5, 1, 2, 4, 8, 16, 32
 *   SNR_DB    dB in 2500 Hz, carrier power while ON
 *   FREQ      carrier (Hz) - with "iq", relative to the I/Q center
 *   iq        writes a stereo I/Q file (left = I, right = Q) instead
 *             of real SSB audio
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
    double n, snr, freq, t0, len, drift = 0.0, sigma = 0.05, peak = 0.0, scale;
    int fs, iq = 0, ch;
    long N, i;
    double *x, *y = NULL;
    char calls[256], *tok;
    FILE *f;

    if (argc < 9) {
        fprintf(stderr, "usage: %s CALL[,CALL2..] MODE SNR_DB FREQ T0 LEN_S FS OUT.wav [iq] [drift_hz_s]\n", argv[0]);
        return 2;
    }
    n = atof(argv[2]); snr = atof(argv[3]); freq = atof(argv[4]);
    t0 = atof(argv[5]); len = atof(argv[6]); fs = atoi(argv[7]);
    if (argc > 9 && !strcmp(argv[9], "iq")) iq = 1;
    if (argc > 10) drift = atof(argv[10]);
    N = (long)(len * fs);
    x = calloc((size_t)N, sizeof(double));
    if (iq) y = calloc((size_t)N, sizeof(double));
    synth_seed(getenv("SEED") ? strtoull(getenv("SEED"), NULL, 10) : 2026);

    strncpy(calls, argv[1], sizeof calls - 1); calls[sizeof calls - 1] = 0;
    {
    double tk = t0;
    for (tok = strtok(calls, ","); tok; tok = strtok(NULL, ",")) {
        unsigned char chips[TW_MAXCHIPS];
        char proto[8] = "classic", call[16] = "", loc[16] = "";
        int nch = TWC_NCHIPS, rc;
        char *c1 = strchr(tok, ':');
        if (c1) {
            char *c2 = strchr(c1 + 1, ':');
            snprintf(proto, sizeof proto, "%.*s", (int)(c1 - tok), tok);
            if (c2) { snprintf(call, sizeof call, "%.*s", (int)(c2 - c1 - 1), c1 + 1); snprintf(loc, sizeof loc, "%s", c2 + 1); }
            else snprintf(call, sizeof call, "%s", c1 + 1);
        } else snprintf(call, sizeof call, "%s", tok);
        if (!strcmp(proto, "TWS")) { rc = tw_encode(TW_TYPE_S, call, NULL, chips); nch = 2 * tw_ncoded(TW_TYPE_S) + 1; }
        else if (!strcmp(proto, "TWL")) { rc = tw_encode(TW_TYPE_L, call, loc, chips); nch = 2 * tw_ncoded(TW_TYPE_L) + 1; }
        else rc = twc_encode(call, chips, NULL);
        if (rc != 0) { fprintf(stderr, "cannot encode %s\n", tok); return 1; }
        if (!iq) {
            synth_add_frame(x, N, fs, chips, nch, tw_tchip(n), tk, freq, synth_amp_for_snr(snr, sigma, fs), 0.7, drift, 0.005);
        } else {
            /* complex exponential: I = A cos, Q = A sin. For a complex
             * signal the same SNR convention gives |A|^2 = P, noise
             * sigma^2 per component -> amplitude from the real formula
             * divided by sqrt(2) (power A^2/2 per real formula equals
             * |A|^2 for the complex tone with half the noise density) */
            double a = synth_amp_for_snr(snr, sigma, fs) / sqrt(2.0);
            synth_add_frame(x, N, fs, chips, nch, tw_tchip(n), tk, freq, a, 0.7, drift, 0.005);
            synth_add_frame(y, N, fs, chips, nch, tw_tchip(n), tk, freq, a, 0.7 - M_PI / 2, drift, 0.005);
        }
        printf("%s at %.2f s, %.2f Hz, %.1f dB (%d chips)\n", tok, tk, freq, snr, nch);
        tk += nch * tw_tchip(n) + 2.0;
    }
    }
    synth_add_noise(x, N, sigma);
    if (iq) synth_add_noise(y, N, sigma);
    for (i = 0; i < N; i++) {
        if (fabs(x[i]) > peak) peak = fabs(x[i]);
        if (iq && fabs(y[i]) > peak) peak = fabs(y[i]);
    }
    scale = 30000.0 / (peak > 0 ? peak : 1);

    ch = iq ? 2 : 1;
    f = fopen(argv[8], "wb");
    if (!f) { perror(argv[8]); return 1; }
    fwrite("RIFF", 1, 4, f); wr32(f, (uint32_t)(36 + N * 2 * ch)); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16); wr16(f, 1); wr16(f, (uint16_t)ch);
    wr32(f, (uint32_t)fs); wr32(f, (uint32_t)(fs * 2 * ch)); wr16(f, (uint16_t)(2 * ch)); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, (uint32_t)(N * 2 * ch));
    for (i = 0; i < N; i++) {
        wr16(f, (uint16_t)(int16_t)lround(x[i] * scale));
        if (iq) wr16(f, (uint16_t)(int16_t)lround(y[i] * scale));
    }
    fclose(f);
    free(x); free(y);
    return 0;
}
