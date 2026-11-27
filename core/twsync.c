/*
 * twsync.c - Spectrogram, candidate search, refinement and SNR
 * estimation for classic. See twsync.h for the principle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>
#include "twsync.h"


/* ---------------------------------------------------------------- */
/* Small helpers                                                     */
/* ---------------------------------------------------------------- */

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Median of n floats (copies, does not modify v). */
static float median_of(const float *v, long n, float *scratch)
{
    if (n <= 0) return 0.0f;
    memcpy(scratch, v, sizeof(float) * (size_t)n);
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    return (n & 1) ? scratch[n / 2] : 0.5f * (scratch[n / 2 - 1] + scratch[n / 2]);
}

/* ---------------------------------------------------------------- */
/* 1. Spectrogram                                                    */
/* ---------------------------------------------------------------- */

int tw_spectrogram(const tw_bb_t *bb, double tchip, double tol, double spread_hz,
                      tw_spec_t *sp)
{
    int hs, Braw, nbraw;
    float *raw;
    double Lf = bb->fs * tchip;
    int L = (int)lround(Lf);
    int zp, nfft, Bt, b, W;
    long p, nsamp_pos, i;
    fftw_complex *in, *out;
    fftw_plan plan;
    float *col, *scratch, *mu_raw;

    memset(sp, 0, sizeof *sp);
    if (L < 4 || fabs(Lf - L) > 1e-6) return -1;
    if (bb->n < L) return -1;

    sp->tchip = tchip;
    sp->fs = bb->fs;
    sp->L = L;
    sp->step = (L % 16 == 0) ? L / 16 : 1;
    sp->M = L / sp->step;
    zp = (L <= 512) ? 4 : 2;
    nfft = zp * L;
    sp->nfft = nfft;
    sp->df = bb->fs / nfft;

    /* spread: half-width in bins, at least one main lobe (zp bins) so
     * that the SNR correction below holds */
    hs = 0;
    if (spread_hz > 0.0) {
        hs = (int)lround(0.5 * spread_hz / sp->df);
        if (hs < zp) hs = zp;
    }
    sp->hs = hs;
    sp->zscale = 1.0;
    sp->snr_corr = 1.0;
    sp->ncoded = 119;            /* the 239-chip frames (classic, TWIST S) */
    sp->nch = TW_SHORT_CHIPS;
    sp->ops = NULL;

    Bt = (int)ceil((tol + 2.0 / tchip) / sp->df) + 8 * zp;
    if (2 * (Bt + hs) + 1 > nfft) Bt = (nfft - 1) / 2 - hs;
    if (Bt < 1) return -1;
    Braw = Bt + hs;
    nbraw = 2 * Braw + 1;
    sp->b0 = Bt;
    sp->nb = 2 * Bt + 1;
    sp->np = (bb->n - L) / sp->step + 1;

    sp->E = (float *)malloc(sizeof(float) * (size_t)sp->np * (size_t)sp->nb);
    sp->mu = (float *)malloc(sizeof(float) * (size_t)sp->nb);
    in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)nfft);
    out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)nfft);
    raw = (float *)malloc(sizeof(float) * (size_t)nbraw);
    if (!sp->E || !sp->mu || !in || !out || !raw) {
        fftw_free(in); fftw_free(out); free(raw); tw_spectrogram_free(sp); return -1;
    }
    tw_fftw_lock();
    plan = fftw_plan_dft_1d(nfft, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    tw_fftw_unlock();
    memset(in, 0, sizeof(fftw_complex) * (size_t)nfft);

    for (p = 0; p < sp->np; p++) {
        const fftw_complex *src = bb->z + p * sp->step;
        float *row = sp->E + p * sp->nb;
        memcpy(in, src, sizeof(fftw_complex) * (size_t)L);
        fftw_execute(plan);
        for (b = 0; b < nbraw; b++) {
            int k = b - Braw;
            if (k < 0) k += nfft;
            raw[b] = (float)(out[k][0] * out[k][0] + out[k][1] * out[k][1]);
        }
        if (hs == 0) {
            memcpy(row, raw, sizeof(float) * (size_t)sp->nb);
        } else {
            /* running sum over 2*hs+1 bins */
            double acc = 0.0;
            int j;
            for (j = 0; j < 2 * hs + 1; j++) acc += raw[j];
            for (b = 0; b < sp->nb; b++) {
                row[b] = (float)acc;
                if (b + 2 * hs + 1 < nbraw) acc += raw[b + 2 * hs + 1] - raw[b];
            }
        }
    }
    free(raw);
    tw_fftw_lock(); fftw_destroy_plan(plan); tw_fftw_unlock();
    fftw_free(in);
    fftw_free(out);

    /* Noise level per bin: median over time (on at most ~4000 evenly
     * spaced positions), /ln2 to turn the median of an exponential
     * distribution into its mean; then a running median across
     * frequency, so that a signal present for most of the recording
     * (a classic beacon is "on" half of the time while transmitting)
     * does not inflate its own noise reference. */
    nsamp_pos = sp->np < 4000 ? sp->np : 4000;
    col = (float *)malloc(sizeof(float) * (size_t)nsamp_pos);
    scratch = (float *)malloc(sizeof(float) * (size_t)(nsamp_pos > sp->nb ? nsamp_pos : sp->nb));
    mu_raw = (float *)malloc(sizeof(float) * (size_t)sp->nb);
    if (!col || !scratch || !mu_raw) {
        free(col); free(scratch); free(mu_raw); tw_spectrogram_free(sp); return -1;
    }
    for (b = 0; b < sp->nb; b++) {
        for (i = 0; i < nsamp_pos; i++) {
            long pp = (long)((double)i * (sp->np - 1) / (nsamp_pos > 1 ? nsamp_pos - 1 : 1));
            col[i] = sp->E[pp * sp->nb + b];
        }
        mu_raw[b] = median_of(col, nsamp_pos, scratch) / 0.6931472f;
    }
    W = 8 * zp;
    for (b = 0; b < sp->nb; b++) {
        int lo = b - W, hi = b + W;
        if (lo < 0) lo = 0;
        if (hi > sp->nb - 1) hi = sp->nb - 1;
        sp->mu[b] = median_of(mu_raw + lo, hi - lo + 1, scratch);
        if (sp->mu[b] <= 0.0f) sp->mu[b] = 1e-30f;
    }
    free(col); free(scratch); free(mu_raw);

    for (p = 0; p < sp->np; p++) {
        float *row = sp->E + p * sp->nb;
        for (b = 0; b < sp->nb; b++) row[b] /= sp->mu[b];
    }

    if (hs > 0) {
        /* A sum of several (correlated) bins fluctuates less than one
         * bin: its normalized value has mean 1 but a variance well
         * below 1. Everything downstream (candidate threshold aside,
         * which is relative) is calibrated in single-bin noise units,
         * where the difference of two independent chip energies has a
         * variance of 2. Measure that variance here (trimmed, so that
         * signals do not count) and rescale around the mean to match:
         * E' = 1 + (E - 1) * zscale. */
        long ns = 0, cnt = 0, kk, nmax = 200000;
        double sum2 = 0.0, s0 = 0.0, thr;
        float *dv = (float *)malloc(sizeof(float) * (size_t)nmax);
        if (!dv) { tw_spectrogram_free(sp); return -1; }
        for (kk = 0; kk < nmax && sp->np > sp->M; kk++) {
            /* pseudo-random cells spread over the whole map */
            long pp = (long)((kk * 7919L) % (sp->np - sp->M));
            int bb2 = (int)((kk * 104729L) % sp->nb);
            dv[ns++] = sp->E[(pp + sp->M) * sp->nb + bb2] - sp->E[pp * sp->nb + bb2];
        }
        for (kk = 0; kk < ns; kk++) s0 += dv[kk] * dv[kk];
        thr = ns > 0 ? 25.0 * s0 / ns : 0.0;          /* 5 sigma, squared */
        for (kk = 0; kk < ns; kk++)
            if ((double)dv[kk] * dv[kk] < thr) { sum2 += dv[kk] * dv[kk]; cnt++; }
        free(dv);
        if (cnt > 100 && sum2 > 0.0) sp->zscale = sqrt(2.0 * cnt / sum2);
        for (p = 0; p < sp->np; p++) {
            float *row = sp->E + p * sp->nb;
            for (b = 0; b < sp->nb; b++) row[b] = 1.0f + (row[b] - 1.0f) * (float)sp->zscale;
        }
        /* A narrow carrier inside the summed band shows up zp times
         * (the bins oversample the 1/T main lobe zp times), while the
         * noise is counted once per bin: correct the SNR estimate. */
        sp->snr_corr = (double)(2 * hs + 1) / zp;
    }
    return 0;
}

