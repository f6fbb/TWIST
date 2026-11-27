/*
 * twtx.c - Transmit audio: frames, carriers and pauses rendered as a
 * keyed tone (twtx.h).
 *
 * The sequence becomes a list of segments of constant level and
 * frequency (one per symbol of a frame - an on/off chip, or a tone of
 * a frequency-shift frame such as PI4 - one
 * per Morse element or gap, one per carrier or pause), each with its
 * start sample. Rendering walks the list with the sample counter; the
 * envelope follows the level of the current segment, with a raised-
 * cosine ramp at the start of a segment whose level differs from the
 * previous one, and a final ramp down after the last segment. The
 * phase advances by the frequency of the current segment: a change of
 * frequency (a PI4 symbol, a step at its own frequency) keeps it
 * continuous.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twtx.h"
#include "twdefs.h"

#define TWTX_MAX_STEPS 64

typedef struct {
    long  start;         /* first sample                      */
    float level;         /* 0 or 1                            */
    float freq;          /* Hz                                */
    short step;          /* index of its step                 */
    short chip;          /* chip index in a frame, else -1    */
} seg_t;

struct twtx {
    twl_tx_settings_t s;
    twl_step_t steps[TWTX_MAX_STEPS];
    int    nsteps;
    int    nchips[TWTX_MAX_STEPS];
    seg_t *seg;
    int    nseg, cap;
    long   end;          /* first sample after the last segment */
    long   ramp;         /* ramp length, samples (>= 1)          */
    long   total;        /* end + ramp: the last ramp down       */
    long   pos;          /* next sample to render                */
    int    cur;          /* current segment                      */
    double phase;
    float  env;          /* envelope of the last sample rendered */
    volatile int stop_req;
    volatile float level_req;  /* level asked by twtx_set_level (any thread) */
    float  level_cur;    /* level applied, follows level_req smoothly */
    float  level_k;      /* smoothing step per sample (10 ms)         */
    long   stop_at;      /* sample where the fade-out started, -1 */
    float  stop_env;
    int    started, done;
    char   err[160];
};

static void set_err(twtx_t *t, const char *fmt, const char *a, const char *b)
{
    snprintf(t->err, sizeof t->err, fmt, a ? a : "", b ? b : "");
}

static long at(double sec, int fs) { return (long)floor(sec * fs + 0.5); }

static int add_seg(twtx_t *t, double sec, float level, double freq, int step, int chip)
{
    if (t->nseg == t->cap) {
        int cap = t->cap ? 2 * t->cap : 512;
        seg_t *g = (seg_t *)realloc(t->seg, sizeof(seg_t) * (size_t)cap);
        if (!g) return -1;
        t->seg = g;
        t->cap = cap;
    }
    t->seg[t->nseg].start = at(sec, t->s.fs);
    t->seg[t->nseg].level = level;
    t->seg[t->nseg].freq = (float)freq;
    t->seg[t->nseg].step = (short)step;
    t->seg[t->nseg].chip = (short)chip;
    t->nseg++;
    return 0;
}

/* ---- Morse ---- */

/* Morse of A-Z, 0-9, "/" ("." dot, "-" dash); NULL if none */
static const char *morse(char c)
{
    static const char *az[26] = { ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
        "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--",
        "-..-", "-.--", "--.." };
    static const char *d09[10] = { "-----", ".----", "..---", "...--", "....-", ".....", "-....",
        "--...", "---..", "----." };
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return az[c - 'A'];
    if (c >= '0' && c <= '9') return d09[c - '0'];
    if (c == '/') return "-..-.";
    return NULL;
}

/* The text of a CW step */
static void cw_text(const twl_step_t *st, char *out, size_t size)
{
    snprintf(out, size, "%.*s", (int)sizeof st->text, st->text);
}

static double cw_wpm(const twl_step_t *st) { return st->wpm == 0.0 ? 12.0 : st->wpm; }

