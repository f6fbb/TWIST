/*
 * twdec.c - The decoder behind the C API of each protocol library
 * (twdec.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "twdec.h"
#include "twdefs.h"
#include "twpipe.h"
#include "twcall.h"
#if TW_WITH_CLASSIC
#include "twcdeep.h"
#endif
#if TW_WITH_TWIST
#include "twist.h"
#endif
#if TW_WITH_PI4
#include "pi4pipe.h"
#endif

#define TWDEC_MAX_KNOWN 256
#define TWDEC_MAX_RAW    64     /* spots kept from one pipeline call */

struct twdec {
    int         protos;
    char        calls[TWDEC_MAX_KNOWN][12];   /* trimmed, upper case */
    int         ncalls;
#if TW_WITH_CLASSIC
    twc_known_t known[TWDEC_MAX_KNOWN];      /* the deep search's    */
    int         nknown;
#endif
    double     *buf;          /* float -> double conversion */
    int         nbuf;
    char        err[160];
};

static void set_err(twdec_t *d, const char *msg)
{
    if (d) snprintf(d->err, sizeof d->err, "%s", msg);
}

void twdec_settings_default(twl_settings_t *s)
{
    tw_params_t p;
    if (!s) return;
    tw_params_default(&p);
    memset(s, 0, sizeof *s);
    s->protocols  = TWL_PROTO_ALL;             /* PI4 on request, as always */
    s->f_center   = p.f_center;
    s->tol        = p.tol;
    s->spread_hz  = 0.0;
    s->deep       = 1;
    s->known_only = 0;
    s->end_min    = p.end_min;
    s->end_max    = p.end_max;
    s->pi4_f_center = p.pi4_f_center;
    s->pi4_tol      = p.pi4_tol;
}

twdec_t *twdec_create(int protos)
{
    twdec_t *d = (twdec_t *)calloc(1, sizeof(twdec_t));
    if (d) d->protos = protos;
    return d;
}

void twdec_destroy(twdec_t *d)
{
    if (!d) return;
    free(d->buf);
    free(d);
}

int twdec_set_known(twdec_t *d, const char *calls)
{
    const char *q = calls;
    if (!d) return -1;
    d->ncalls = 0;
#if TW_WITH_CLASSIC
    d->nknown = 0;
#endif
    d->err[0] = '\0';
    if (!q) return 0;
    while (*q) {
        char tok[16];
        int n = 0, i;
        if (*q == ',' || *q == ';' || isspace((unsigned char)*q)) { q++; continue; }
        while (*q && *q != ',' && *q != ';' && !isspace((unsigned char)*q)) {
            if (n < 15) tok[n++] = *q;
            q++;
        }
        tok[n] = '\0';
        if (d->ncalls >= TWDEC_MAX_KNOWN) {
            set_err(d, "too many known callsigns (max 256)");
            return -1;
        }
        for (i = 0; tok[i]; i++) tok[i] = (char)toupper((unsigned char)tok[i]);
#if TW_WITH_CLASSIC
        /* the classic frame: only the callsigns it can carry (as
         * before: the list kept is the deep search's) */
        if (twc_known_init(tok, &d->known[d->nknown]) == 0) {
            memcpy(d->calls[d->ncalls], d->known[d->nknown].call, 7);
            d->calls[d->ncalls++][7] = '\0';
            d->nknown++;
        }
#else
        if (n <= 11) snprintf(d->calls[d->ncalls++], 12, "%s", tok);
#endif
    }
    return d->ncalls;
}

/* Duration of a spot's frame, s */
static double spot_seconds(int proto)
{
#if TW_WITH_PI4
    if (proto == TWL_PROTO_PI4) return tw_pi4_duration();
#endif
#if TW_WITH_TWIST
    if (proto == TWL_PROTO_TWL) return (2 * tw_ncoded(TW_TYPE_L) + 1) * tw_tchip(0.5);
#endif
    (void)proto;
    return TW_SHORT_CHIPS * tw_tchip(0.5);
}