void tw_spectrogram_free(tw_spec_t *sp)
{
    free(sp->E);
    free(sp->mu);
    memset(sp, 0, sizeof *sp);
}

/* ---------------------------------------------------------------- */
/* 2. Candidate search                                               */
/* ---------------------------------------------------------------- */

static int cmp_cand_desc(const void *a, const void *b)
{
    double x = ((const tw_cand_t *)a)->score, y = ((const tw_cand_t *)b)->score;
    return (x < y) - (x > y);
}

int tw_find_candidates(const tw_spec_t *sp, double dt_min, double dt_max,
                          double tol, double max_missing, double z_min,
                          int max_cand, tw_cand_t *out,
                          double *map_median, double *map_sigma)
{
    const int M = sp->M;
    const int NCH = sp->nch, NCOD = sp->ncoded;
    const long span = (long)(NCH - 1) * M;          /* chip 0 -> last chip */
    long miss = (long)floor(max_missing * NCH) * M;  /* positions allowed outside */
    long p_lo, p_hi, np_map, p, k;
    int Bs, nbs, bmin, b;
    float *S, *D, *zero_row, *acc, *best;
    double med, sig;
    tw_cand_t *list = NULL;
    long nlist = 0, cap = 0;
    int nout = 0;

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

    S = (float *)malloc(sizeof(float) * (size_t)np_map * (size_t)nbs);
    D = (float *)malloc(sizeof(float) * (size_t)NCOD * (size_t)nbs);
    zero_row = (float *)calloc((size_t)sp->nb, sizeof(float));
    acc = (float *)malloc(sizeof(float) * (size_t)nbs);
    best = (float *)malloc(sizeof(float) * (size_t)nbs);
    if (!S || !D || !zero_row || !acc || !best) {
        free(S); free(D); free(zero_row); free(acc); free(best); return -1;
    }

    for (p = p_lo; p <= p_hi; p++) {
        float *Srow = S + (p - p_lo) * nbs;
        /* soft bits for every bin at this start position */
        for (k = 0; k < NCOD; k++) {
            long pa = p + (1 + 2 * k) * M, pb = p + (2 + 2 * k) * M;
            const float *ra = (pa >= 0 && pa < sp->np) ? sp->E + pa * sp->nb + bmin : zero_row;
            const float *rb = (pb >= 0 && pb < sp->np) ? sp->E + pb * sp->nb + bmin : zero_row;
            float *d = D + k * nbs;
            if (ra == zero_row || rb == zero_row) {
                memset(d, 0, sizeof(float) * (size_t)nbs);
            } else {
                for (b = 0; b < nbs; b++) d[b] = rb[b] - ra[b];
            }
        }
        for (b = 0; b < nbs; b++) Srow[b] = 0.0f;
        if (sp->ops) { sp->ops->score(D, nbs, Srow, acc, best); continue; }
        /* any code: energy of the Manchester decisions */
        for (k = 0; k < NCOD; k++) {
            const float *d = D + k * nbs;
            for (b = 0; b < nbs; b++) Srow[b] += fabsf(d[b]);
        }
    }
    free(D); free(zero_row); free(acc); free(best);

    /* Robust statistics of the score map (median, 1.4826 * MAD) on
     * at most ~200000 evenly spaced cells. */
    {
        long ntot = np_map * nbs;
        long ns = ntot < 200000 ? ntot : 200000;
        float *smp = (float *)malloc(sizeof(float) * (size_t)ns);
        float *scr = (float *)malloc(sizeof(float) * (size_t)ns);
        if (!smp || !scr) { free(smp); free(scr); free(S); return -1; }
        for (k = 0; k < ns; k++) smp[k] = S[(long)((double)k * ntot / ns)];
        med = median_of(smp, ns, scr);
        for (k = 0; k < ns; k++) smp[k] = fabsf(smp[k] - (float)med);
        sig = 1.4826 * median_of(smp, ns, scr);
        if (sig <= 0.0) sig = 1e-9;
        free(smp); free(scr);
    }
    if (map_median) *map_median = med;
    if (map_sigma) *map_sigma = sig;

    /* Local maxima (3x3) above threshold. */
    {
        float thr = (float)(med + z_min * sig);
        long pp;
        for (pp = 0; pp < np_map; pp++) {
            for (b = 0; b < nbs; b++) {
                float v = S[pp * nbs + b];
                int ismax = 1, dp, db;
                if (v < thr) continue;
                for (dp = -1; dp <= 1 && ismax; dp++)
                    for (db = -1; db <= 1; db++) {
                        long q = pp + dp; int bb2 = b + db;
                        if ((dp == 0 && db == 0) || q < 0 || q >= np_map || bb2 < 0 || bb2 >= nbs) continue;
                        if (S[q * nbs + bb2] > v) { ismax = 0; break; }
                    }
                if (!ismax) continue;
                if (nlist == cap) {
                    long nc2 = cap ? cap * 2 : 256;
                    tw_cand_t *nl = (tw_cand_t *)realloc(list, sizeof(tw_cand_t) * (size_t)nc2);
                    if (!nl) { free(list); free(S); return -1; }
                    list = nl; cap = nc2;
                }
                list[nlist].p = pp + p_lo;
                list[nlist].b = b + bmin;
                list[nlist].t0 = (double)(pp + p_lo) * sp->step / sp->fs;
                list[nlist].f0 = (double)(b + bmin - sp->b0) * sp->df;
                list[nlist].score = v;
                list[nlist].z = (v - med) / sig;
                nlist++;
            }
        }
    }
    free(S);

    /* Strongest first, greedy non-maximum suppression: drop anything
     * within TW_SHIFT_SPAN bit-shifts (+1 chip) and 2 * (1/T) Hz of
     * an already kept candidate - those are the shifted-alignment
     * side peaks of the same signal, which the decoder explores
     * itself (tw_fit_shift). */
    if (nlist > 0) {
        long a;
        double dt_sup = (2.0 * TW_SHIFT_SPAN + 1.0) * sp->tchip, df_sup = 2.0 / sp->tchip;
        qsort(list, (size_t)nlist, sizeof(tw_cand_t), cmp_cand_desc);
        for (a = 0; a < nlist && nout < max_cand; a++) {
            int j, keep = 1;
            for (j = 0; j < nout; j++)
                if (fabs(list[a].t0 - out[j].t0) <= dt_sup && fabs(list[a].f0 - out[j].f0) <= df_sup) {
                    keep = 0; break;
                }
            if (keep) out[nout++] = list[a];
        }
    }
    free(list);
    return nout;
}

