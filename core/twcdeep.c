/*
 * twcdeep.c - Deep search for known callsigns. See twcdeep.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twcdeep.h"
#include "twcenc.h"

#define NCH TWC_NCHIPS

int twc_known_init(const char *call, twc_known_t *k)
{
    int i, j = 0;
    memset(k, 0, sizeof *k);
    if (twc_encode(call, k->chips, k->norm) != 0) return -1;
    for (i = 0; i < TWC_NCODED; i++) k->s[i] = (signed char)(k->chips[2 + 2 * i] ? 1 : -1);
    for (i = 0; i < 6; i++) if (k->norm[i] != ' ') k->call[j++] = k->norm[i];
    k->call[j] = '\0';
    return 0;
}

/* Upper tail of the standard normal, and its inverse by bisection. */
static double q_tail(double z) { return 0.5 * erfc(z / sqrt(2.0)); }

double twc_deep_threshold(double n_hyp, double fa)
{
    double target, lo = 0.0, hi = 40.0;
    int it;
    if (n_hyp < 1.0) n_hyp = 1.0;
    if (fa <= 0.0) fa = 1e-6;
    target = fa / n_hyp;
    for (it = 0; it < 100; it++) {
        double mid = 0.5 * (lo + hi);
        if (q_tail(mid) > target) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

static int cmp_hit(const void *a, const void *b)
{
    double x = ((const twc_deephit_t *)a)->z, y = ((const twc_deephit_t *)b)->z;
    return (x < y) - (x > y);
}

int twc_deep_search(const tw_spec_t *sp, const twc_known_t *known, int nknown,
                      double dt_min, double dt_max, double tol, double max_missing,
                      double fa, int max_hits, twc_deephit_t *out, double *z_th)
{
    const int M = sp->M;
    const long span = (long)(NCH - 1) * M;
    long miss, p_lo, p_hi, np_map, p, k, pp;
    int Bs, nbs, bmin, b, kc;
    float *Z, *D, *Qs, *zero_row;
    double zth, n_hyp, dur = NCH * sp->tchip;
    twc_deephit_t *list = NULL;
    long nlist = 0, cap = 0;
    int nout = 0;

    if (nknown <= 0) return 0;
    if (max_missing < 0.0) max_missing = 0.0;
    if (max_missing > 0.5) max_missing = 0.5;
    miss = (long)floor(max_missing * NCH) * M;

    /* Clamp in floating point before converting: the default window
     * is +/-1e9 s, which overflows a 32-bit long (Windows). */
    p_lo = (long)fmax(ceil(dt_min * sp->fs / sp->step), (double)-miss);
    p_hi = (long)fmin(floor(dt_max * sp->fs / sp->step), (double)(sp->np - 1 - span + miss));
    if (p_hi < p_lo) return 0;
    np_map = p_hi - p_lo + 1;

    Bs = (int)floor(tol / sp->df);
    if (Bs > sp->b0) Bs = sp->b0;
    bmin = sp->b0 - Bs;
    nbs = 2 * Bs + 1;

    /* Effective number of independent hypotheses. Z decorrelates
     * after roughly 1/3 chip in time (M positions = 1 chip) and 1/3
     * main-lobe width (1/T Hz) in frequency - factor 3 per axis,
     * calibrated on pure noise (tests/deep_calib.c): with it, the
     * measured distribution of the largest Z of a run matches
     * n_hyp * Q(z) at the median and stays below it in the tail
     * (i.e. the real false-alarm rate is at most the requested one). */
    n_hyp = (3.0 * (double)np_map / M + 1.0) * (3.0 * (double)nbs * sp->df * sp->tchip + 1.0) * nknown;
    zth = twc_deep_threshold(n_hyp, fa);
    if (z_th) *z_th = zth;

    Z = (float *)malloc(sizeof(float) * (size_t)nknown * (size_t)np_map * (size_t)nbs);
    D = (float *)malloc(sizeof(float) * (size_t)TWC_NCODED * (size_t)nbs);
    Qs = (float *)malloc(sizeof(float) * (size_t)nbs);
    zero_row = (float *)calloc((size_t)sp->nb, sizeof(float));
    if (!Z || !D || !Qs || !zero_row) { free(Z); free(D); free(Qs); free(zero_row); return -1; }

    for (p = p_lo; p <= p_hi; p++) {
        for (b = 0; b < nbs; b++) Qs[b] = 0.0f;
        for (k = 0; k < TWC_NCODED; k++) {
            long pa = p + (1 + 2 * k) * M, pb = p + (2 + 2 * k) * M;
            const float *ra = (pa >= 0 && pa < sp->np) ? sp->E + pa * sp->nb + bmin : zero_row;
            const float *rb = (pb >= 0 && pb < sp->np) ? sp->E + pb * sp->nb + bmin : zero_row;
            float *d = D + k * nbs;
            if (ra == zero_row || rb == zero_row) {
                memset(d, 0, sizeof(float) * (size_t)nbs);
            } else {
                for (b = 0; b < nbs; b++) { d[b] = rb[b] - ra[b]; Qs[b] += d[b] * d[b]; }
            }
        }
        for (kc = 0; kc < nknown; kc++) {
            float *zrow = Z + ((size_t)kc * np_map + (size_t)(p - p_lo)) * nbs;
            const signed char *s = known[kc].s;
            for (b = 0; b < nbs; b++) zrow[b] = 0.0f;
            for (k = 0; k < TWC_NCODED; k++) {
                const float *d = D + k * nbs;
                if (s[k] > 0) for (b = 0; b < nbs; b++) zrow[b] += d[b];
                else          for (b = 0; b < nbs; b++) zrow[b] -= d[b];
            }
            for (b = 0; b < nbs; b++) zrow[b] = Qs[b] > 0.0f ? zrow[b] / sqrtf(Qs[b]) : 0.0f;
        }
    }
    free(D); free(Qs); free(zero_row);

    for (kc = 0; kc < nknown; kc++) {
        const float *zm = Z + (size_t)kc * np_map * nbs;
        for (pp = 0; pp < np_map; pp++)
            for (b = 0; b < nbs; b++) {
                float v = zm[pp * nbs + b];
                int ismax = 1, dp, db;
                if (v < zth) continue;
                for (dp = -1; dp <= 1 && ismax; dp++)
                    for (db = -1; db <= 1; db++) {
                        long q = pp + dp; int b2 = b + db;
                        if ((dp == 0 && db == 0) || q < 0 || q >= np_map || b2 < 0 || b2 >= nbs) continue;
                        if (zm[q * nbs + b2] > v) { ismax = 0; break; }
                    }
                if (!ismax) continue;
                if (nlist == cap) {
                    long nc2 = cap ? cap * 2 : 64;
                    twc_deephit_t *nl = (twc_deephit_t *)realloc(list, sizeof(twc_deephit_t) * (size_t)nc2);
                    if (!nl) { free(list); free(Z); return -1; }
                    list = nl; cap = nc2;
                }
                list[nlist].k = kc;
                list[nlist].p = pp + p_lo;
                list[nlist].b = b + bmin;
                list[nlist].t0 = (double)(pp + p_lo) * sp->step / sp->fs;
                list[nlist].f0 = (double)(b + bmin - sp->b0) * sp->df;
                list[nlist].z = v;
                nlist++;
            }
    }
    free(Z);

    if (nlist > 0) {
        long a;
        qsort(list, (size_t)nlist, sizeof(twc_deephit_t), cmp_hit);
        for (a = 0; a < nlist && nout < max_hits; a++) {
            int j, keep = 1;
            for (j = 0; j < nout; j++)
                if (out[j].k == list[a].k && fabs(list[a].t0 - out[j].t0) < 0.5 * dur &&
                    fabs(list[a].f0 - out[j].f0) < 3.0 / sp->tchip) { keep = 0; break; }
            if (keep) out[nout++] = list[a];
        }
    }
    free(list);
    return nout;
}

/* Z from exact chip energies. */
static double z_of(const float e[NCH], const signed char s[TWC_NCODED])
{
    double num = 0.0, den = 0.0;
    int k;
    for (k = 0; k < TWC_NCODED; k++) {
        float a = e[1 + 2 * k], b = e[2 + 2 * k];
        double d;
        if (a < 0.0f || b < 0.0f) continue;
        d = (double)b - a;
        num += s[k] * d;
        den += d * d;
    }
    return den > 0.0 ? num / sqrt(den) : 0.0;
}

static void try_deep(const tw_bb_t *bb, const tw_spec_t *sp, const twc_known_t *k,
                     long t0, double f, double drift, int min_valid, tw_fit_t *best)
{
    float e[NCH];
    int nv = tw_chip_energies_sp(bb, sp, t0, f, drift, e);
    double z;
    if (nv < min_valid) return;
    z = z_of(e, k->s);
    if (z > best->score) {
        best->t_samp = t0; best->f = f; best->drift = drift; best->score = z;
        best->mu = tw_noise_at(sp, f); best->nvalid = nv;
        best->zscale = sp->zscale; best->snr_corr = sp->snr_corr;
        memcpy(best->e, e, sizeof e);
    }
}

int twc_deep_refine(const tw_bb_t *bb, const tw_spec_t *sp,
                      const twc_known_t *k, const twc_deephit_t *h,
                      double drift_max, double max_missing, tw_fit_t *fit)
{
    long tc = h->p * sp->step, dt;
    int s1 = sp->step / 4 > 0 ? sp->step / 4 : 1;
    int min_valid = (int)ceil((1.0 - max_missing) * NCH);
    double df = sp->df;
    int j;
    tw_fit_t best;

    memset(&best, 0, sizeof best);
    best.score = -1e300;
    best.nch = NCH;          /* (was left at 0: SNR -99 dB and no consistency figures for deep hits) */
    for (dt = -sp->step; dt <= sp->step; dt += s1)
        for (j = -4; j <= 4; j++)
            try_deep(bb, sp, k, tc + dt, h->f0 + j * df / 4.0, 0.0, min_valid, &best);
    if (best.score <= -1e299) return -1;

    if (drift_max > 0.0) {
        double dur = NCH * sp->tchip, dd = 0.5 * df / dur;
        int K = (int)ceil(drift_max / dd), kd;
        long t_b = best.t_samp;
        double f_b = best.f;
        if (K > 12) { K = 12; dd = drift_max / K; }
        for (kd = -K; kd <= K; kd++) {
            if (kd == 0) continue;
            for (j = -2; j <= 2; j++)
                try_deep(bb, sp, k, t_b, f_b + j * df / 4.0, kd * dd, min_valid, &best);
        }
    }
    {
        long t_b = best.t_samp;
        double f_b = best.f, d_b = best.drift;
        int s2 = s1 / 4 > 0 ? s1 / 4 : 1;
        for (dt = -s1; dt <= s1; dt += s2)
            for (j = -2; j <= 2; j++)
                try_deep(bb, sp, k, t_b + dt, f_b + j * df / 16.0, d_b, min_valid, &best);
    }
    {
        int i;
        for (i = 0; i < TWC_NCODED; i++) {
            float a = best.e[1 + 2 * i], b = best.e[2 + 2 * i];
            best.soft[i] = (a < 0.0f || b < 0.0f) ? 0.0f : (float)((b - a) / best.mu * best.zscale);
        }
    }
    *fit = best;
    return 0;
}