int twdec_decode(twdec_t *d, const float *samples, int n, int fs,
                 const twl_settings_t *s, twl_spot_t *spots, int max_spots)
{
    tw_params_t p;
    tw_spot_t raw[TWDEC_MAX_RAW];
    int i, ns, nout = 0;

    if (!d) return -1;
    d->err[0] = '\0';
    if (!samples || !s || n <= 0 || fs <= 0 || max_spots < 0 || (max_spots > 0 && !spots)) {
        set_err(d, "invalid argument");
        return -1;
    }
    if (!(s->protocols & (TWL_PROTO_ALL | TWL_PROTO_PI4))) {
        set_err(d, "no protocol selected");
        return -1;
    }
    if (!(s->protocols & d->protos)) return 0;       /* none of this library's */
    if (n > d->nbuf) {
        double *b = (double *)realloc(d->buf, sizeof(double) * (size_t)n);
        if (!b) { set_err(d, "out of memory"); return -1; }
        d->buf = b;
        d->nbuf = n;
    }
    for (i = 0; i < n; i++) d->buf[i] = (double)samples[i];

    tw_params_default(&p);
    p.protocols  = s->protocols & d->protos;          /* same bits as TW_PROTO_* */
    if (s->pi4_f_center > 0) p.pi4_f_center = s->pi4_f_center;
    if (s->pi4_tol > 0) p.pi4_tol = s->pi4_tol;
    p.f_center   = s->f_center;
    p.tol        = s->tol;
    p.spread_hz  = s->spread_hz > 0 ? s->spread_hz : 0.0;
    p.deep       = s->deep ? 1 : 0;
    p.known_only = s->known_only ? 1 : 0;
    p.end_min    = s->end_min;
    p.end_max    = s->end_max;
#if TW_WITH_CLASSIC
    p.known      = d->nknown ? d->known : NULL;
    p.nknown     = d->nknown;
#else
    p.calls      = d->ncalls ? (const char (*)[12])d->calls : NULL;
    p.ncalls     = d->ncalls;
#endif
    if (p.known_only && d->ncalls == 0) {
        set_err(d, "known_only needs a list of known callsigns (set_known)");
        return -1;
    }

    ns = tw_decode_real(d->buf, (long)n, fs, &p, raw, TWDEC_MAX_RAW);
    if (ns < 0) {
        set_err(d, "block too short or invalid for decoding");
        return -1;
    }
    for (i = 0; i < ns && nout < max_spots; i++) {
        twl_spot_t *o = &spots[nout++];
        memset(o, 0, sizeof *o);
        memcpy(o->call, raw[i].call, sizeof o->call); o->call[sizeof o->call - 1] = '\0';
        snprintf(o->loc, sizeof o->loc, "%s", raw[i].loc);
        o->proto    = raw[i].proto;
        o->deep     = raw[i].deep;
        o->start    = raw[i].dt;
        o->end      = raw[i].dt + spot_seconds(raw[i].proto);
        o->freq     = raw[i].df;
        o->snr      = raw[i].snr;
        o->drift    = raw[i].drift;
        o->contrast = raw[i].proto == TWL_PROTO_PI4 ? 0.0 : raw[i].contrast;
    }
    return nout;
}

int twdec_decode_recent(twdec_t *d, const float *samples, int n, int fs,
                        const twl_settings_t *s, double lookback,
                        twl_spot_t *spots, int max_spots)
{
    twl_settings_t w;
    double t_end;
    if (!d) return -1;
    if (!s || fs <= 0 || lookback <= 0) {
        set_err(d, "invalid argument");
        return -1;
    }
    t_end = (double)n / fs;
    w = *s;
    w.end_min = t_end - lookback;
    w.end_max = t_end;
    return twdec_decode(d, samples, n, fs, &w, spots, max_spots);
}

const char *twdec_error(const twdec_t *d)
{
    return d ? d->err : "no decoder";
}