/* ---------------------------------------------------------------- */
/* 3. Refinement                                                     */
/* ---------------------------------------------------------------- */

/* Raw energy of each chip for a (start, mid-frame frequency, drift)
 * hypothesis. Chips outside the recording get -1. */
int tw_chip_energies(const tw_bb_t *bb, int L, int NCH, long t0, double f_mid,
                         double drift, float *e)
{
    double tmid = ((double)t0 + 0.5 * NCH * L) / bb->fs;
    int k, m, nvalid = 0;

    for (k = 0; k < NCH; k++) {
        long s = t0 + (long)k * L;
        double f, wr, wi, rr = 1.0, ri = 0.0, ar = 0.0, ai = 0.0;
        if (s < 0 || s + L > bb->n) { e[k] = -1.0f; continue; }
        f = f_mid + drift * (((double)s + 0.5 * L) / bb->fs - tmid);
        wr = cos(-2.0 * M_PI * f / bb->fs);
        wi = sin(-2.0 * M_PI * f / bb->fs);
        for (m = 0; m < L; m++) {
            const double *z = bb->z[s + m];
            double t;
            ar += z[0] * rr - z[1] * ri;
            ai += z[0] * ri + z[1] * rr;
            t = rr * wr - ri * wi;
            ri = rr * wi + ri * wr;
            rr = t;
        }
        e[k] = (float)(ar * ar + ai * ai);
        nvalid++;
    }
    return nvalid;
}

