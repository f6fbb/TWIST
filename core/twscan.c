/*
 * twscan.c - Wideband pre-detection of classic signals. See
 * twscan.h for the principle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>
#include "twdefs.h"
#include "twscan.h"
#include "twbb.h"

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static int cmp_hit(const void *a, const void *b)
{
    double x = ((const tw_scanhit_t *)a)->z, y = ((const tw_scanhit_t *)b)->z;
    return (x < y) - (x > y);
}

int tw_scan(const double *I, const double *Q, long n, int fs, double tchip,
               double f_lo, double f_hi, double z_min, double min_sep,
               int max_hits, tw_scanhit_t *out, long *n_bins, int verbose)
{
    return tw_scan_w(I, Q, n, fs, tchip, f_lo, f_hi, z_min, min_sep, max_hits, out, n_bins,
                     TW_SHORT_CHIPS - 1, 0.0, verbose);
}

int tw_scan_w(const double *I, const double *Q, long n, int fs, double tchip,
              double f_lo, double f_hi, double z_min, double min_sep,
              int max_hits, tw_scanhit_t *out, long *n_bins, int win_jumps,
              double min_var_ratio, int verbose)
{
    const int PH = 4;                       /* windows per chip            */
    const int WIN = win_jumps >= 16 ? win_jumps : 16;   /* jumps measured  */
    double hop = tchip * fs / PH;
    int L = (int)lround(tchip * fs);
    int nfft = 2 * L;
    double df = (double)fs / nfft;
    long nfr, j, b, k_lo, k_hi, nb;
    float *E = NULL, *stat = NULL, *tmp = NULL;
    fftw_complex *in = NULL, *outf = NULL;
    fftw_plan plan;
    tw_scanhit_t *cand = NULL;
    long ncand = 0;
    int nout = 0;

    if (L < 8) return -1;
    nfr = (long)floor((n - L) / hop) + 1;
    if (nfr < PH * 8) return -1;

    k_lo = (long)ceil(f_lo / df);
    k_hi = (long)floor(f_hi / df);
    if (!Q && k_lo < 1) k_lo = 1;
    if (!Q && k_hi > nfft / 2 - 1) k_hi = nfft / 2 - 1;
    if (Q && k_lo < -nfft / 2 + 1) k_lo = -nfft / 2 + 1;
    if (Q && k_hi > nfft / 2 - 1) k_hi = nfft / 2 - 1;
    nb = k_hi - k_lo + 1;
    if (nb < 5) return -1;
    if (n_bins) *n_bins = nb;

    E = (float *)malloc(sizeof(float) * (size_t)nfr * (size_t)nb);
    in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)nfft);
    outf = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)nfft);
    if (!E || !in || !outf) goto fail;
    tw_fftw_lock();
    plan = fftw_plan_dft_1d(nfft, in, outf, FFTW_FORWARD, FFTW_ESTIMATE);
    tw_fftw_unlock();
    memset(in, 0, sizeof(fftw_complex) * (size_t)nfft);

    /* 1. short-time spectrum, one-chip rectangular windows, 1/4-chip hop */
    for (j = 0; j < nfr; j++) {
        long s0 = (long)lround(j * hop), m;
        float *row = E + j * nb;
        for (m = 0; m < L; m++) {
            in[m][0] = I[s0 + m];
            in[m][1] = Q ? Q[s0 + m] : 0.0;
        }
        fftw_execute(plan);
        for (b = 0; b < nb; b++) {
            long k = k_lo + b;
            if (k < 0) k += nfft;
            row[b] = (float)(outf[k][0] * outf[k][0] + outf[k][1] * outf[k][1]);
        }
    }
    tw_fftw_lock(); fftw_destroy_plan(plan); tw_fftw_unlock();
    fftw_free(in); in = NULL;
    fftw_free(outf); outf = NULL;

    /* 2. per bin: normalize by the median over time, then the jump
     *    statistic, maximum over time and chip phase */
    tmp = (float *)malloc(sizeof(float) * (size_t)nfr);
    if (!tmp) goto fail;
    /* Noise reference per bin: median over time, then a running median
     * across +/- NBW bins. The second step matters: in the bin of a
     * beacon that transmits most of the time, the median over time is
     * raised by the signal itself (by a factor of ~3 for a strong
     * classic beacon on 40 % of the time), which would shrink exactly
     * the jumps we are looking for. */
    {
        const long NBW = 16;
        float *medt = (float *)malloc(sizeof(float) * (size_t)nb);
        float *win = (float *)malloc(sizeof(float) * (size_t)(2 * NBW + 1));
        float *medf = (float *)malloc(sizeof(float) * (size_t)nb);
        if (!medt || !win || !medf) { free(medt); free(win); free(medf); goto fail; }
        for (b = 0; b < nb; b++) {
            long ns = 0;
            for (j = 0; j < nfr; j += (nfr > 2000 ? nfr / 2000 : 1)) tmp[ns++] = E[j * nb + b];
            qsort(tmp, (size_t)ns, sizeof(float), cmp_float);
            medt[b] = tmp[ns / 2] / 0.6931472f;
        }
        for (b = 0; b < nb; b++) {
            long lo = b - NBW, hi = b + NBW, q, nw = 0;
            if (lo < 0) lo = 0;
            if (hi > nb - 1) hi = nb - 1;
            for (q = lo; q <= hi; q++) win[nw++] = medt[q];
            qsort(win, (size_t)nw, sizeof(float), cmp_float);
            medf[b] = win[nw / 2] > 0.0f ? win[nw / 2] : 1e-30f;
        }
        free(medt); free(win);
        stat = medf;           /* reused below: noise reference, then the statistic */
    }
    for (b = 0; b < nb; b++) {
        long ph;
        float med = stat[b], best = 0.0f;
        for (ph = 0; ph < PH; ph++) {
            long nk = (nfr - ph + PH - 1) / PH, kk;
            double acc = 0.0, accE = 0.0, accE2 = 0.0;
            int w = (nk - 1 < WIN) ? (int)(nk - 1) : WIN;
            if (w < 16) continue;
            /* sliding sum of |e(k+1)-e(k)| over w jumps */
            for (kk = 0; kk + 1 < nk; kk++) {
                float d = fabsf(E[(ph + (kk + 1) * PH) * nb + b] - E[(ph + kk * PH) * nb + b]) / med;
                acc += d;
                {
                    double e = E[(ph + kk * PH) * nb + b] / med;
                    accE += e;
                    accE2 += e * e;
                }
                if (kk >= w) {
                    double e = E[(ph + (kk - w) * PH) * nb + b] / med;
                    acc -= fabsf(E[(ph + (kk + 1 - w) * PH) * nb + b] -
                                 E[(ph + (kk - w) * PH) * nb + b]) / med;
                    accE -= e;
                    accE2 -= e * e;
                }
                /* Keying index acc/accE (mean jump / mean energy): about 1
                 * for noise, 1 to 1.5 for on/off keying, but well below 1
                 * for a steady carrier, whose energy is large and hardly
                 * moves (its jumps grow only as the square root of its
                 * level). Steady carriers (birdies, unkeyed beacons) are
                 * thus kept out of the candidate list. */
                /* Optionally (frame in progress) a second test against
                 * steady carriers, sharper than the keying index at the
                 * levels that matter: the energy of a bin varies as much
                 * as its mean for noise AND for on/off keying (var/mean^2
                 * ~ 1), but less for noise plus a steady carrier of
                 * per-chip SNR r (1 - r^2/(1+r)^2: 0.56 at r = 2). */
                if (kk >= w - 1 && acc / w > best && acc >= 0.7 * accE) {
                    double m1 = accE / w, v = accE2 / w - m1 * m1;
                    if (min_var_ratio <= 0.0 || (m1 > 0.0 && v / (m1 * m1) >= min_var_ratio))
                        best = (float)(acc / w);
                }
            }
        }
        stat[b] = best;
    }
    free(E); E = NULL;

    /* 3. robust z across the band, local maxima, strongest first */
    {
        float *s2 = (float *)malloc(sizeof(float) * (size_t)nb);
        double medb, sig;
        if (!s2) goto fail;
        memcpy(s2, stat, sizeof(float) * (size_t)nb);
        qsort(s2, (size_t)nb, sizeof(float), cmp_float);
        medb = s2[nb / 2];
        for (b = 0; b < nb; b++) s2[b] = fabsf(stat[b] - (float)medb);
        qsort(s2, (size_t)nb, sizeof(float), cmp_float);
        sig = 1.4826 * s2[nb / 2];
        free(s2);
        if (sig <= 0.0) sig = 1e-9;
        if (verbose)
            fprintf(stderr, "Scan: %ld bins of %.2f Hz from %.0f to %.0f Hz, %ld windows; "
                    "jump statistic median %.3f, sigma %.4f\n",
                    nb, df, k_lo * df, k_hi * df, nfr, medb, sig);
        cand = (tw_scanhit_t *)malloc(sizeof(tw_scanhit_t) * (size_t)nb);
        if (!cand) goto fail;
        for (b = 0; b < nb; b++) {
            double z = (stat[b] - medb) / sig;
            if (z < z_min) continue;
            if (b > 0 && stat[b - 1] > stat[b]) continue;
            if (b < nb - 1 && stat[b + 1] >= stat[b]) continue;
            cand[ncand].freq = (k_lo + b) * df;
            cand[ncand].z = z;
            ncand++;
        }
    }
    qsort(cand, (size_t)ncand, sizeof(tw_scanhit_t), cmp_hit);
    {
        long a;
        for (a = 0; a < ncand && nout < max_hits; a++) {
            int c, keep = 1;
            for (c = 0; c < nout; c++)
                if (fabs(cand[a].freq - out[c].freq) < min_sep) { keep = 0; break; }
            if (keep) out[nout++] = cand[a];
        }
    }
    free(cand);
    free(stat);
    free(tmp);
    return nout;

fail:
    free(E); free(stat); free(tmp); free(cand);
    if (in) fftw_free(in);
    if (outf) fftw_free(outf);
    return -1;
}
