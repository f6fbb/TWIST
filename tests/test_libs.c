/*
 * test_libs.c - Checks the libraries of TWIST Console the way the
 * desktop application uses them, linked against the SHARED libraries
 * (libtwcore, libtwist, libopera, libpi4), and measures the short
 * pass: each protocol library decodes its own frames, twl_arbitrate
 * merges them.
 *
 * A 45 s block of 12 kHz audio, as the continuous decoder sees it
 * every 3 s, holding four frames:
 *   classic F5ZZO           ends at 43.5 s   (in the last 3 s)
 *   TWIST S F6FBB           ends at 44.2 s   (in the last 3 s)
 *   TWIST L F1HDI JN18CS    ends at 44.0 s   (in the last 3 s)
 *   classic F4ABC           ends at 35.0 s   (older: already reported
 *                                             by an earlier pass)
 * *_decode_recent(lookback 3 s) must report the first three only,
 * *_decode over the whole block all four.
 *
 * usage: test_libs [-v]     exit status 0 = passed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "twcore.h"
#include "libtwist.h"
#include "twcl.h"
#include "libpi4.h"
#include "synth.h"

#define FS 12000
#define LEN 45.0

static int fail = 0;
static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fail = 1;
}

static void add(double *x, long n, int proto, const char *call, const char *loc,
                double t_end, double freq, double snr)
{
    unsigned char chips[TWL_MAX_CHIPS];
    int nch = proto == TWL_PROTO_CLASSIC ? twcl_encode(call, chips, TWL_MAX_CHIPS)
                                         : twt_encode(proto, call, loc, chips, TWL_MAX_CHIPS);
    double tc = twl_chip_seconds();
    if (nch <= 0) { printf("FAIL  cannot encode %s\n", call); fail = 1; return; }
    synth_add_frame(x, n, FS, chips, nch, tc, t_end - nch * tc, freq,
                    synth_amp_for_snr(snr, 0.1, FS), 0.4, 0.0, 0.005);
}

/* The libraries on one block, as the application: the classic frame,
 * TWIST, PI4, arbitrated */
static twcl_decoder *g_cl;
static twt_decoder  *g_tw;
static pi4l_decoder *g_p4;
static int decode_all(const float *x, int n, const twl_settings_t *s, double lookback, twl_spot_t *sp, int max)
{
    int ns = 0, k;
    k = lookback > 0 ? twcl_decode_recent(g_cl, x, n, FS, s, lookback, sp, max)
                     : twcl_decode(g_cl, x, n, FS, s, sp, max);
    if (k < 0) return -1;
    ns += k;
    k = lookback > 0 ? twt_decode_recent(g_tw, x, n, FS, s, lookback, sp + ns, max - ns)
                     : twt_decode(g_tw, x, n, FS, s, sp + ns, max - ns);
    if (k < 0) return -1;
    ns += k;
    k = lookback > 0 ? pi4l_decode_recent(g_p4, x, n, FS, s, lookback, sp + ns, max - ns)
                     : pi4l_decode(g_p4, x, n, FS, s, sp + ns, max - ns);
    if (k < 0) return -1;
    ns += k;
    return twl_arbitrate(sp, ns);
}

