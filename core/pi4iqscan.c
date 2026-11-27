/*
 * pi4iqscan.c - see pi4iqscan.h (from pi4d)
 *
 * Build:
 *   gcc -O2 -Wall -c iqscan.c
 *   gcc ... iqscan.o -lfftw3 -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <fftw3.h>
#include "pi4.h"
#include "pi4iqscan.h"

typedef struct {
    int foff;
    long start;
    int score;
    double power;
} freq_cand_t;

static int freq_cand_cmp_desc(const void *a, const void *b)
{
    const freq_cand_t *fa = (const freq_cand_t *)a;
    const freq_cand_t *fb = (const freq_cand_t *)b;
    if (fb->score != fa->score) return fb->score - fa->score;
    if (fb->power > fa->power) return 1;
    if (fb->power < fa->power) return -1;
    return 0;
}

static int wrap_bin(int b, int nfft)
{
    b %= nfft;
    if (b < 0) b += nfft;
    return b;
}

int iq_scan(const double *I, const double *Q, long npts, int fs_in,
            double freq_center_hz, double freq_tol_hz, double dt_max,
            int min_quality_pct, int max_candidates, iq_candidate_t *out)
{
    int nfft = (int)lround(fs_in / PI4_DFBIN);
    double bin_hz = (double)fs_in / nfft;
    long nsps_native = (long)lround(PI4_TSYM * fs_in);
    long hop = nsps_native / 20;
    long stride = nsps_native / hop;   /* = 20, frames per symbol */
    int k_bins = (int)lround(PI4_TONESPACING / bin_hz);

    long n_frames = 1 + (npts - nfft) / hop;
    long max_start, needed_span;
    int tone0_bin_nominal, freq_tol_bins;
    float *mag;                 /* [frame * nfft + bin] */
    double *window;
    fftw_complex *fft_in, *fft_out;
    fftw_plan plan;
    long fr;
    int f_off;
    int n_foff;
    freq_cand_t *cands;
    uint8_t *sync_bit_all;      /* stdint not included on purpose - use unsigned char */
    float *best_p_all;

    if (n_frames <= 0) return -1;
    needed_span = stride * PI4_NSYM;
    max_start = n_frames - needed_span;
    {
        long dt_max_frames = (long)lround(dt_max * fs_in / (double)hop);
        if (dt_max_frames < max_start) max_start = dt_max_frames;
    }
    if (max_start <= 0) return -1;

    tone0_bin_nominal = (int)lround((freq_center_hz - 0.5 * PI4_TONESPACING) / bin_hz);
    freq_tol_bins = (int)lround(freq_tol_hz / bin_hz);
    if (freq_tol_bins < 1) freq_tol_bins = 1;
    n_foff = 2 * freq_tol_bins + 1;

    /* ---- Build the (complex, two-sided) spectrogram ---- */
    mag = (float *)malloc(sizeof(float) * (size_t)n_frames * (size_t)nfft);
    window = (double *)malloc(sizeof(double) * (size_t)nfft);
    if (!mag || !window) { free(mag); free(window); return -1; }
    for (long i = 0; i < nfft; i++)
        window[i] = 0.5 - 0.5 * cos(2.0 * M_PI * i / (nfft - 1));

    fft_in  = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)nfft);
    fft_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)nfft);
    plan = fftw_plan_dft_1d(nfft, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

    for (fr = 0; fr < n_frames; fr++) {
        long base = fr * hop;
        for (long i = 0; i < nfft; i++) {
            fft_in[i][0] = I[base + i] * window[i];
            fft_in[i][1] = Q[base + i] * window[i];
        }
        fftw_execute(plan);
        float *row = mag + (size_t)fr * nfft;
        for (long b = 0; b < nfft; b++) {
            double re = fft_out[b][0], im = fft_out[b][1];
            row[b] = (float)sqrt(re * re + im * im);
        }
    }
    fftw_destroy_plan(plan);
    fftw_free(fft_in);
    fftw_free(fft_out);
    free(window);

    /* ---- For each frequency offset, find the best time candidate ---- */
    sync_bit_all = (uint8_t *)malloc(sizeof(uint8_t) * (size_t)n_frames);
    best_p_all   = (float *)malloc(sizeof(float) * (size_t)n_frames);
    cands = (freq_cand_t *)malloc(sizeof(freq_cand_t) * (size_t)n_foff);
    if (!sync_bit_all || !best_p_all || !cands) {
        free(mag); free(sync_bit_all); free(best_p_all); free(cands);
        return -1;
    }

    for (int idx = 0; idx < n_foff; idx++) {
        f_off = idx - freq_tol_bins;
        int t0 = tone0_bin_nominal + f_off;

        for (fr = 0; fr < n_frames; fr++) {
            const float *row = mag + (size_t)fr * nfft;
            float p0 = row[wrap_bin(t0, nfft)];
            float p1 = row[wrap_bin(t0 + k_bins, nfft)];
            float p2 = row[wrap_bin(t0 + 2 * k_bins, nfft)];
            float p3 = row[wrap_bin(t0 + 3 * k_bins, nfft)];
            int best_tone = 0; float best_p = p0;
            if (p1 > best_p) { best_p = p1; best_tone = 1; }
            if (p2 > best_p) { best_p = p2; best_tone = 2; }
            if (p3 > best_p) { best_p = p3; best_tone = 3; }
            sync_bit_all[fr] = (uint8_t)(best_tone & 1);
            best_p_all[fr] = best_p;
        }

        int best_score = -1;
        long best_start = -1;
        double best_power = -1.0;
        for (long start = 0; start < max_start; start++) {
            long last = start + (long)(PI4_NSYM - 1) * stride;
            if (last >= n_frames) break;
            int score = 0;
            double power = 0.0;
            for (int s = 0; s < PI4_NSYM; s++) {
                long f2 = start + (long)s * stride;
                score += (sync_bit_all[f2] == PI4_SYNCVEC[s]);
                power += (double)best_p_all[f2] * best_p_all[f2];
            }
            if (score > best_score || (score == best_score && power > best_power)) {
                best_score = score; best_start = start; best_power = power;
            }
        }
        cands[idx] = (freq_cand_t){ .foff = f_off, .start = best_start,
                                     .score = best_score, .power = best_power };
    }
    free(mag);
    free(sync_bit_all);
    free(best_p_all);

    qsort(cands, n_foff, sizeof(freq_cand_t), freq_cand_cmp_desc);

    int min_score = (int)lround(min_quality_pct / 100.0 * PI4_NSYM);
    int min_sep_bins = 2 * k_bins;
    int *accepted_foff = (int *)malloc(sizeof(int) * (size_t)max_candidates);
    int n_accepted = 0;

    for (int i = 0; i < n_foff && n_accepted < max_candidates; i++) {
        if (cands[i].score < min_score || cands[i].start < 0) break;
        int too_close = 0;
        for (int j = 0; j < n_accepted; j++) {
            if (abs(cands[i].foff - accepted_foff[j]) < min_sep_bins) { too_close = 1; break; }
        }
        if (too_close) continue;

        accepted_foff[n_accepted] = cands[i].foff;
        out[n_accepted].carrier_freq_hz =
            (tone0_bin_nominal + cands[i].foff) * bin_hz + 0.5 * PI4_TONESPACING;
        out[n_accepted].dt_hint = (double)(cands[i].start * hop) / fs_in;
        out[n_accepted].quality_pct = (int)lround(100.0 * cands[i].score / PI4_NSYM);
        n_accepted++;
    }

    free(accepted_foff);
    free(cands);
    return n_accepted;
}