/* Walks the Morse of a CW step: calls emit(on, t, units) for each
 * element (on = 1) and each gap between letters and words (on = 0),
 * in dot units; returns the length in units, or -1 (empty text, a
 * character without Morse). emit may be NULL. */
static int cw_walk(const twl_step_t *st, int (*emit)(void *, int, double, double), void *ctx)
{
    char text[40];
    int i, j, units = 0, first = 1, gap = 0;
    cw_text(st, text, sizeof text);
    for (i = 0; text[i]; i++) {
        const char *m;
        if (text[i] == ' ') { if (!first) gap = 7; continue; }
        if (!(m = morse(text[i]))) return -1;
        if (!first) {
            if (!gap) gap = 3;
            if (emit && emit(ctx, 0, units, gap) != 0) return -1;
            units += gap;
        }
        gap = 0;
        for (j = 0; m[j]; j++) {
            int len = m[j] == '-' ? 3 : 1;
            if (j > 0) {
                if (emit && emit(ctx, 0, units, 1) != 0) return -1;
                units += 1;
            }
            if (emit && emit(ctx, 1, units, len) != 0) return -1;
            units += len;
        }
        first = 0;
    }
    return first ? -1 : units;
}

/* A frame step that can be sent: 1, else 0 */
static int frame_ok(const twl_step_t *st)
{
    return st->nsym > 0 && st->nsym <= TWL_MAX_SYMBOLS && st->sym_s >= 0.001 && st->sym_s <= 60.0
        && st->spacing_hz >= 0.0;
}

double twtx_step_seconds(const twl_step_t *st)
{
    if (!st) return -1.0;
    switch (st->kind) {
    case TWL_STEP_FRAME:
        return frame_ok(st) ? st->nsym * st->sym_s : -1.0;
    case TWL_STEP_CARRIER:
    case TWL_STEP_PAUSE:
        return st->seconds >= 0.05 && st->seconds <= 3600.0 ? st->seconds : -1.0;
    case TWL_STEP_CW: {
        double wpm = cw_wpm(st);
        int u;
        if (!(wpm >= 5.0 && wpm <= 40.0)) return -1.0;
        u = cw_walk(st, NULL, NULL);
        return u > 0 ? u * 1.2 / wpm : -1.0;
    }
    default:
        return -1.0;
    }
}

typedef struct { twtx_t *t; double T, dot, freq; int step; } cw_ctx_t;

static int cw_emit(void *p, int on, double at_units, double units)
{
    cw_ctx_t *c = (cw_ctx_t *)p;
    (void)units;
    return add_seg(c->t, c->T + at_units * c->dot, on ? 1.0f : 0.0f, c->freq, c->step, -1);
}

twtx_t *twtx_create(void)
{
    twtx_t *t = (twtx_t *)calloc(1, sizeof(twtx_t));
    if (t) t->done = 1;
    return t;
}

void twtx_destroy(twtx_t *t)
{
    if (!t) return;
    free(t->seg);
    free(t);
}

