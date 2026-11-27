/*
 * twcore.c - libtwcore (twcore.h): live audio (twaudio.c), transmission
 * (twtx.c), the keying detector (twscan.c) and the arbitration between
 * the spots of several libraries - no protocol.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twcore.h"
#include "twdefs.h"
#include "twtx.h"
#include "twaudio.h"
#include "twscan.h"

int twl_api_version(void) { return TWL_CORE_API_VERSION; }
const char *twl_version(void) { return TW_VERSION; }
double twl_chip_seconds(void) { return tw_tchip(0.5); }

int twl_keyed(const float *samples, int n, int fs, const twl_settings_t *s,
              double window_s, double z_min, twl_keyed_t *out, int max)
{
    double tc = twl_chip_seconds(), half, lo, hi;
    long m, i, start;
    double *x;
    tw_scanhit_t hits[64];
    int nh, k, nout = 0, win;
    if (!samples || !s || !out || max <= 0 || fs < 4000 || window_s < 3.0) return -1;
    /* the window plus one chip of margin, from the end of the block */
    m = (long)((window_s + 2 * tc) * fs);
    if (m > n) m = n;
    start = n - m;
    win = (int)(window_s / tc);
    if (m < (long)((win / 2 + 8) * tc * fs)) return 0;            /* too short yet */
    x = (double *)malloc(sizeof(double) * (size_t)m);
    if (!x) return -1;
    for (i = 0; i < m; i++) x[i] = samples[start + i];
    /* the statistics are robust over a band of at least 600 Hz: the
     * zone itself may be narrow */
    half = s->tol > 300.0 ? s->tol : 300.0;
    lo = s->f_center - half;
    hi = s->f_center + half;
    if (lo < 100.0) lo = 100.0;
    nh = tw_scan_w(x, NULL, m, fs, tc, lo, hi, z_min, 15.0, 64, hits, NULL, win, 0.85, 0);
    free(x);
    if (nh < 0) return -1;
    for (k = 0; k < nh && nout < max; k++) {
        if (fabs(hits[k].freq - s->f_center) > s->tol + 5.0) continue;
        out[nout].freq = hits[k].freq;
        out[nout].z = hits[k].z;
        nout++;
    }
    return nout;
}

/* Spots that cannot both be real (twcore.h). PI4, in its own zone and
 * minute, is left out, as it always was. */
int twl_arbitrate(twl_spot_t *spots, int n)
{
    const double fsep = 3.0 / tw_tchip(0.5);         /* 3 chip widths, Hz */
    unsigned char *drop;
    int a, b, m = 0;
    if (!spots || n <= 1) return n < 0 ? 0 : n;
    drop = (unsigned char *)calloc((size_t)n, 1);
    if (!drop) return n;
    /* the same callsign in overlapping frames: the first one is kept */
    for (b = 0; b < n; b++) {
        if (spots[b].proto == TWL_PROTO_PI4) continue;
        double db = spots[b].end - spots[b].start;
        for (a = 0; a < b; a++) {
            if (drop[a] || spots[a].proto == TWL_PROTO_PI4 || strcmp(spots[a].call, spots[b].call)) continue;
            if (fabs(spots[a].start - spots[b].start) < 0.5 * db) { drop[b] = 1; break; }
        }
    }
    /* frames of different protocols at the same frequency, overlapping
     * by more than half of the shorter one: the larger contrast */
    for (a = 0; a < n; a++)
        for (b = a + 1; b < n; b++) {
            double da, db, ov;
            if (drop[a] || drop[b] || spots[a].proto == spots[b].proto) continue;
            if (spots[a].proto == TWL_PROTO_PI4 || spots[b].proto == TWL_PROTO_PI4) continue;
            if (fabs(spots[a].freq - spots[b].freq) >= fsep) continue;
            da = spots[a].end - spots[a].start; db = spots[b].end - spots[b].start;
            ov = fmin(spots[a].end, spots[b].end) - fmax(spots[a].start, spots[b].start);
            if (ov <= 0.5 * fmin(da, db)) continue;
            if (spots[a].contrast >= spots[b].contrast) drop[b] = 1; else drop[a] = 1;
        }
    for (a = 0; a < n; a++) if (!drop[a]) spots[m++] = spots[a];
    free(drop);
    return m;
}

/* ---------------- transmission (twtx.c) ---------------- */

void twl_tx_settings_default(twl_tx_settings_t *s)
{
    if (!s) return;
    s->fs = 48000;
    s->tone_hz = 1500.0;
    s->level = 0.5;
    s->ramp_s = 0.005;
}

twl_tx *twl_tx_create(void) { return (twl_tx *)twtx_create(); }
void twl_tx_destroy(twl_tx *t) { twtx_destroy((twtx_t *)t); }

int twl_tx_start(twl_tx *t, const twl_tx_settings_t *s, const twl_step_t *steps, int nsteps)
{
    return twtx_start((twtx_t *)t, s, steps, nsteps);
}

int twl_tx_render(twl_tx *t, float *out, int n) { return twtx_render((twtx_t *)t, out, n); }
void twl_tx_stop(twl_tx *t) { twtx_stop((twtx_t *)t); }
int twl_tx_set_level(twl_tx *t, double level) { return twtx_set_level((twtx_t *)t, level); }
void twl_tx_status(const twl_tx *t, twl_tx_status_t *st) { twtx_status((const twtx_t *)t, st); }
const char *twl_tx_last_error(const twl_tx *t) { return twtx_error((const twtx_t *)t); }
double twl_tx_step_seconds(const twl_step_t *st) { return twtx_step_seconds(st); }

/* ---------------- live audio (twaudio.c) ---------------- */

int twl_audio_available(void) { return twa_available(); }
int twl_audio_device_count(void) { return twa_device_count(); }
int twl_audio_device_info(int index, twl_audio_device_t *d) { return twa_device_info(index, d); }
twl_audio *twl_audio_create(void) { return (twl_audio *)twa_create(); }
void twl_audio_destroy(twl_audio *a) { twa_destroy((twa_t *)a); }
int twl_audio_start_input(twl_audio *a, int device, int rate, double keep_s)
{
    return twa_start_input((twa_t *)a, device, rate, keep_s);
}
int twl_audio_last(twl_audio *a, float *out, int n) { return twa_last((twa_t *)a, out, n); }
int twl_audio_push(twl_audio *a, const float *x, int frames, int channels)
{
    return twa_push((twa_t *)a, x, frames, channels);
}
void twl_audio_input_stats(const twl_audio *a, twl_audio_stats_t *st) { twa_input_stats((const twa_t *)a, st); }
void twl_audio_stop_input(twl_audio *a) { twa_stop_input((twa_t *)a); }
int twl_audio_start_output(twl_audio *a, int device, twl_tx *tx)
{
    return twa_start_output((twa_t *)a, device, (twtx_t *)tx);
}
int twl_audio_output_active(const twl_audio *a) { return twa_output_active((const twa_t *)a); }
int twl_audio_start_output_at(twl_audio *a, int device, twl_tx *tx, double utc)
{
    return twa_start_output_at((twa_t *)a, device, (twtx_t *)tx, utc);
}
double twl_audio_output_started(const twl_audio *a) { return twa_output_started((const twa_t *)a); }
double twl_audio_output_latency(const twl_audio *a) { return twa_output_latency((const twa_t *)a); }
void twl_audio_stop_output(twl_audio *a) { twa_stop_output((twa_t *)a); }
const char *twl_audio_last_error(const twl_audio *a) { return twa_error((const twa_t *)a); }