int tw_chip_energies_sp(const tw_bb_t *bb, const tw_spec_t *sp, long t0,
                           double f_mid, double drift, float *e)
{
    float tmp[TW_MAXCHIPS];
    int j, k, nv;
    if (sp->hs == 0) return tw_chip_energies(bb, sp->L, sp->nch, t0, f_mid, drift, e);
    nv = tw_chip_energies(bb, sp->L, sp->nch, t0, f_mid - sp->hs * sp->df, drift, e);
    for (j = -sp->hs + 1; j <= sp->hs; j++) {
        tw_chip_energies(bb, sp->L, sp->nch, t0, f_mid + j * sp->df, drift, tmp);
        for (k = 0; k < sp->nch; k++) if (e[k] >= 0.0f) e[k] += tmp[k];
    }
    return nv;
}

static void energies_to_soft(const float *e, int ncoded, double mu, double zscale, float *soft)
{
    int k;
    for (k = 0; k < ncoded; k++) {
        float a = e[1 + 2 * k], b = e[2 + 2 * k];
        soft[k] = (a < 0.0f || b < 0.0f) ? 0.0f : (float)((b - a) / mu * zscale);
    }
}

double tw_noise_at(const tw_spec_t *sp, double f)
{
    int b = (int)lround(f / sp->df) + sp->b0;
    if (b < 0) b = 0;
    if (b > sp->nb - 1) b = sp->nb - 1;
    return sp->mu[b];
}