int twtx_start(twtx_t *t, const twl_tx_settings_t *s, const twl_step_t *steps, int nsteps)
{
    double T = 0.0;
    int i, k;

    if (!t) return -1;
    t->err[0] = '\0';
    t->nseg = 0;
    t->started = 0;
    t->done = 1;
    if (!s || !steps || nsteps <= 0) { set_err(t, "empty sequence%s%s", 0, 0); return -1; }
    if (nsteps > TWTX_MAX_STEPS) { set_err(t, "too many steps (max 64)%s%s", 0, 0); return -1; }
    if (s->fs < 8000 || s->fs > 384000) { set_err(t, "sample rate out of range (8000-384000 Hz)%s%s", 0, 0); return -1; }
    if (!(s->tone_hz > 0.0 && s->tone_hz < 0.45 * s->fs)) { set_err(t, "tone frequency out of range%s%s", 0, 0); return -1; }
    if (!(s->level > 0.0 && s->level <= 1.0)) { set_err(t, "level out of range (0 < level <= 1)%s%s", 0, 0); return -1; }
    if (!(s->ramp_s >= 0.0 && s->ramp_s <= 0.05)) { set_err(t, "ramp out of range (0-50 ms)%s%s", 0, 0); return -1; }
    t->s = *s;

    for (i = 0; i < nsteps; i++) {
        const twl_step_t *st = &t->steps[i];
        double f;
        t->steps[i] = steps[i];
        t->steps[i].text[sizeof st->text - 1] = '\0';
        t->nchips[i] = 0;
        f = st->tone_hz == 0.0 ? s->tone_hz : st->tone_hz;
        if (!(f > 0.0 && f < 0.45 * s->fs)) { set_err(t, "tone frequency out of range%s%s", 0, 0); return -1; }
        if (st->kind == TWL_STEP_FRAME) {
            double lo = f + (st->spacing_hz > 0.0 ? st->offset_hz : 0.0);
            double hi = lo + (st->spacing_hz > 0.0 ? 255.0 * st->spacing_hz : 0.0);
            if (!frame_ok(st)) { set_err(t, "frame step without symbols%s%s", 0, 0); return -1; }
            if (st->spacing_hz > 0.0) {
                /* the highest symbol actually used */
                int top = 0;
                for (k = 0; k < st->nsym; k++) if (st->sym[k] > top) top = st->sym[k];
                hi = lo + top * st->spacing_hz;
            }
            if (!(lo > 0.0 && hi < 0.45 * s->fs)) { set_err(t, "frame tones out of range%s%s", 0, 0); return -1; }
            t->nchips[i] = st->nsym;
            for (k = 0; k < st->nsym; k++) {
                int r = st->spacing_hz > 0.0
                    ? add_seg(t, T + k * st->sym_s, 1.0f, lo + st->sym[k] * st->spacing_hz, i, k)
                    : add_seg(t, T + k * st->sym_s, st->sym[k] ? 1.0f : 0.0f, f, i, k);
                if (r != 0) goto oom;
            }
            T += st->nsym * st->sym_s;
        } else if (st->kind == TWL_STEP_CW) {
            double wpm = cw_wpm(st);
            cw_ctx_t c;
            int u;
            if (!(wpm >= 5.0 && wpm <= 40.0)) { set_err(t, "CW speed out of range (5-40 wpm)%s%s", 0, 0); return -1; }
            if (cw_walk(st, NULL, NULL) <= 0) {
                set_err(t, "cannot send in CW (A-Z 0-9 /): %s%s", st->text, 0);
                return -1;
            }
            c.t = t; c.T = T; c.dot = 1.2 / wpm; c.freq = f; c.step = i;
            if ((u = cw_walk(st, cw_emit, &c)) <= 0) goto oom;
            T += u * c.dot;
        } else if (st->kind == TWL_STEP_CARRIER || st->kind == TWL_STEP_PAUSE) {
            if (!(st->seconds >= 0.05 && st->seconds <= 3600.0)) {
                set_err(t, "carrier or pause duration out of range (0.05-3600 s)%s%s", 0, 0);
                return -1;
            }
            if (add_seg(t, T, st->kind == TWL_STEP_CARRIER ? 1.0f : 0.0f, f, i, -1) != 0) goto oom;
            T += st->seconds;
        } else {
            set_err(t, "unknown step kind%s%s", 0, 0);
            return -1;
        }
    }
    t->nsteps = nsteps;
    t->end = at(T, s->fs);
    t->ramp = at(s->ramp_s, s->fs);
    if (t->ramp < 1) t->ramp = 1;
    t->total = t->end + t->ramp;
    t->pos = 0;
    t->cur = 0;
    t->phase = 0.0;
    t->env = 0.0f;
    t->stop_req = 0;
    t->stop_at = -1;
    t->level_req = t->level_cur = (float)s->level;
    t->level_k = (float)(1.0 - exp(-1.0 / (0.010 * s->fs)));
    t->started = 1;
    t->done = 0;
    return 0;
oom:
    set_err(t, "out of memory%s%s", 0, 0);
    t->nseg = 0;
    return -1;
}

