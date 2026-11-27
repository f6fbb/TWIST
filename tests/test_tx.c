/*
 * test_tx.c - Checks the transmit audio (twl_tx_*, twcore.h) through
 * the shared libraries, without a radio: the frames encoded by their
 * protocol's library (twt_frame_step, twcl_frame_step, pi4l_frame_step),
 * the rendered audio decoded back by the same libraries, its chip
 * timing checked sample by sample, its keying free of clicks.
 *
 * usage: test_tx [-v]        exit status 0 = passed
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

/* A frame step from its protocol's library; *ok = 0 if it cannot be
 * encoded (the step is then empty: kind 0) */
static twl_step_t frame_ok(int proto, const char *call, const char *loc, int *ok)
{
    twl_step_t s;
    int rc = proto == TWL_PROTO_CLASSIC ? twcl_frame_step(call, &s) :
             proto == TWL_PROTO_PI4 ? pi4l_frame_step(call, loc, &s) :
             twt_frame_step(proto, call, loc, &s);
    if (ok) *ok = rc == 0;
    if (rc != 0) memset(&s, 0, sizeof s);
    return s;
}
static twl_step_t frame(int proto, const char *call, const char *loc) { return frame_ok(proto, call, loc, NULL); }



static twl_step_t timed(int kind, double sec, int hold)
{
    twl_step_t s;
    memset(&s, 0, sizeof s);
    s.kind = kind;
    s.seconds = sec;
    s.ptt_hold = hold;
    return s;
}

/* Renders a whole sequence in blocks of `block` samples; returns the
 * sample count, *out allocated. */
