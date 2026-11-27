/*
 * pi4pipe.c - PI4 frames in libtwist (see pi4pipe.h): the pipeline of
 * pi4d (sync, SNR, symbol metrics, Fano), on a start/end window.
 */
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pi4.h"
#include "pi4sync.h"
#include "pi4demod.h"
#include "pi4dec.h"
#include "pi4snr.h"
#include "pi4resample.h"
#include "pi4pipe.h"
#include "twcall.h"

double tw_pi4_duration(void) { return PI4_MSGDUR; }

static int is_locator(const char *s)
{
    size_t n = strlen(s);
    if (n != 4 && n != 6) return 0;
    if (s[0] < 'A' || s[0] > 'R' || s[1] < 'A' || s[1] > 'R') return 0;
    if (!isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3])) return 0;
    if (n == 6 && (s[4] < 'A' || s[4] > 'X' || s[5] < 'A' || s[5] > 'X')) return 0;
    return 1;
}

int tw_pi4_message(const char msg[9], char call[12], char loc[7])
{
    char t[9], base[9], norm[7];
    int a = 0, b = (int)strnlen(msg, 8), i, nparts = 0, digits;
    const char *part[3];
    char buf[9];
    call[0] = '\0'; loc[0] = '\0';
    while (a < b && msg[a] == ' ') a++;
    while (b > a && msg[b - 1] == ' ') b--;
    if (b <= a) return 0;
    memcpy(t, msg + a, (size_t)(b - a)); t[b - a] = '\0';
    for (i = 0; t[i]; i++) if (!isalnum((unsigned char)t[i]) && t[i] != '/') return 0;   /* no space inside */

    /* "/JN12AX": the locator of the station */
    if (t[0] == '/') {
        if (!is_locator(t + 1)) return 0;
        strcpy(loc, t + 1);
        return 2;
    }
    /* a callsign, with at most one prefix or suffix: the part with the
     * digit that is not a lone digit or a short suffix is the base */
    strcpy(buf, t);
    {
        char *s = buf, *q;
        while (nparts < 3 && s) {
            q = strchr(s, '/');
            if (q) *q = '\0';
            part[nparts++] = s;
            s = q ? q + 1 : NULL;
        }
        if (s) return 0;                               /* more than two slashes */
    }
    base[0] = '\0';
    for (i = 0; i < nparts; i++) {
        if (!*part[i]) return 0;                       /* "F5ZZO/" or "//" */
        for (digits = 0, a = 0; part[i][a]; a++) digits += isdigit((unsigned char)part[i][a]) != 0;
        if (digits && strlen(part[i]) >= 3 && strlen(part[i]) > strlen(base)) strcpy(base, part[i]);
    }
    if (!base[0] || strlen(base) > 6) return 0;
    if (tw_normalize_call(base, norm) != 0 || !tw_call_plausible(norm)) return 0;
    strcpy(call, t);
    return 1;
}

int tw_pi4_decode(const double *x, long n, int fs, const tw_params_t *p,
                  tw_spot_t *spots, int max)
{
    double *y = NULL;
    const double *d = x;
    long ny = n;
    float *f;
    double total, lo, hi, tone0 = p->pi4_f_center - 0.5 * PI4_TONESPACING;
    int nout = 0, i;

    if (max <= 0) return 0;
    if (fs != (int)PI4_FS) {
        if (resample_to_rate(x, n, fs, (int)PI4_FS, &y, &ny) != 0) return -1;
        d = y;
    }
    total = (double)ny / PI4_FS;
    /* frame starts: the start window of p, and the end window minus a frame */
    lo = p->dt_min > 0 ? p->dt_min : 0.0;
    hi = p->dt_max >= 0 ? p->dt_max : total;
    if (p->end_max >= 0 && p->end_max - PI4_MSGDUR < hi) hi = p->end_max - PI4_MSGDUR;
    if (p->end_min > -1e8 && p->end_min - PI4_MSGDUR > lo) lo = p->end_min - PI4_MSGDUR;
    if (lo < 0) lo = 0;
    if (hi > total - PI4_MSGDUR - 0.05) hi = total - PI4_MSGDUR - 0.05;
    if (hi < lo) { free(y); return 0; }

    f = (float *)malloc(sizeof(float) * (size_t)ny);
    if (!f) { free(y); return -1; }
    for (i = 0; i < ny; i++) f[i] = (float)d[i];

    /* windows still to search: the strongest frame of a window is found,
     * then the parts of the window before and after it (another frame
     * cannot overlap it) - every frame of a long block, in any order */
    {
        double wl[64], wh[64];
        int nw = 0;
        wl[nw] = lo; wh[nw] = hi; nw++;
        while (nw > 0 && nout < max) {
            double a = wl[nw - 1], b = wh[nw - 1];
            long off = (long)lround(a * PI4_FS);
            double dtx, dfx, peak, dt_abs, snr;
            int flip;
            float sym[PI4_NSYM];
            char msg[PI4_MSGLEN + 1], call[12], loc[7];
            long metric;
            int kind;
            nw--;
            if (b < a) continue;
            if (pi4_sync(d + off, (int)(ny - off), tone0, p->pi4_tol, b - a, PI4_TSYM / 10.0,
                         &dtx, &dfx, &peak, &flip) != 0) continue;
            dt_abs = a + dtx;
            snr = pi4_estimate_snr(f, (int)ny, dt_abs, dfx);
            if (pi4_symbol_metrics(f, (int)ny, dt_abs, dfx, flip, sym) != 0) continue;
            if (pi4_decode_message(sym, msg, &metric) == 0 && (kind = tw_pi4_message(msg, call, loc)) != 0) {
                tw_spot_t *o = &spots[nout++];
                memset(o, 0, sizeof *o);
                snprintf(o->call, sizeof o->call, "%s", call);
                snprintf(o->loc, sizeof o->loc, "%s", loc);
                o->proto = TW_PROTO_PI4;
                o->dt = dt_abs;
                o->df = dfx + 0.5 * PI4_TONESPACING;    /* the "carrier", as pi4d prints it */
                o->snr = snr;
                o->z = peak;
                o->score = (double)metric;
                o->nvalid = PI4_NSYM;
                if (p->verbose)
                    fprintf(stderr, "PI4: DT=%.3f s DF=%.2f Hz SNR %.1f dB Fano %ld : \"%s\"\n",
                            dt_abs, o->df, snr, metric, msg);
                /* the rest of the window, before and after this frame */
                if (nw + 2 <= 64) {
                    wl[nw] = dt_abs + PI4_MSGDUR; wh[nw] = b; nw++;
                    wl[nw] = a; wh[nw] = dt_abs - PI4_MSGDUR; nw++;
                }
            } else if (p->verbose)
                fprintf(stderr, "PI4: candidate DT=%.3f s DF=%.2f Hz SNR %.1f dB : no valid message\n",
                        dt_abs, dfx + 0.5 * PI4_TONESPACING, snr);
        }
    }
    free(f);
    free(y);
    return nout;
}