/* raised cosine 0 -> 1 over x in [0, 1] */
static double rc(double x) { return x >= 1.0 ? 1.0 : 0.5 * (1.0 - cos(M_PI * x)); }

int twtx_render(twtx_t *t, float *out, int n)
{
    int i, m = 0;
    double w = 2.0 * M_PI / (t ? t->s.fs : 1);
    if (!t || !out || n <= 0) return 0;
    for (i = 0; i < n; i++) {
        double e;
        long p = t->pos;
        if (t->done) { out[i] = 0.0f; continue; }
        if (t->stop_req && t->stop_at < 0) { t->stop_at = p; t->stop_env = t->env; }
        if (t->stop_at >= 0) {
            long q = p - t->stop_at;
            if (q >= t->ramp) { t->done = 1; out[i] = 0.0f; continue; }
            e = t->stop_env * (1.0 - rc((double)q / t->ramp));
        } else if (p >= t->total) {
            t->done = 1; out[i] = 0.0f; continue;
        } else if (p >= t->end) {
            /* after the last segment: ramp down from its level */
            float last = t->seg[t->nseg - 1].level;
            e = last * (1.0 - rc((double)(p - t->end) / t->ramp));
        } else {
            const seg_t *g;
            float prev;
            while (t->cur + 1 < t->nseg && p >= t->seg[t->cur + 1].start) t->cur++;
            g = &t->seg[t->cur];
            prev = t->cur > 0 ? t->seg[t->cur - 1].level : 0.0f;
            e = prev + (g->level - prev) * rc((double)(p - g->start) / t->ramp);
        }
        t->env = (float)e;
        t->level_cur += (t->level_req - t->level_cur) * t->level_k;   /* no step: no click */
        out[i] = (float)(t->level_cur * e * sin(t->phase));
        t->phase += w * t->seg[t->cur].freq;
        if (t->phase > 2.0 * M_PI) t->phase -= 2.0 * M_PI;
        t->pos++;
        m = i + 1;
    }
    return m;
}

void twtx_stop(twtx_t *t)
{
    if (t) t->stop_req = 1;
}

int twtx_set_level(twtx_t *t, double level)
{
    if (!t || !(level > 0.0 && level <= 1.0)) return -1;
    t->level_req = (float)level;
    t->s.level = level;
    return 0;
}

void twtx_status(const twtx_t *t, twl_tx_status_t *st)
{
    if (!st) return;
    memset(st, 0, sizeof *st);
    st->step = -1;
    st->chip = -1;
    if (!t || !t->started) { st->done = 1; return; }
    st->elapsed = (double)t->pos / t->s.fs;
    st->total = (double)t->total / t->s.fs;
    st->done = t->done;
    if (t->done) return;
    {
        /* segment of the NEXT sample to render: what goes on the air now */
        int c = t->cur;
        const seg_t *g;
        while (c + 1 < t->nseg && t->pos >= t->seg[c + 1].start) c++;
        g = &t->seg[c];
        const twl_step_t *s = &t->steps[g->step];
        int in_tail = t->pos >= t->end;
        st->step = g->step;
        st->kind = s->kind;
        st->chip = in_tail ? -1 : g->chip;
        st->nchips = t->nchips[g->step];
        /* PTT: on during frames, carriers, the final ramp and the
         * fade-out; during a pause only if the step holds it */
        st->ptt = s->kind != TWL_STEP_PAUSE || s->ptt_hold || in_tail || t->stop_at >= 0;
    }
}

const char *twtx_error(const twtx_t *t)
{
    return t ? t->err : "no transmitter";
}

int twtx_rate(const twtx_t *t)
{
    return t && t->started ? t->s.fs : 0;
}