void tw_spec_frame(tw_spec_t *sp, int ncoded, const tw_code_ops_t *ops)
{
    if (ncoded > TW_MAXCODED) ncoded = TW_MAXCODED;
    sp->ncoded = ncoded;
    sp->nch = 2 * ncoded + 1;
    sp->ops = ops;
}

/* Frame metric of the current geometry: the classic frame's (best Walsh codeword
 * per group), or, for another code, the energy of the Manchester
 * decisions. */
static double frame_metric(const tw_spec_t *sp, const float *soft)
{
    double s = 0.0;
    int k;
    if (sp->ops) return sp->ops->metric(soft);
    for (k = 0; k < sp->ncoded; k++) s += fabs(soft[k]);
    return s;
}

/* Evaluates one hypothesis, keeps it in *best if better. */
static void try_hyp(const tw_bb_t *bb, const tw_spec_t *sp, long t0, double f,
                    double drift, int min_valid, tw_fit_t *best)
{
    float e[TW_MAXCHIPS], soft[TW_MAXCODED];
    double mu = tw_noise_at(sp, f), score;
    int nv = tw_chip_energies_sp(bb, sp, t0, f, drift, e);
    if (nv < min_valid) return;
    energies_to_soft(e, sp->ncoded, mu, sp->zscale, soft);
    score = frame_metric(sp, soft);
    if (score > best->score) {
        best->t_samp = t0; best->f = f; best->drift = drift;
        best->score = score; best->mu = mu; best->nvalid = nv;
        best->zscale = sp->zscale; best->snr_corr = sp->snr_corr;
        best->nch = sp->nch;
        memcpy(best->e, e, sizeof(float) * (size_t)sp->nch);
        memcpy(best->soft, soft, sizeof(float) * (size_t)sp->ncoded);
    }
}