static int has(const twl_spot_t *s, int n, int proto, const char *call, const char *loc)
{
    int i;
    for (i = 0; i < n; i++)
        if (s[i].proto == proto && !strcmp(s[i].call, call) && (!loc || !strcmp(s[i].loc, loc)))
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    long n = (long)(LEN * FS), i;
    double *x = calloc((size_t)n, sizeof(double));
    float *xf = malloc(sizeof(float) * (size_t)n);
    twl_settings_t st;
    twl_spot_t sp[32];
    unsigned char chips[TWL_MAX_CHIPS];
    char hex[100];
    int ns, k, nch;
    clock_t c0, c1;
    double t_pass;

    g_cl = twcl_create(); g_tw = twt_create(); g_p4 = pi4l_create();
    if (!x || !xf || !g_cl || !g_tw || !g_p4) { printf("FAIL  out of memory\n"); return 1; }
    check(twl_api_version() == TWL_CORE_API_VERSION && twt_api_version() == TWT_API_VERSION &&
          twcl_api_version() == TWCL_API_VERSION && pi4l_api_version() == PI4L_API_VERSION,
          "API versions match the headers");
    check(!strcmp(twl_version(), twt_version()) && !strcmp(twl_version(), twcl_version()) &&
          !strcmp(twl_version(), pi4l_version()), "one version for the four libraries");
    printf("      version %s, chip %.3f s\n", twl_version(), twl_chip_seconds());
    check(!strcmp(twt_proto_name(TWL_PROTO_TWS), "TWIST S") && !strcmp(twt_proto_name(TWL_PROTO_TWL), "TWIST L") &&
          twt_proto_name(TWL_PROTO_CLASSIC)[0] == '\0' && twcl_proto_name(TWL_PROTO_CLASSIC)[0] != '\0' &&
          twcl_proto_name(TWL_PROTO_TWS)[0] == '\0' && !strcmp(pi4l_proto_name(TWL_PROTO_PI4), "PI4"),
          "each library names its own protocols only");

    /* encoder: same chips as twistenc (F6FBB JN12AX, hex printed by twistenc) */
    nch = twt_encode(TWL_PROTO_TWL, "F6FBB", "JN12AX", chips, TWL_MAX_CHIPS);
    for (k = 0; k < 2 * ((nch + 7) / 8); k++) {   /* bytes, MSB first, as twistenc */
        int v = 0, b;
        for (b = 0; b < 4; b++) v = 2 * v + (4 * k + b < nch ? chips[4 * k + b] : 0);
        hex[k] = "0123456789ABCDEF"[v];
    }
    hex[k] = '\0';
    check(nch == 321 && !strcmp(hex, "CCAAACCAD2D5354D2AB334ACCB53333354CAD2CD2AAD52CD4CB4B2ACAAB4AD2AB5333554CD2B4D5280"),
          "twt_encode TWIST L = twistenc");
    check(twcl_encode("F6FBB", chips, TWL_MAX_CHIPS) == 239 && twt_frame_chips(TWL_PROTO_TWS) == 239 &&
          twt_frame_chips(TWL_PROTO_TWL) == 321 && twt_frame_chips(TWL_PROTO_CLASSIC) == 0,
          "frame lengths 239 / 239 / 321 chips");
    check(twt_encode(TWL_PROTO_TWL, "F6FBB", NULL, chips, TWL_MAX_CHIPS) == -1 &&
          twcl_encode("F6FBB", chips, 100) == -1,
          "encoders refuse a missing locator or a short buffer");

    synth_seed(2026);
    add(x, n, TWL_PROTO_CLASSIC, "F5ZZO", NULL, 43.5, 1420.0, -16);
    add(x, n, TWL_PROTO_TWS, "F6FBB", NULL, 44.2, 1580.0, -16);
    add(x, n, TWL_PROTO_TWL, "F1HDI", "JN18CS", 44.0, 1500.0, -16);
    add(x, n, TWL_PROTO_CLASSIC, "F4ABC", NULL, 35.0, 1650.0, -16);
    synth_add_noise(x, n, 0.1);
    for (i = 0; i < n; i++) xf[i] = (float)x[i];

    twt_settings_default(&st);
    check(twcl_set_known(g_cl, "F5ZZO, F1HDI;F6FBB") == 3 && twt_set_known(g_tw, "F5ZZO, F1HDI;F6FBB") == 3,
          "set_known: 3 callsigns");

    /* the short pass of the continuous decoder */
    c0 = clock();
    ns = decode_all(xf, (int)n, &st, 3.0, sp, 32);
    c1 = clock();
    t_pass = (double)(c1 - c0) / CLOCKS_PER_SEC;
    if (verbose || ns != 3)
        for (k = 0; k < ns; k++)
            printf("      %d %-6s %-6s start %5.2f end %5.2f %7.1f Hz %4.0f dB%s\n", sp[k].proto,
                   sp[k].call, sp[k].loc, sp[k].start, sp[k].end, sp[k].freq, sp[k].snr, sp[k].deep ? " deep" : "");
    check(ns == 3 && has(sp, ns, TWL_PROTO_CLASSIC, "F5ZZO", NULL) && has(sp, ns, TWL_PROTO_TWS, "F6FBB", NULL) &&
          has(sp, ns, TWL_PROTO_TWL, "F1HDI", "JN18CS"),
          "short pass (last 3 s): classic + TWIST S + TWIST L, not the older frame");
    for (k = 0; k < ns; k++)
        if (sp[k].end < LEN - 3.0 - 0.2 || sp[k].end > LEN + 0.2) break;
    check(ns > 0 && k == ns, "short pass: every frame ends in the last 3 s");
    printf("      short pass: %.3f s CPU (target < 0.5 s)\n", t_pass);
    check(t_pass < 3.0, "short pass time reasonable");

    /* the whole block */
    ns = decode_all(xf, (int)n, &st, 0, sp, 32);
    check(ns == 4 && has(sp, ns, TWL_PROTO_CLASSIC, "F4ABC", NULL), "whole block: the four frames");

    /* each library alone: only its own frames */
    ns = twt_decode(g_tw, xf, (int)n, FS, &st, sp, 32);
    check(ns == 2 && has(sp, ns, TWL_PROTO_TWS, "F6FBB", NULL) && has(sp, ns, TWL_PROTO_TWL, "F1HDI", "JN18CS"),
          "libtwist alone: TWIST S + TWIST L");
    ns = twcl_decode(g_cl, xf, (int)n, FS, &st, sp, 32);
    check(ns == 2 && has(sp, ns, TWL_PROTO_CLASSIC, "F5ZZO", NULL) && has(sp, ns, TWL_PROTO_CLASSIC, "F4ABC", NULL),
          "libopera alone: the two classic frames");
    for (k = 0; k < ns; k++) if (!(sp[k].contrast > 1.0)) break;
    check(ns > 0 && k == ns, "spots carry their contrast");
    st.protocols = TWL_PROTO_CLASSIC;
    check(twt_decode(g_tw, xf, (int)n, FS, &st, sp, 32) == 0, "libtwist: no frame of another protocol asked, none given");
    twt_settings_default(&st);

    /* arbitration between libraries (twl_arbitrate) */
    {
        twl_spot_t a[4];
        memset(a, 0, sizeof a);
        /* a classic and a TWIST S frame at the same place: the larger contrast stays */
        a[0].proto = TWL_PROTO_CLASSIC; strcpy(a[0].call, "F5AAA"); a[0].start = 1; a[0].end = 31.6; a[0].freq = 1500; a[0].contrast = 3;
        a[1].proto = TWL_PROTO_TWS; strcpy(a[1].call, "F5BBB"); a[1].start = 2; a[1].end = 32.6; a[1].freq = 1505; a[1].contrast = 9;
        /* the same callsign twice (rain scatter: another frequency): the first stays */
        a[2].proto = TWL_PROTO_TWS; strcpy(a[2].call, "F5AAA"); a[2].start = 1.5; a[2].end = 32.1; a[2].freq = 1700; a[2].contrast = 5;
        /* PI4 at the same place: never arbitrated */
        a[3].proto = TWL_PROTO_PI4; strcpy(a[3].call, "F5BBB"); a[3].start = 2; a[3].end = 26.3; a[3].freq = 1505;
        k = twl_arbitrate(a, 4);
        check(k == 2 && a[0].proto == TWL_PROTO_TWS && !strcmp(a[0].call, "F5BBB") && a[1].proto == TWL_PROTO_PI4,
              "twl_arbitrate: larger contrast kept, same callsign once, PI4 left alone");
    }

    /* frame in progress (twl_keyed): the last 8 s of a block whose
     * TWIST L started 10 s before its end; then noise alone; then a
     * steady carrier */
    {
        long m = 20L * FS;
        double *y = calloc((size_t)m, sizeof(double));
        float *yf = malloc(sizeof(float) * (size_t)m);
        twl_keyed_t kk[8];
        twl_settings_t ks;
        unsigned char ch[TWL_MAX_CHIPS];
        int nk, nc = twt_encode(TWL_PROTO_TWL, "F6FBB", "JN12AX", ch, TWL_MAX_CHIPS);
        twt_settings_default(&ks);
        synth_seed(77);
        synth_add_noise(y, m, 0.1);
        for (i = 0; i < m; i++) yf[i] = (float)y[i];
        nk = twl_keyed(yf, (int)m, FS, &ks, 8.0, 6.0, kk, 8);
        check(nk == 0, "in progress: nothing on noise alone (z >= 6)");
        for (i = 0; i < m; i++) y[i] += synth_amp_for_snr(-6.0, 0.1, FS) * cos(2 * 3.14159265358979 * 1400.0 * i / FS);
        for (i = 0; i < m; i++) yf[i] = (float)y[i];
        nk = twl_keyed(yf, (int)m, FS, &ks, 8.0, 6.0, kk, 8);
        check(nk == 0, "in progress: a steady carrier is not a keyed signal");
        synth_add_frame(y, m, FS, ch, nc, twl_chip_seconds(), 10.0, 1560.0,
                        synth_amp_for_snr(-14.0, 0.1, FS), 0.2, 0.0, 0.005);
        for (i = 0; i < m; i++) yf[i] = (float)y[i];
        nk = twl_keyed(yf, (int)m, FS, &ks, 8.0, 6.0, kk, 8);
        if (verbose) for (k = 0; k < nk; k++) printf("      keyed %.1f Hz z %.1f\n", kk[k].freq, kk[k].z);
        check(nk >= 1 && fabs(kk[0].freq - 1560.0) < 8.0, "in progress: TWIST L at -14 dB found 10 s after its start, at its frequency");
        free(y); free(yf);
    }

    /* errors */
    st.protocols = 0;
    check(twt_decode(g_tw, xf, (int)n, FS, &st, sp, 32) == -1 && twt_last_error(g_tw)[0] != '\0',
          "error reported (no protocol selected)");

    twcl_destroy(g_cl);
    twt_destroy(g_tw);
    pi4l_destroy(g_p4);
    free(x);
    free(xf);
    puts(fail ? "SOME LIBRARY TESTS FAILED" : "LIBRARY TESTS PASSED");
    return fail;
}