static long render_all(twl_tx *tx, int block, float **out)
{
    long cap = 1 << 20, n = 0;
    float *x = malloc(sizeof(float) * (size_t)cap);
    int m;
    while (x) {
        if (n + block > cap) { cap *= 2; x = realloc(x, sizeof(float) * (size_t)cap); if (!x) break; }
        m = twl_tx_render(tx, x + n, block);
        n += m;
        if (m < block) break;
    }
    *out = x;
    return x ? n : -1;
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    twl_tx *tx = twl_tx_create();
    twt_decoder *dt = twt_create();
    twcl_decoder *dc = twcl_create();
    pi4l_decoder *dp = pi4l_create();
    twl_tx_settings_t st;
    twl_tx_status_t ss;
    twl_step_t seq[4];
    twl_settings_t ds;
    twl_spot_t sp[16];
    float *x = NULL, blk[1000];
    double tc = twl_chip_seconds(), maxd, lim;
    long n, i, expect;
    int k, ns, ok, ptt_in_pause;

    if (!tx || !dt || !dc || !dp) { puts("FAIL  out of memory"); return 1; }
    check(twl_api_version() == TWL_CORE_API_VERSION, "API version matches the header");

    /* 1. a beacon sequence at 12 kHz: TWIST L, carrier 10 s, pause 5 s
     *    with the PTT released, classic frame - rendered in odd-sized blocks,
     *    noise added, decoded back */
    twl_tx_settings_default(&st);
    st.fs = 12000;
    seq[0] = frame(TWL_PROTO_TWL, "F6FBB", "JN12AX");
    seq[1] = timed(TWL_STEP_CARRIER, 10.0, 0);
    seq[2] = timed(TWL_STEP_PAUSE, 5.0, 0);
    seq[3] = frame(TWL_PROTO_CLASSIC, "F6FBB", NULL);
    check(twl_tx_start(tx, &st, seq, 4) == 0, "sequence accepted");
    twl_tx_status(tx, &ss);
    expect = lround((321 * tc + 10.0 + 5.0 + 239 * tc) * st.fs) + lround(st.ramp_s * st.fs);
    check(fabs(ss.total - (double)expect / st.fs) < 1e-9, "total duration: frames + carrier + pause + final ramp");
    ptt_in_pause = -1;
    n = 0;
    x = malloc(sizeof(float) * (size_t)(expect + 1000));
    while (x && (k = twl_tx_render(tx, x + n, 997)) > 0) {
        n += k;
        twl_tx_status(tx, &ss);
        if (ss.kind == TWL_STEP_PAUSE && !ss.done && ptt_in_pause != 1) ptt_in_pause = ss.ptt;
    }
    check(n == expect, "sample count exact");
    check(ptt_in_pause == 0, "PTT released during a pause without hold");
    twl_tx_status(tx, &ss);
    check(ss.done && !ss.ptt && twl_tx_render(tx, blk, 1000) == 0, "finished: done, PTT off, no more samples");
    {
        float *y = malloc(sizeof(float) * (size_t)(n + 2 * st.fs));
        double sigma;
        /* 2 s of noise before; SNR -10 dB in 2500 Hz while keyed on */
        synth_seed(7);
        /* carrier power A^2/2 over the noise power in 2500 Hz, sigma^2 x 2500 / (fs/2) */
        sigma = sqrt(st.level * st.level / 2.0 / (pow(10.0, -10.0 / 10.0) * 2500.0 / (st.fs / 2.0)));
        for (i = 0; i < n + 2 * st.fs; i++) y[i] = (float)(sigma * synth_gauss());
        for (i = 0; i < n; i++) y[i + 2 * st.fs] += x[i];
        twt_settings_default(&ds);
        ns = twt_decode(dt, y, (int)(n + 2 * st.fs), st.fs, &ds, sp, 16);
        ns += twcl_decode(dc, y, (int)(n + 2 * st.fs), st.fs, &ds, sp + ns, 16 - ns);
        if (verbose)
            for (k = 0; k < ns; k++)
                printf("      %d %-6s %-6s start %6.3f %7.2f Hz %4.0f dB\n", sp[k].proto, sp[k].call, sp[k].loc, sp[k].start, sp[k].freq, sp[k].snr);
        ok = ns == 2 && sp[0].proto == TWL_PROTO_TWL && !strcmp(sp[0].call, "F6FBB") && !strcmp(sp[0].loc, "JN12AX") &&
             sp[1].proto == TWL_PROTO_CLASSIC && !strcmp(sp[1].call, "F6FBB");
        check(ok, "decoded back: TWIST L F6FBB JN12AX, then classic F6FBB (in noise)");
        check(ok && fabs(sp[0].start - 2.0) < 0.02 && fabs(sp[1].start - (2.0 + 321 * tc + 15.0)) < 0.02 &&
              fabs(sp[0].freq - 1500.0) < 1.0, "decoded times and tone frequency as sent");
        free(y);
    }
    free(x);

    /* 2. chip timing at 44.1 kHz (a chip = 5644.8 samples): render up to
     *    each exact boundary and ask which chip is on the air */
    st.fs = 44100;
    seq[0] = frame(TWL_PROTO_TWS, "F1HDI", NULL);
    twl_tx_start(tx, &st, seq, 1);
    ok = 1;
    for (k = 0, n = 0; k < 239 && ok; k++) {
        long b = lround((k + 1) * tc * st.fs);      /* start of chip k+1 */
        twl_tx_status(tx, &ss);
        ok = ss.chip == k && ss.nchips == 239;
        while (n < b) { int m = (int)(b - n > 1000 ? 1000 : b - n); twl_tx_render(tx, blk, m); n += m; }
    }
    check(ok, "44.1 kHz: every chip starts at round(k x 0.128 s x fs), no drift");

    /* 3. no clicks: the sample-to-sample step never exceeds what the
     *    tone and a 5 ms raised cosine allow; hard keying does */
    st.fs = 48000;
    st.tone_hz = 1234.5;   /* not 1500: at 48 kHz a 1500 Hz tone is at phase 0
                              on every chip boundary, which hides hard keying */
    seq[0] = frame(TWL_PROTO_TWL, "F6FBB", "JN12AX");
    twl_tx_start(tx, &st, seq, 1);
    n = render_all(tx, 4096, &x);
    for (maxd = 0, i = 1; i < n; i++) if (fabs(x[i] - x[i - 1]) > maxd) maxd = fabs(x[i] - x[i - 1]);
    lim = st.level * (2 * M_PI * st.tone_hz / st.fs + M_PI / (2 * st.ramp_s * st.fs)) * 1.01;
    check(maxd <= lim, "5 ms ramps: no step larger than tone + ramp slope");
    free(x);
    st.ramp_s = 0.0;   /* hard keying, for comparison: must be detected */
    twl_tx_start(tx, &st, seq, 1);
    n = render_all(tx, 4096, &x);
    for (maxd = 0, i = 1; i < n; i++) if (fabs(x[i] - x[i - 1]) > maxd) maxd = fabs(x[i] - x[i - 1]);
    if (verbose) printf("      hard keying: n=%ld, max step %.3f (limit %.3f)\n", n, maxd, lim);
    check(maxd > 2 * lim, "(the check does see hard keying)");
    free(x);
    st.ramp_s = 0.005;
    st.tone_hz = 1500.0;

    /* 4. immediate stop in the middle of a carrier: fades out in 5 ms */
    seq[0] = timed(TWL_STEP_CARRIER, 30.0, 0);
    twl_tx_start(tx, &st, seq, 1);
    for (k = 0; k < 48; k++) twl_tx_render(tx, blk, 1000);     /* ~1 s */
    twl_tx_status(tx, &ss);
    check(ss.ptt && ss.kind == TWL_STEP_CARRIER && fabs(ss.elapsed - 1.0) < 0.001, "carrier on the air, PTT on");
    twl_tx_stop(tx);
    k = twl_tx_render(tx, blk, 1000);
    twl_tx_status(tx, &ss);
    check(k == 240 && fabs(blk[k - 1]) < 0.01 && ss.done && !ss.ptt, "stop: 5 ms fade-out, then done and PTT off");

    /* 5. level changed while a carrier plays (Tune): reaches the new
     *    level within 50 ms, no sample-to-sample jump beyond the tone's */
    seq[0] = timed(TWL_STEP_CARRIER, 5.0, 0);
    st.level = 0.5;
    twl_tx_start(tx, &st, seq, 1);
    for (k = 0; k < 48; k++) twl_tx_render(tx, blk, 1000);
    check(twl_tx_set_level(tx, 0.2) == 0, "level change accepted while playing");
    check(twl_tx_set_level(tx, 1.5) == -1 && twl_tx_set_level(tx, 0.0) == -1, "level out of range refused");
    {
        float y[4800], peak = 0.0f, prev = 0.0f, jump = 0.0f;
        int j;
        twl_tx_render(tx, y, 4800);                       /* 100 ms after the change */
        for (j = 0; j < 4800; j++) {
            float dj = fabsf(y[j] - prev);
            if (j > 0 && dj > jump) jump = dj;
            prev = y[j];
            if (j >= 2400 && fabsf(y[j]) > peak) peak = fabsf(y[j]);
        }
        check(fabsf(peak - 0.2f) < 0.01f, "new level reached (0.2)");
        /* a 1500 Hz tone at 48 kHz moves at most 2*pi*1500/48000*0.5 = 0.098 per sample */
        check(jump < 0.105f, "no click while the level moves");
    }
    twl_tx_stop(tx);
    twl_tx_render(tx, blk, 1000);

    /* 6. errors */
    seq[0] = frame_ok(TWL_PROTO_TWL, "F6FBB", NULL, &ok);
    check(!ok && twl_tx_start(tx, &st, seq, 1) == -1 && twl_tx_last_error(tx)[0] != '\0',
          "refused: TWIST L without locator, with a message");
    seq[0] = frame(TWL_PROTO_TWS, "F6FBB", NULL);
    seq[0].nsym = 0;
    check(twl_tx_start(tx, &st, seq, 1) == -1 && strstr(twl_tx_last_error(tx), "symbols") != NULL,
          "refused: a frame step without symbols");
    seq[0] = timed(TWL_STEP_PAUSE, 0.0, 0);
    check(twl_tx_start(tx, &st, seq, 1) == -1, "refused: zero-length pause");
    twl_tx_status(tx, &ss);
    check(ss.done && !ss.ptt, "after a refused start: nothing to send");

    /* 7. PI4 and CW (API 7): durations, continuous phase, decoded back */
    {
        twl_step_t cw;
        memset(&cw, 0, sizeof cw);
        cw.kind = TWL_STEP_CW;
        snprintf(cw.text, sizeof cw.text, "EE");
        check(fabs(twl_tx_step_seconds(&cw) - 0.5) < 1e-9, "CW 12 wpm: EE = 5 dots = 0.5 s");
        snprintf(cw.text, sizeof cw.text, "E E");
        cw.wpm = 24;
        check(fabs(twl_tx_step_seconds(&cw) - 0.45) < 1e-9, "CW 24 wpm: \"E E\" = 9 dots = 0.45 s (word gap 7)");
        snprintf(cw.text, sizeof cw.text, "F5Z#");
        check(twl_tx_step_seconds(&cw) < 0, "CW: a character without Morse refused");
        cw.wpm = 50;
        snprintf(cw.text, sizeof cw.text, "F5ZZO");
        check(twl_tx_step_seconds(&cw) < 0, "CW: 50 wpm refused");
        seq[0] = frame(TWL_PROTO_PI4, "F5ZZO", NULL);
        check(fabs(twl_tx_step_seconds(&seq[0]) - 146.0 / 6.0) < 1e-9 && fabs(pi4l_frame_seconds() - 146.0 / 6.0) < 1e-9,
              "PI4 frame: 146 symbols of 1/6 s");
        seq[0] = frame(TWL_PROTO_PI4, "", "JN12AX");
        check(twl_tx_step_seconds(&seq[0]) > 24 && !strcmp(seq[0].text, "/JN12AX"), "PI4 frame: \"/JN12AX\" (locator, no callsign)");
        seq[0] = frame_ok(TWL_PROTO_PI4, "F5ZZO/P12", NULL, &ok);
        check(!ok && twl_tx_step_seconds(&seq[0]) < 0 && twl_tx_start(tx, &st, seq, 1) == -1,
              "PI4: 9 characters refused");
        frame_ok(TWL_PROTO_PI4, "f5zz-o", NULL, &ok);
        check(!ok, "PI4: a character out of the alphabet refused");
    }
    {
        /* pause, PI4 F5ZZO at 800 Hz, CW identification and carrier at
         * 800 Hz, PI4 /JN12AX one minute after the first; at 12 kHz */
        twl_step_t q[6];
        double sigma, fmax;
        float *y;
        int sym_ok = 1;
        memset(q, 0, sizeof q);
        st.fs = 12000;
        st.tone_hz = 1500.0;
        q[0] = timed(TWL_STEP_PAUSE, 1.0, 0);
        q[1] = frame(TWL_PROTO_PI4, "F5ZZO", NULL);
        q[1].tone_hz = 800.0;
        q[2].kind = TWL_STEP_CW;
        snprintf(q[2].text, sizeof q[2].text, "F5ZZO JN12AX");
        q[2].tone_hz = 800.0;
        q[3] = timed(TWL_STEP_CARRIER, 60.0 - 146.0 / 6.0 - twl_tx_step_seconds(&q[2]) - 0.5, 0);
        q[3].tone_hz = 800.0;
        q[4] = timed(TWL_STEP_PAUSE, 0.5, 0);
        q[5] = frame(TWL_PROTO_PI4, "", "JN12AX");
        q[5].tone_hz = 800.0;
        check(q[3].seconds > 5 && twl_tx_start(tx, &st, q, 6) == 0, "PI4 beacon minute accepted (PI4, CW, carrier)");
        /* status: symbol by symbol */
        for (k = 0, n = 0; k < 146 && sym_ok; k++) {
            long b = lround((1.0 + (k + 1) / 6.0) * st.fs);
            while (n < lround((1.0 + k / 6.0) * st.fs) + 1) { twl_tx_render(tx, blk, 1); n++; }
            twl_tx_status(tx, &ss);
            sym_ok = ss.step == 1 && ss.chip == k && ss.nchips == 146 && ss.ptt;
            while (n < b) { int m = (int)(b - n > 1000 ? 1000 : b - n); twl_tx_render(tx, blk, m); n += m; }
        }
        check(sym_ok, "PI4 status: symbol k on the air at 1 + k/6 s, of 146");
        twl_tx_start(tx, &st, q, 6);
        n = render_all(tx, 4096, &x);
        /* continuous phase: no step beyond the highest tone's slope */
        fmax = 800.0 - 117.1875 + 3 * 234.375;
        for (maxd = 0, i = 1 + st.fs; i < lround(st.fs * (1.0 + 146.0 / 6.0)); i++)
            if (fabs(x[i] - x[i - 1]) > maxd) maxd = fabs(x[i] - x[i - 1]);
        check(maxd <= st.level * 2 * M_PI * fmax / st.fs * 1.01, "PI4: continuous phase across the symbols");
        y = malloc(sizeof(float) * (size_t)(n + 2 * st.fs));
        synth_seed(11);
        sigma = sqrt(st.level * st.level / 2.0 / (pow(10.0, -15.0 / 10.0) * 2500.0 / (st.fs / 2.0)));
        for (i = 0; i < n + 2 * st.fs; i++) y[i] = (float)(sigma * synth_gauss());
        for (i = 0; i < n; i++) y[i] += x[i];
        pi4l_settings_default(&ds);
        ds.protocols = TWL_PROTO_PI4;
        ns = pi4l_decode(dp, y, (int)(n + 2 * st.fs), st.fs, &ds, sp, 16);
        if (verbose)
            for (k = 0; k < ns; k++)
                printf("      %d %-6s %-6s start %6.3f %7.2f Hz %4.0f dB\n", sp[k].proto, sp[k].call, sp[k].loc, sp[k].start, sp[k].freq, sp[k].snr);
        ok = ns == 2 && sp[0].proto == TWL_PROTO_PI4 && !strcmp(sp[0].call, "F5ZZO") &&
             sp[1].proto == TWL_PROTO_PI4 && !sp[1].call[0] && !strcmp(sp[1].loc, "JN12AX");
        check(ok, "decoded back (-15 dB): PI4 F5ZZO, then /JN12AX");
        check(ok && fabs(sp[0].start - 1.0) < 0.1 && fabs(sp[1].start - 61.0) < 0.1 &&
              fabs(sp[0].freq - 800.0) < 2.0 && fabs(sp[1].freq - 800.0) < 2.0,
              "PI4: decoded at 1 s and 61 s (one minute apart), at 800 Hz");
        free(y);
        free(x);
    }

    twl_tx_destroy(tx);
    twt_destroy(dt);
    twcl_destroy(dc);
    pi4l_destroy(dp);
    puts(fail ? "SOME TX TESTS FAILED" : "TX TESTS PASSED");
    return fail;
}