int tw_refine(const tw_bb_t *bb, const tw_spec_t *sp,
                 const tw_cand_t *c, double drift_max, double max_missing,
                 tw_fit_t *fit)
{
    long tc = c->p * sp->step;
    int s1 = sp->step / 4 > 0 ? sp->step / 4 : 1;
    int min_valid = (int)ceil((1.0 - max_missing) * sp->nch);
    double df = sp->df;
    long dt;
    int j;
    tw_fit_t best;

    memset(&best, 0, sizeof best);
    best.score = -1e300;

    /* stage 1: start time +/- one spectrogram step, frequency +/- 1 bin */
    for (dt = -sp->step; dt <= sp->step; dt += s1)
        for (j = -4; j <= 4; j++)
            try_hyp(bb, sp, tc + dt, c->f0 + j * df / 4.0, 0.0, min_valid, &best);
    if (best.score <= -1e299) return -1;

    /* stage 2: linear drift, steps of half a bin over the frame */
    if (drift_max > 0.0) {
        double dur = sp->nch * sp->tchip;
        double dd = 0.5 * df / dur;
        int K = (int)ceil(drift_max / dd), kd;
        long t_b = best.t_samp;
        double f_b = best.f;
        if (K > 12) { K = 12; dd = drift_max / K; }
        for (kd = -K; kd <= K; kd++) {
            if (kd == 0) continue;
            for (j = -2; j <= 2; j++)
                try_hyp(bb, sp, t_b, f_b + j * df / 4.0, kd * dd, min_valid, &best);
        }
    }

    /* stage 3: polish */
    {
        long t_b = best.t_samp;
        double f_b = best.f, d_b = best.drift;
        int s2 = s1 / 4 > 0 ? s1 / 4 : 1;
        for (dt = -s1; dt <= s1; dt += s2)
            for (j = -2; j <= 2; j++)
                try_hyp(bb, sp, t_b + dt, f_b + j * df / 16.0, d_b, min_valid, &best);
    }

    *fit = best;
    return 0;
}

int tw_fit_shift(const tw_bb_t *bb, const tw_spec_t *sp, const tw_fit_t *in,
                    int shift_chips, double max_missing, tw_fit_t *out)
{
    int min_valid = (int)ceil((1.0 - max_missing) * sp->nch);
    *out = *in;
    out->t_samp = in->t_samp + (long)shift_chips * sp->L;
    out->nvalid = tw_chip_energies_sp(bb, sp, out->t_samp, in->f, in->drift, out->e);
    if (out->nvalid < min_valid) return -1;
    energies_to_soft(out->e, sp->ncoded, out->mu, sp->zscale, out->soft);
    out->score = frame_metric(sp, out->soft);
    return 0;
}

/* ---------------------------------------------------------------- */
/* SNR                                                               */
/* ---------------------------------------------------------------- */

double tw_estimate_snr(const tw_fit_t *fit, const unsigned char *chips,
                          double tchip)
{
    double son = 0.0, soff = 0.0, r;
    int non = 0, noff = 0, k;

    for (k = 0; k < fit->nch; k++) {
        if (fit->e[k] < 0.0f) continue;
        if (chips[k]) { son += fit->e[k]; non++; }
        else          { soff += fit->e[k]; noff++; }
    }
    if (non == 0 || noff == 0) return -99.0;
    son /= non;
    soff /= noff;
    /* (E_on - E_off) / E_off = P_s * L / sigma^2 ; SNR(2500 Hz) =
     * P_s / (sigma^2 / fs * 2500) = ratio / (2500 * T) */
    r = (son - soff) / soff;
    if (fit->snr_corr > 0.0) r *= fit->snr_corr;
    if (r < 1e-4) r = 1e-4;
    return 10.0 * log10(r / (2500.0 * tchip));
}

