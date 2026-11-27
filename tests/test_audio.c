/*
 * test_audio.c - The capture path of the live audio (twl_audio_*,
 * twcore.h), fed without a sound card (TWL_AUDIO_NONE + push), as
 * the sound card callback would: channel mixing, decimation filter,
 * ring buffer, levels; then a decoding pass on what it kept.
 *
 * usage: test_audio [-v]        exit status 0 = passed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twcore.h"
#include "libtwist.h"
#include "twcl.h"
#include "libpi4.h"
#include "synth.h"

static int fail = 0;
static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fail = 1;
}

/* RMS of the kept samples after pushing a stereo tone of amplitude amp
 * at f Hz for 2 s at rate fs (the first 0.5 s, filter start-up, skipped). */
static double tone_rms(int fs, double f, double amp, int *rate_out)
{
    twl_audio *a = twl_audio_create();
    twl_audio_stats_t st;
    int n = 2 * fs, i, m;
    float *x = malloc(sizeof(float) * 2 * (size_t)n), *y = malloc(sizeof(float) * (size_t)n);
    double s = 0.0;
    twl_audio_start_input(a, TWL_AUDIO_NONE, fs, 10.0);
    for (i = 0; i < n; i++) x[2 * i] = x[2 * i + 1] = (float)(amp * sin(2 * M_PI * f * i / fs));
    for (i = 0; i < n; i += 480) twl_audio_push(a, x + 2 * i, (n - i < 480 ? n - i : 480), 2);
    twl_audio_input_stats(a, &st);
    m = twl_audio_last(a, y, n);
    *rate_out = st.rate;
    for (i = m / 4; i < m; i++) s += (double)y[i] * y[i];
    twl_audio_destroy(a);
    free(x); free(y);
    return sqrt(s / (m - m / 4));
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    twl_audio *a = twl_audio_create();
    twl_audio_stats_t st;
    twt_decoder *d = twt_create();
    twl_settings_t s;
    twl_spot_t sp[8];
    int rate, fs = 48000, i, k, n, ns, nch;
    double r1, r2, tc = twl_chip_seconds();
    unsigned char chips[TWL_MAX_CHIPS];
    float *x, *y, blk[1000];

    check(twl_api_version() == TWL_CORE_API_VERSION, "API version matches the header");
    printf("      live audio (PortAudio) built in: %s\n", twl_audio_available() ? "yes" : "no");

    /* decimation filter */
    r1 = tone_rms(48000, 1500.0, 0.5, &rate);
    check(rate == 12000 && fabs(20 * log10(r1 / (0.5 / sqrt(2.0)))) < 0.1, "48 kHz -> 12 kHz, 1500 Hz passes (within 0.1 dB)");
    r2 = tone_rms(48000, 7000.0, 0.5, &rate);
    if (verbose) printf("      7000 Hz at 48 kHz: %.1f dB\n", 20 * log10(r2 / (0.5 / sqrt(2.0))));
    check(20 * log10(r2 / (0.5 / sqrt(2.0))) < -60.0, "7000 Hz (alias of 5000 Hz) rejected by more than 60 dB");
    r1 = tone_rms(44100, 3000.0, 0.5, &rate);
    check(rate == 11025 && fabs(20 * log10(r1 / (0.5 / sqrt(2.0)))) < 0.5, "44.1 kHz -> 11025 Hz, 3000 Hz passes (within 0.5 dB)");
    r1 = tone_rms(12000, 1500.0, 0.5, &rate);
    check(rate == 12000 && fabs(20 * log10(r1 / (0.5 / sqrt(2.0)))) < 0.01, "12 kHz kept as is");

    /* ring buffer: keeps the newest samples, oldest first */
    twl_audio_start_input(a, TWL_AUDIO_NONE, 12000, 2.0);
    for (i = 0; i < 1000; i++) blk[i] = (float)i;
    for (k = 0; k < 30; k++) { for (i = 0; i < 1000; i++) blk[i] = (float)(k * 1000 + i); twl_audio_push(a, blk, 1000, 1); }
    y = malloc(sizeof(float) * 30000);
    n = twl_audio_last(a, y, 30000);
    check(n == 24000 && y[0] == 6000.0f && y[n - 1] == 29999.0f, "ring buffer: last 2 s, oldest first");
    n = twl_audio_last(a, y, 10);
    check(n == 10 && y[0] == 29990.0f, "ring buffer: last 10 samples");
    twl_audio_input_stats(a, &st);
    check(fabs(st.seconds - 2.5) < 1e-9 && st.overruns == 0, "stats: 2.5 s captured, no overrun");

    /* level meter: a 0.25 peak tone -> about -12 dBFS peak, -15 dBFS rms */
    twl_audio_start_input(a, TWL_AUDIO_NONE, 12000, 5.0);
    for (i = 0; i < 1000; i++) blk[i] = (float)(0.25 * sin(2 * M_PI * 1000.0 * i / 12000.0));
    for (k = 0; k < 12; k++) twl_audio_push(a, blk, 1000, 1);
    twl_audio_input_stats(a, &st);
    check(fabs(st.level_dbfs - 20 * log10(0.25)) < 0.2 && fabs(st.rms_dbfs - 20 * log10(0.25 / sqrt(2.0))) < 0.2,
          "level: peak and RMS dBFS of a known tone");
    free(y);

    /* a frame through the capture path at 48 kHz stereo, decoded */
    nch = twt_encode(TWL_PROTO_TWL, "F5ZZO", "JN12AX", chips, TWL_MAX_CHIPS);
    n = 45 * fs;
    {
        double *mono = calloc((size_t)n, sizeof(double));
        x = malloc(sizeof(float) * 2 * (size_t)n);
        synth_seed(3);
        synth_add_frame(mono, n, fs, chips, nch, tc, 2.0, 1450.0, synth_amp_for_snr(-16, 0.05, fs), 0.2, 0.0, 0.005);
        synth_add_noise(mono, n, 0.05);
        for (i = 0; i < n; i++) x[2 * i] = x[2 * i + 1] = (float)mono[i];
        free(mono);
    }
    twl_audio_start_input(a, TWL_AUDIO_NONE, fs, 60.0);
    for (i = 0; i < n; i += 1024) twl_audio_push(a, x + 2 * i, (n - i < 1024 ? n - i : 1024), 2);
    twl_audio_input_stats(a, &st);
    y = malloc(sizeof(float) * (size_t)(45 * st.rate));
    k = twl_audio_last(a, y, 45 * st.rate);
    twt_settings_default(&s);
    s.f_center = 1450.0;
    ns = twt_decode_recent(d, y, k, st.rate, &s, 3.0, sp, 8);
    if (verbose) for (i = 0; i < ns; i++) printf("      %s %s %s start %.2f %.1f Hz %.0f dB\n", twt_proto_name(sp[i].proto), sp[i].call, sp[i].loc, sp[i].start, sp[i].freq, sp[i].snr);
    check(ns == 1 && sp[0].proto == TWL_PROTO_TWL && !strcmp(sp[0].call, "F5ZZO") && fabs(sp[0].start - 2.0) < 0.05,
          "48 kHz stereo frame, through the capture path: decoded by the short pass");
    free(x); free(y);

    check(!strcmp(twt_proto_name(TWL_PROTO_TWS), "TWIST S") && !strcmp(twt_proto_name(TWL_PROTO_TWL), "TWIST L") &&
          twcl_proto_name(TWL_PROTO_CLASSIC)[0] != '\0' && !strcmp(pi4l_proto_name(TWL_PROTO_PI4), "PI4") &&
          twt_proto_name(99)[0] == '\0', "protocol labels");

    twt_destroy(d);
    twl_audio_destroy(a);
    puts(fail ? "SOME AUDIO TESTS FAILED" : "AUDIO TESTS PASSED");
    return fail;
}