double tw_estimate_snr_bb(const tw_bb_t *bb, const tw_spec_t *sp, const tw_fit_t *fit,
                             const unsigned char *chips, double tchip)
{
    /* The OFF chips are not a good noise reference for a strong
     * carrier: keying edges, fading or drift leave some of it there,
     * and a receiver's AGC raises the noise as soon as it stops - the
     * SNR of strong signals came out 10 dB and more too low (testers,
     * against the OPERA program). The noise is measured instead during
     * the ON chips themselves: the carrier fitted in each chip (the
     * same correlation as tw_chip_energies) is subtracted, and the
     * residual's spectrum, at least 15 chip bandwidths away from the
     * carrier (what the fit leaves of it stays near it), is the noise
     * at the same moments - same AGC gain. Its median over the bins and
     * chips keeps other signals of the band out. */
    int L = sp->L, NCH = fit->nch, k, m, b, non = 0, guard = 15;   /* bins of 1/T */
    double son = 0.0, noise, r, tmid = ((double)fit->t_samp + 0.5 * NCH * L) / bb->fs;
    long n = 0;
    float *v;
    fftw_complex *in, *out;
    fftw_plan plan;

    if (sp->hs != 0 || L < 16) return tw_estimate_snr(fit, chips, tchip);
    for (k = 0; k < NCH; k++)
        if (fit->e[k] >= 0.0f && chips[k]) { son += fit->e[k]; non++; }
    if (non == 0) return -99.0;
    son /= non;

    v = (float *)malloc(sizeof(float) * (size_t)non * (size_t)L);
    in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)L);
    out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)L);
    if (!v || !in || !out) { free(v); fftw_free(in); fftw_free(out); return tw_estimate_snr(fit, chips, tchip); }
    tw_fftw_lock();
    plan = fftw_plan_dft_1d(L, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    tw_fftw_unlock();

    for (k = 0; k < NCH; k++) {
        /* a window from the middle of an ON chip to the middle of the
         * next one, also ON: a fit a few samples off the true chips
         * would otherwise take in a keying edge, whose spectrum spreads
         * far from the carrier */
        long s0 = fit->t_samp + (long)k * L + L / 2;
        double f, wr, wi, rr = 1.0, ri = 0.0, ar = 0.0, ai = 0.0;
        int bc;
        if (k + 1 >= NCH || !chips[k] || !chips[k + 1] || fit->e[k] < 0.0f || fit->e[k + 1] < 0.0f) continue;
        if (s0 < 0 || s0 + L > bb->n) continue;
        f = fit->f + fit->drift * (((double)s0 + 0.5 * L) / bb->fs - tmid);
        wr = cos(-2.0 * M_PI * f / bb->fs);
        wi = sin(-2.0 * M_PI * f / bb->fs);
        for (m = 0; m < L; m++) {                    /* a = sum z c*   */
            const double *z = bb->z[s0 + m];
            double t;
            ar += z[0] * rr - z[1] * ri;
            ai += z[0] * ri + z[1] * rr;
            t = rr * wr - ri * wi; ri = rr * wi + ri * wr; rr = t;
        }
        ar /= L; ai /= L;
        rr = 1.0; ri = 0.0;
        for (m = 0; m < L; m++) {                    /* z - a c        */
            const double *z = bb->z[s0 + m];
            double t, cr = rr, ci = -ri;             /* c = conj(c*)   */
            in[m][0] = z[0] - (ar * cr - ai * ci);
            in[m][1] = z[1] - (ar * ci + ai * cr);
            t = rr * wr - ri * wi; ri = rr * wi + ri * wr; rr = t;
        }
        fftw_execute(plan);
        bc = (int)lround(f / bb->fs * L);            /* the carrier's bin */
        for (b = 0; b < L; b++) {
            int d = b - bc;
            d = ((d % L) + L) % L; if (d > L / 2) d = L - d;
            if (d <= guard) continue;
            v[n++] = (float)(out[b][0] * out[b][0] + out[b][1] * out[b][1]);
        }
    }
    tw_fftw_lock(); fftw_destroy_plan(plan); tw_fftw_unlock();
    fftw_free(in); fftw_free(out);
    if (n < 200) { free(v); return tw_estimate_snr(fit, chips, tchip); }
    qsort(v, (size_t)n, sizeof(float), cmp_float);
    noise = v[n / 2] / 0.6931472;                    /* median of an exponential -> its mean */
    free(v);
    r = (son - noise) / noise;
    if (r < 1e-4) r = 1e-4;
    return 10.0 * log10(r / (2500.0 * tchip));
}

