/*
 * twpipe.c - Complete receive pipeline. See twpipe.h.
 *
 * Compiled once per library, with the protocols it holds (TW_WITH_CLASSIC,
 * TW_WITH_TWIST, TW_WITH_PI4, twpipe.h): the classic frame, the TWIST
 * frames and PI4 never need each other's code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twpipe.h"
#include "twbb.h"
#include "twsync.h"
#include "twscan.h"
#include "twcall.h"
#if TW_WITH_CLASSIC
#include "twcdec.h"
#include "twcenc.h"
#include "twcdeep.h"
#endif
#if TW_WITH_PI4
#include "pi4pipe.h"
#endif
#if TW_WITH_TWIST
#include "twist.h"
#include "ldpc.h"
#endif

void tw_params_default(tw_params_t *p)
{
    p->mode_n = 0.5;
    p->f_center = 1500.0;
    p->tol = 200.0;
    p->dt_min = -1e9;
    p->dt_max = -1.0;
    p->end_min = -1e9;
    p->end_max = -1.0;
    p->drift_max = -1.0;
    p->spread_hz = 0.0;
    p->max_missing = 0.2;
    p->z_min = 7.0;
    p->max_cand = 30;
    p->list_max = 32;
    p->plausible_only = 1;
    p->twin_margin = 10.0;
    p->osd_margin = 2.0;
    p->off_max = 0.6;
    p->known = NULL;
    p->nknown = 0;
    p->calls = NULL;
    p->ncalls = 0;
    p->known_only = 0;
    p->deep = 1;
    p->deep_fa = 1e-4;
    p->snr_avg = 1;
    p->verbose = 0;
    p->protocols = TW_PROTO_ALL;
    p->pi4_f_center = 800.0;
    p->pi4_tol = 150.0;
}

#if TW_WITH_OOK
/* Smallest baseband rate 125 * 2^k that holds the search band plus
 * the signal's own width (main lobe +/- 1/T, plus some margin for
 * the noise estimate), and gives at least 16 samples per chip. With
 * 125 Hz * 0.256 s = 32, a chip is always 32 * n * 2^k samples - an
 * exact power of two for every OpN. */
static int choose_fs_bb(double tol, double tchip, double spread)
{
    int fs = 125;
    while (fs < 2.0 * (tol + 3.0 / tchip + 0.5 * spread) + 40.0 || fs * tchip < 16.0 - 1e-9) fs *= 2;
    return fs;
}

#endif

static int cmp_spot_dt(const void *a, const void *b)
{
    double x = ((const tw_spot_t *)a)->dt, y = ((const tw_spot_t *)b)->dt;
    return (x > y) - (x < y);
}

#if TW_WITH_CLASSIC
typedef struct {
    twc_frame_t fr;
    int shift;           /* index into the shift table */
    int rank;            /* rank within its own shift  */
} pooled_t;

static int cmp_pooled(const void *a, const void *b)
{
    double x = ((const pooled_t *)a)->fr.metric, y = ((const pooled_t *)b)->fr.metric;
    return (x < y) - (x > y);
}

static void trim_call(const char in[7], char out[7])
{
    int s = 0, e = 5, k = 0;
    while (s < 6 && in[s] == ' ') s++;
    while (e >= s && in[e] == ' ') e--;
    while (s <= e) out[k++] = in[s++];
    out[k] = '\0';
}

#endif

#if TW_WITH_OOK
/* Pattern consistency of a decoded frame: how its chip energies split
 * between the ON and OFF chips of the decoded callsign's pattern, in
 * units of the noise. */
static void frame_consistency(const tw_fit_t *fit, const unsigned char *chips,
                              double *contrast, double *off_excess)
{
    double son = 0.0, soff = 0.0, mu = fit->mu > 0.0 ? fit->mu : 1e-30;
    int non = 0, noff = 0, k;
    for (k = 0; k < fit->nch; k++) {
        if (fit->e[k] < 0.0f) continue;
        if (chips[k]) { son += fit->e[k]; non++; }
        else          { soff += fit->e[k]; noff++; }
    }
    if (non == 0 || noff == 0) { *contrast = 0.0; *off_excess = 0.0; return; }
    son /= non; soff /= noff;
    *contrast = (son - soff) / mu;
    *off_excess = soff / mu - 1.0;
}

static int in_known(const tw_params_t *p, const char *call)
{
    int i;
    for (i = 0; i < p->ncalls; i++)
        if (!strcmp(p->calls[i], call)) return 1;
#if TW_WITH_CLASSIC
    for (i = 0; i < p->nknown; i++)
        if (!strcmp(p->known[i].call, call)) return 1;
#endif
    return 0;
}

/* ---------------------------------------------------------------- */
/* TWIST frames (LDPC + CRC-16): same search and refinement as classic */
/* with the frame geometry of the type and a code-agnostic metric;   */
/* decoding by belief propagation, then OSD of order 2; CRC-16,      */
/* callsign/locator validity, ITU plausibility, pattern consistency. */
/* ---------------------------------------------------------------- */

/* Frame length of a spot's protocol (s). */
/* Start window [lo, hi] for a frame of duration dur: dt_min/dt_max
 * intersected with the end window shifted by the frame duration. */
static void start_window(const tw_params_t *p, double dur, double *lo, double *hi)
{
    double end_hi = p->end_max < 0 ? 1e9 : p->end_max;
    *lo = fmax(p->dt_min, p->end_min - dur);
    *hi = fmin(p->dt_max < 0 ? 1e9 : p->dt_max, end_hi - dur);
}

static double spot_duration(const tw_spot_t *o, double tchip)
{
    int nch = TW_SHORT_CHIPS;
    (void)o;
#if TW_WITH_TWIST
    if (o->proto == TW_PROTO_S) nch = 2 * tw_ncoded(TW_TYPE_S) + 1;
    if (o->proto == TW_PROTO_L) nch = 2 * tw_ncoded(TW_TYPE_L) + 1;
#endif
#if TW_WITH_PI4
    if (o->proto == TW_PROTO_PI4) return tw_pi4_duration();
#endif
    return nch * tchip;
}
#endif /* TW_WITH_OOK */

#if TW_WITH_TWIST
static int decode_twist_fit(const tw_fit_t *fit, int type, const tw_params_t *p,
                            char call[7], char loc[7], unsigned char *chips,
                            double *contrast, double *off_excess, int *how)
{
    const ldpc_t *code = tw_code(type);
    int n = code->n, i;
    float ea[TW_MAXCODED], eb[TW_MAXCODED], llr[TW_MAXCODED];
    unsigned char cw[LDPC_MAXN], msg[LDPC_MAXN];
    double disc, margin;
    for (i = 0; i < n; i++) { ea[i] = fit->e[1 + 2 * i]; eb[i] = fit->e[2 + 2 * i]; }
    tw_llr(ea, eb, n, fit->mu, llr, 0);
    if (ldpc_decode(code, llr, 50, 2, 0, cw, msg, &disc, &margin, how) != 0) return -1;
    if (tw_check(type, msg, call, loc) != 0) return -1;
    /* OSD always gives a codeword: on noise, the nearest one barely
     * leads the others, and the CRC-16 alone let one in 65536 through
     * - many ghosts on a busy band (testers: far more than OPERA,
     * whose twin margin does the same job) */
    if (*how == 2 && p->osd_margin > 0.0 && margin < p->osd_margin) return -4;
    if (p->plausible_only) {
        char norm[7];
        if (tw_normalize_call(call, norm) != 0 || !tw_call_plausible(norm)) return -2;
    }
    if (p->known_only && !in_known(p, call)) return -2;
    chips[0] = 1;
    for (i = 0; i < n; i++) { chips[1 + 2 * i] = cw[i] ? 0 : 1; chips[2 + 2 * i] = cw[i] ? 1 : 0; }
    frame_consistency(fit, chips, contrast, off_excess);
    if (p->off_max > 0.0 && *off_excess > p->off_max + 0.05 * *contrast) return -3;
    return 0;
}

static int run_twist(const tw_bb_t *bb, const tw_spec_t *sp_in, const tw_params_t *p,
                     int type, double drift_max, tw_spot_t *spots, int nspots, int max_spots)
{
    double tchip = tw_tchip(p->mode_n);
    double dt_min, dt_max;
    const char *name = type == TW_TYPE_S ? "TWIST-S" : "TWIST-L";
    tw_spec_t sp = *sp_in;            /* shares E and mu: not freed here */
    tw_cand_t *cand, *tried;
    int ncand, c, ntried = 0, npool, nfound = 0;
    double med = 0, sig = 0, dur, snr_offset;

    tw_spec_frame(&sp, tw_ncoded(type), NULL);
    dur = sp.nch * tchip;
    start_window(p, dur, &dt_min, &dt_max);
    /* Manchester: half of the chips after the start chip are ON */
    snr_offset = p->snr_avg ? 10.0 * log10((double)(sp.ncoded + 1) / sp.nch) : 0.0;
    if (drift_max > 0.0 && p->drift_max < 0) drift_max = 2.0 * sp.df / dur;

    npool = (p->max_cand > 0 ? p->max_cand : 1) * 50;
    cand = (tw_cand_t *)malloc(sizeof(tw_cand_t) * (size_t)npool);
    tried = (tw_cand_t *)malloc(sizeof(tw_cand_t) * (size_t)npool);
    if (!cand || !tried) { free(cand); free(tried); return nspots; }
    ncand = tw_find_candidates(&sp, dt_min, dt_max, p->tol, p->max_missing,
                                  p->z_min, npool, cand, &med, &sig);
    if (p->verbose)
        fprintf(stderr, "%s score map: median %.1f, sigma %.1f; %d candidate(s) above %.1f sigma\n",
                name, med, sig, ncand < 0 ? 0 : ncand, p->z_min);

    for (c = 0; c < ncand && nspots < max_spots && ntried < p->max_cand; c++) {
        tw_fit_t fit, sh[2 * TW_SHIFT_SPAN + 1];
        int shk[2 * TW_SHIFT_SPAN + 1], nsh = 0, k, a, b, s, dup = 0, done = 0, why = -1;

        /* shadow of a spot already decoded (any protocol), or weaker
         * echo of a candidate already tried */
        for (s = 0; s < nspots; s++) {
            /* same frequency, frames overlapping by more than half of
             * the shorter one - spots of THIS frame type only: a spot
             * of another type at the same place must not hide this
             * one (if it is itself a false decode); overlapping spots
             * of different types are arbitrated at the end of run() */
            if (spots[s].proto != (type == TW_TYPE_S ? TW_PROTO_S : TW_PROTO_L)) continue;
            double fd = fabs((cand[c].f0 + bb->f_center) - spots[s].df);
            double ds = spot_duration(&spots[s], tchip);
            double ov = fmin(cand[c].t0 + dur, spots[s].dt + ds) - fmax(cand[c].t0, spots[s].dt);
            if (fd < 3.0 / tchip && ov > 0.5 * fmin(dur, ds)) { dup = 1; break; }
        }
        for (s = 0; s < ntried && !dup; s++)
            if (fabs(cand[c].f0 - tried[s].f0) < 3.0 / tchip &&
                fabs(cand[c].t0 - tried[s].t0) < dur && cand[c].z < 0.75 * tried[s].z) dup = 1;
        if (dup) continue;
        tried[ntried++] = cand[c];

        if (tw_refine(bb, &sp, &cand[c], drift_max, p->max_missing, &fit) != 0) continue;
        for (k = -TW_SHIFT_SPAN; k <= TW_SHIFT_SPAN; k++)
            if (tw_fit_shift(bb, &sp, &fit, 2 * k, p->max_missing, &sh[nsh]) == 0) shk[nsh++] = 2 * k;
        /* best-aligned first */
        for (a = 0; a < nsh; a++)
            for (b = a + 1; b < nsh; b++)
                if (sh[b].score > sh[a].score) {
                    tw_fit_t t = sh[a]; int ti = shk[a];
                    sh[a] = sh[b]; shk[a] = shk[b]; sh[b] = t; shk[b] = ti;
                }
        for (a = 0; a < nsh && !done; a++) {
            char call[7], loc[7];
            unsigned char chips[TW_MAXCHIPS];
            double con = 0, offx = 0;
            int how = 0, r = decode_twist_fit(&sh[a], type, p, call, loc, chips, &con, &offx, &how);
            if (r != 0) { if (r < why || why == -1) why = r; continue; }
            done = 1;
            {
                tw_spot_t *o = &spots[nspots];
                memset(o, 0, sizeof *o);
                memcpy(o->call, call, 7);
                memcpy(o->loc, loc, 7);
                o->proto = type == TW_TYPE_S ? TW_PROTO_S : TW_PROTO_L;
                o->dt = (double)sh[a].t_samp / bb->fs;
                o->df = sh[a].f + bb->f_center;
                o->drift = sh[a].drift;
                o->z = cand[c].z;
                o->score = sh[a].score;
                o->rank = how;            /* 1 = BP, 2 = OSD */
                o->shift = shk[a];
                o->nvalid = sh[a].nvalid;
                o->snr = tw_estimate_snr_bb(bb, &sp, &sh[a], chips, tchip) + snr_offset;
                o->contrast = con;
                o->off_excess = offx;
                for (s = 0; s < nspots; s++)
                    if (!strcmp(spots[s].call, o->call) && fabs(spots[s].dt - o->dt) < 0.5 * dur) { dup = 1; break; }
                if (p->verbose)
                    fprintf(stderr, "%s candidate %d: z=%.1f t=%.3f s f=%.2f Hz drift=%+.4f Hz/s chips=%d/%d : %s %s "
                            "(%s, SNR %.1f dB, contrast %.2f, OFF excess %.2f)%s\n",
                            name, c + 1, cand[c].z, o->dt, o->df, o->drift, o->nvalid, sp.nch, o->call, o->loc,
                            how == 1 ? "BP" : "OSD", o->snr, con, offx, dup ? " [duplicate]" : "");
                if (!dup) { nspots++; nfound++; }
            }
        }
        if (!done && p->verbose)
            fprintf(stderr, "%s candidate %d: z=%.1f t=%.3f s f=%.2f Hz : %s\n", name, c + 1, cand[c].z,
                    cand[c].t0, cand[c].f0 + bb->f_center,
                    why == -4 ? "valid CRC but OSD codeword not distinct (noise)" :
                    why == -2 ? "valid CRC but implausible callsign" :
                    why == -3 ? "valid CRC but OFF chips not empty (another signal)" : "no valid frame");
    }
    free(cand);
    free(tried);
    (void)nfound;
    return nspots;
}
#endif

#if TW_WITH_CLASSIC && TW_WITH_TWIST
/* Spots of DIFFERENT protocols at the same frequency whose frames
 * overlap by more than half of the shorter one cannot both be real
 * (one transmitter, one frame at a time): keep the one whose decoded
 * pattern explains the received chips best (largest ON/OFF contrast -
 * the true frame's), drop the other. */
static int arbitrate(tw_spot_t *spots, int nspots, double tchip)
{
    int a, b, n = 0;
    unsigned char *drop = (unsigned char *)calloc((size_t)(nspots > 0 ? nspots : 1), 1);
    if (!drop) return nspots;
    for (a = 0; a < nspots; a++)
        for (b = a + 1; b < nspots; b++) {
            double da, db, ov;
            if (drop[a] || drop[b] || spots[a].proto == spots[b].proto) continue;
            if (fabs(spots[a].df - spots[b].df) >= 3.0 / tchip) continue;
            da = spot_duration(&spots[a], tchip); db = spot_duration(&spots[b], tchip);
            ov = fmin(spots[a].dt + da, spots[b].dt + db) - fmax(spots[a].dt, spots[b].dt);
            if (ov <= 0.5 * fmin(da, db)) continue;
            if (spots[a].contrast >= spots[b].contrast) drop[b] = 1; else drop[a] = 1;
        }
    for (a = 0; a < nspots; a++) if (!drop[a]) spots[n++] = spots[a];
    free(drop);
    return n;
}
#endif

#if TW_WITH_CLASSIC
/* The classic frame on the spectrogram sp: blind decoding of the
 * candidates of its score map, then the deep search for the known
 * callsigns. Appends to spots (nspots on entry: 0). */
static int run_classic(const tw_bb_t *bb, const tw_spec_t *sp_in, const tw_params_t *p,
                       double drift_max, double dt_min, double dt_max,
                       tw_spot_t *spots, int max_spots)
{
    double tchip = tw_tchip(p->mode_n);
    double dur = twc_duration(p->mode_n);
    tw_spec_t sp = *sp_in;            /* shares E and mu: not freed here */
    tw_cand_t *cand;
    int ncand, c, nspots = 0, ntried = 0, npool;
    tw_cand_t *tried;
    double med = 0, sig = 0;
    /* classic keys the carrier during 120 of its 239 chips: the average
     * power over a frame is the carrier-on power x 120/239 (-3.0 dB). */
    double snr_offset = p->snr_avg ? 10.0 * log10(120.0 / 239.0) : 0.0;

    tw_spec_frame(&sp, TWC_NCODED, &twc_code_ops);
    /* A strong frame produces many secondary score peaks along its own
     * length (partial alignments). They are skipped below as shadows
     * of the decoded spot, but they must not use up the candidate
     * budget: collect a large pool, and count only the candidates
     * actually tried against max_cand. Otherwise a weaker frame
     * elsewhere in the file (another cycle of the same beacon) may
     * never be reached. */
    npool = (p->max_cand > 0 ? p->max_cand : 1) * 50;
    cand = (tw_cand_t *)malloc(sizeof(tw_cand_t) * (size_t)npool);
    tried = (tw_cand_t *)malloc(sizeof(tw_cand_t) * (size_t)npool);
    if (!cand || !tried) { free(cand); free(tried); return -1; }
    ncand = (p->protocols & TW_PROTO_CLASSIC) ?
            tw_find_candidates(&sp, dt_min, dt_max, p->tol, p->max_missing,
                                  p->z_min, npool, cand, &med, &sig) : 0;
    if (p->verbose && (p->protocols & TW_PROTO_CLASSIC))
        fprintf(stderr, "Classic score map: median %.1f, sigma %.1f; %d candidate(s) above %.1f sigma\n",
                med, sig, ncand < 0 ? 0 : ncand, p->z_min);

    for (c = 0; c < ncand && nspots < max_spots && ntried < p->max_cand; c++) {
        tw_fit_t fit;
        twc_decinfo_t info = { 0, -1, 0.0, 0.0 };
        char call6[7];
        int s, dup = 0, shift_ok = 0;

        /* Skip candidates that are just the shadow of a spot already
         * decoded (same frequency, overlapping in time)... */
        for (s = 0; s < nspots; s++) {
            double fd = fabs((cand[c].f0 + bb->f_center) - spots[s].df);
            if (fd < 3.0 / tchip && fabs(cand[c].t0 - spots[s].dt) < 0.9 * dur) { dup = 1; break; }
        }
        /* ...or a clearly weaker echo of a candidate already tried and
         * failed: a weak classic signal also produces lower score peaks
         * at misaligned start times within its own frame. Retrying
         * them cannot succeed where the stronger alignment failed, but
         * would multiply the CRC checks, i.e. the false-decode risk. */
        for (s = 0; s < ntried && !dup; s++)
            if (fabs(cand[c].f0 - tried[s].f0) < 3.0 / tchip &&
                fabs(cand[c].t0 - tried[s].t0) < dur && cand[c].z < 0.75 * tried[s].z) dup = 1;
        if (dup) continue;
        tried[ntried++] = cand[c];

        if (tw_refine(bb, &sp, &cand[c], drift_max, p->max_missing, &fit) != 0) continue;

        /* Explore the one-bit alignment shifts (see tw_fit_shift()),
         * pooling the most likely frames of every shift into a single
         * ranked list: the CRC is checked list_max times in total for
         * the candidate, not list_max times per shift. */
        {
            tw_fit_t sh[2 * TW_SHIFT_SPAN + 1];
            int shk[2 * TW_SHIFT_SPAN + 1];
            int nsh = 0, k, a, nf = 0, found = 0;
            int lm = p->list_max > 0 ? p->list_max : 1;
            pooled_t *pool = (pooled_t *)malloc(sizeof(pooled_t) * (size_t)lm * (2 * TW_SHIFT_SPAN + 1));
            twc_frame_t *fr = (twc_frame_t *)malloc(sizeof(twc_frame_t) * (size_t)lm);
            if (!pool || !fr) { free(pool); free(fr); continue; }

            for (k = -TW_SHIFT_SPAN; k <= TW_SHIFT_SPAN; k++)
                if (tw_fit_shift(bb, &sp, &fit, 2 * k, p->max_missing, &sh[nsh]) == 0) shk[nsh++] = 2 * k;
            for (a = 0; a < nsh; a++) {
                int nfa = twc_list_frames(sh[a].soft, lm, fr), j;
                for (j = 0; j < nfa; j++) { pool[nf].fr = fr[j]; pool[nf].shift = a; pool[nf].rank = j; nf++; }
            }
            qsort(pool, (size_t)nf, sizeof(pooled_t), cmp_pooled);
            /* Check the whole list: keep the first valid frame, but
             * refuse the candidate if a second, different callsign is
             * also valid within the same list (ambiguous). */
            info.n_tried = 0;
            for (a = 0; a < nf && a < lm; a++) {
                char cs[7];
                uint32_t pk;
                info.n_tried++;
                if (twc_check_frame(&pool[a].fr, cs, &pk) != 0) continue;
                if (p->plausible_only && !tw_call_plausible(cs)) continue;
                if (p->known_only) {
                    char tc[7];
                    trim_call(cs, tc);
                    if (!in_known(p, tc)) continue;
                }
                if (!found) {
                    char twin[7];
                    double mg = twc_twin_margin(sh[pool[a].shift].soft, &pool[a].fr,
                                                  p->plausible_only, twin);
                    if (mg < p->twin_margin) {
                        if (p->verbose)
                            fprintf(stderr, "Candidate %d: %s rejected, CRC twin %s only %.1f behind\n",
                                    c + 1, cs, twin, mg);
                        break;
                    }
                    found = 1;
                    memcpy(call6, cs, 7);
                    (void)pk;
                    fit = sh[pool[a].shift];
                    shift_ok = shk[pool[a].shift];
                    info.rank = a;
                    info.loss = pool[0].fr.metric - pool[a].fr.metric;
                } else if (strcmp(cs, call6) != 0) {
                    found = 0;
                    if (p->verbose)
                        fprintf(stderr, "Candidate %d: ambiguous (%s / %s both valid), rejected\n",
                                c + 1, call6, cs);
                    break;
                }
            }
            if (!found) info.rank = -1;
            free(pool);
            free(fr);
        }

        if (info.rank >= 0) {
            unsigned char chips[TWC_NCHIPS];
            tw_spot_t *o = &spots[nspots];
            trim_call(call6, o->call);
            o->dt = (double)fit.t_samp / bb->fs;
            o->df = fit.f + bb->f_center;
            o->drift = fit.drift;
            o->z = cand[c].z;
            o->score = fit.score;
            o->rank = info.rank;
            o->loss = info.loss;
            o->shift = shift_ok;
            o->deep = 0;
            o->proto = TW_PROTO_CLASSIC;
            o->loc[0] = '\0';
            o->nvalid = fit.nvalid;
            if (twc_encode(call6, chips, NULL) == 0) {
                o->snr = tw_estimate_snr_bb(bb, &sp, &fit, chips, tchip) + snr_offset;
                frame_consistency(&fit, chips, &o->contrast, &o->off_excess);
            } else {
                o->snr = -99.0;
                o->contrast = 0.0; o->off_excess = 0.0;
            }
            /* Pattern consistency. The OFF chips of a real classic frame
             * hold only noise (measured: excess <= 0.26 noise units
             * near the threshold, ~0 on strong signals). A frame that
             * passed the CRC on the energy of another signal - a CW
             * beacon's carrier, a birdie, a PI4 tone, all sent to the
             * decoder by the wideband scan - has its OFF chips full of
             * that signal (measured: 2 to 160 noise units). */
            if (p->off_max > 0.0 && o->off_excess > p->off_max + 0.05 * o->contrast) {
                if (p->verbose)
                    fprintf(stderr, "Candidate %d: %s rejected, OFF chips not empty (excess %.2f, "
                            "contrast %.2f, SNR %.1f dB): another signal\n",
                            c + 1, o->call, o->off_excess, o->contrast, o->snr);
                continue;
            }
            /* Same callsign, overlapping frame: the same transmission,
             * whatever the frequency - a station cannot send two frames
             * at once. (A frequency-spread signal - rain scatter - or a
             * receiver image can make the same frame decodable at
             * several frequencies.) Keep the first, i.e. strongest. */
            for (s = 0; s < nspots; s++)
                if (!strcmp(spots[s].call, o->call) && fabs(spots[s].dt - o->dt) < 0.5 * dur) { dup = 1; break; }
            if (p->verbose)
                fprintf(stderr, "Candidate %d: z=%.1f t=%.3f s f=%.2f Hz -> refined t=%.3f s "
                        "f=%.2f Hz drift=%+.4f Hz/s score=%.1f chips=%d/239 : %s (rank %d, SNR %.1f dB, "
                        "contrast %.2f, OFF excess %.2f)%s\n",
                        c + 1, cand[c].z, cand[c].t0, cand[c].f0 + bb->f_center,
                        o->dt, o->df, o->drift, fit.score, fit.nvalid, o->call, info.rank, o->snr,
                        o->contrast, o->off_excess,
                        dup ? " [duplicate]" : "");
            if (!dup) nspots++;
        } else if (p->verbose) {
            fprintf(stderr, "Candidate %d: z=%.1f t=%.3f s f=%.2f Hz -> refined t=%.3f s "
                    "f=%.2f Hz score=%.1f : no valid frame (%d tried)\n",
                    c + 1, cand[c].z, cand[c].t0, cand[c].f0 + bb->f_center,
                    (double)fit.t_samp / bb->fs, fit.f + bb->f_center, fit.score, info.n_tried);
        }
    }

    /* Deep search for the known callsigns not already decoded. */
    if (p->deep && p->nknown > 0 && nspots < max_spots && (p->protocols & TW_PROTO_CLASSIC)) {
        twc_deephit_t hits[64];
        double zth = 0.0;
        int nh = twc_deep_search(&sp, p->known, p->nknown, dt_min, dt_max, p->tol,
                                   p->max_missing, p->deep_fa, 64, hits, &zth);
        if (p->verbose)
            fprintf(stderr, "Deep search: %d known callsign(s), threshold Z=%.2f, %d hit(s)\n",
                    p->nknown, zth, nh < 0 ? 0 : nh);
        for (c = 0; c < nh && nspots < max_spots; c++) {
            const twc_known_t *kn = &p->known[hits[c].k];
            tw_fit_t fit;
            tw_spot_t *o;
            int s, dup = 0;
            for (s = 0; s < nspots; s++) {
                double fd = fabs((hits[c].f0 + bb->f_center) - spots[s].df);
                if (fd < 3.0 / tchip && fabs(hits[c].t0 - spots[s].dt) < 0.9 * dur) { dup = 1; break; }
                /* same callsign, overlapping frame: same transmission */
                if (!strcmp(spots[s].call, kn->call) && fabs(hits[c].t0 - spots[s].dt) < 0.5 * dur) { dup = 1; break; }
            }
            if (dup) {
                if (p->verbose)
                    fprintf(stderr, "Deep hit %s Z=%.1f t=%.3f s f=%.2f Hz: already decoded there\n",
                            kn->call, hits[c].z, hits[c].t0, hits[c].f0 + bb->f_center);
                continue;
            }
            if (twc_deep_refine(bb, &sp, kn, &hits[c], drift_max, p->max_missing, &fit) != 0) continue;
            o = &spots[nspots];
            memcpy(o->call, kn->call, 7);
            o->dt = (double)fit.t_samp / bb->fs;
            o->df = fit.f + bb->f_center;
            o->drift = fit.drift;
            o->z = hits[c].z;
            o->score = fit.score;
            o->rank = -1;
            o->loss = 0.0;
            o->shift = 0;
            o->nvalid = fit.nvalid;
            o->deep = 1;
            o->proto = TW_PROTO_CLASSIC;
            o->loc[0] = '\0';
            o->snr = tw_estimate_snr_bb(bb, &sp, &fit, kn->chips, tchip) + snr_offset;
            frame_consistency(&fit, kn->chips, &o->contrast, &o->off_excess);
            /* Shadow of a strong signal: a strong frame leaks into the
             * frequencies around it (the chip window's sidelobes, about
             * -20 log10(pi df T) dB at df Hz, and its keying edges), so
             * it shows as a weak copy some tens of Hz away - and the
             * matched filter of a known callsign close to the strong one
             * (F5ZAS for F5ZAL: most chips alike) finds it there
             * (testers, 1.6.2: F5ZAS reported 42 Hz under a strong
             * F5ZAL). A hit is such a copy when, next to a spot that
             * overlaps it in time (within 20 chip bandwidths), it is no
             * more than 3 dB above that leakage, and that spot's own
             * pattern, with its own timing, explains the chip energies
             * at the hit's frequency nearly as well as the hit's callsign
             * (ON/OFF contrast more than a third of the hit's). A real
             * weak station there keeps its own timing and pattern (that
             * ratio measured 7, the copies 0.5 to 2.3), and is kept. */
            {
                int shadow = -1;
                for (s = 0; s < nspots && shadow < 0; s++) {
                    unsigned char sc[TWC_NCHIPS];
                    tw_fit_t f2;
                    double fd = fabs(o->df - spots[s].df), x = M_PI * fd * tchip, c2, oe2;
                    double leak = x > 1.0 ? -20.0 * log10(x) : 0.0;
                    double ov = fmin(o->dt + dur, spots[s].dt + spot_duration(&spots[s], tchip)) - fmax(o->dt, spots[s].dt);
                    if (spots[s].proto != TW_PROTO_CLASSIC || ov < 0.5 * dur || fd > 20.0 / tchip
                        || o->snr > spots[s].snr + leak + 3.0 || twc_encode(spots[s].call, sc, NULL) != 0) continue;
                    f2 = fit;
                    f2.t_samp = lround(spots[s].dt * bb->fs);
                    f2.nch = TWC_NCHIPS;
                    if (tw_chip_energies_sp(bb, &sp, f2.t_samp, fit.f, fit.drift, f2.e) < TWC_NCHIPS / 2) continue;
                    frame_consistency(&f2, sc, &c2, &oe2);
                    if (o->contrast < 3.0 * c2) shadow = s;
                }
                if (shadow >= 0) {
                    if (p->verbose)
                        fprintf(stderr, "Deep hit %s at %.2f Hz, SNR %.1f dB: copy of %s at %.2f Hz, SNR %.1f dB\n",
                                kn->call, o->df, o->snr, spots[shadow].call, spots[shadow].df, spots[shadow].snr);
                    continue;
                }
            }
            if (p->verbose)
                fprintf(stderr, "Deep hit %s: Z=%.1f (map) -> refined t=%.3f s f=%.2f Hz drift=%+.4f Hz/s "
                        "Z=%.1f chips=%d/239 SNR %.1f dB, contrast %.2f, OFF excess %.2f\n", kn->call, hits[c].z, o->dt, o->df,
                        o->drift, fit.score, fit.nvalid, o->snr, o->contrast, o->off_excess);
            nspots++;
        }
    }

    free(cand);
    free(tried);

    return nspots;
}
#endif

#if TW_WITH_OOK
static int run(const tw_bb_t *bb, const tw_params_t *p,
               tw_spot_t *spots, int max_spots)
{
    double tchip = tw_tchip(p->mode_n);
    double dur = TW_SHORT_CHIPS * tchip;
    tw_spec_t sp;
    int nspots = 0;
    double dt_min, dt_max;
    double drift_max;

    start_window(p, dur, &dt_min, &dt_max);
    if (tw_spectrogram(bb, tchip, p->tol, p->spread_hz, &sp) != 0) {
        if (p->verbose) fprintf(stderr, "twistd: recording too short or invalid for Op%g\n", p->mode_n);
        return -1;
    }
    drift_max = p->drift_max >= 0 ? p->drift_max : 2.0 * sp.df / dur;

    if (p->verbose)
        fprintf(stderr, "Baseband: %.0f Hz around %.2f Hz, chip %.3f s = %d samples, "
                "bins %.3f Hz, %ld positions x %d bins, drift search +/-%.3f Hz/s%s\n",
                bb->fs, bb->f_center, tchip, sp.L, sp.df, sp.np, sp.nb, drift_max,
                sp.hs ? "" : "");
    if (p->verbose && sp.hs)
        fprintf(stderr, "Spread mode: energy summed over %d bins = %.1f Hz, noise rescale %.2f\n",
                2 * sp.hs + 1, (2 * sp.hs + 1) * sp.df, sp.zscale);

#if TW_WITH_CLASSIC
    if (p->protocols & TW_PROTO_CLASSIC)
        nspots = run_classic(bb, &sp, p, drift_max, dt_min, dt_max, spots, max_spots);
#endif

#if TW_WITH_TWIST
    /* TWIST frames, on the same spectrogram */
    if ((p->protocols & TW_PROTO_S) && nspots < max_spots)
        nspots = run_twist(bb, &sp, p, TW_TYPE_S, drift_max, spots, nspots, max_spots);
    if ((p->protocols & TW_PROTO_L) && nspots < max_spots)
        nspots = run_twist(bb, &sp, p, TW_TYPE_L, drift_max, spots, nspots, max_spots);
#endif

#if TW_WITH_CLASSIC && TW_WITH_TWIST
    nspots = arbitrate(spots, nspots, tchip);
#endif
    tw_spectrogram_free(&sp);
    qsort(spots, (size_t)nspots, sizeof(tw_spot_t), cmp_spot_dt);
    return nspots;
}
#endif

int tw_decode_real(const double *x, long n, int fs, const tw_params_t *p,
                      tw_spot_t *spots, int max_spots)
{
    int rc = 0;
#if TW_WITH_OOK
    if (p->protocols & TW_PROTO_ALL) {
        tw_bb_t bb;
        int fs_bb = choose_fs_bb(p->tol, tw_tchip(p->mode_n), p->spread_hz);
        if (tw_baseband_real(x, n, fs, p->f_center, fs_bb, &bb) != 0) return -1;
        rc = run(&bb, p, spots, max_spots);
        tw_baseband_free(&bb);
        if (rc < 0) return rc;
    }
#endif
#if TW_WITH_PI4
    /* PI4, in its own zone (pipeline of pi4d, core/pi4pipe.c) */
    if ((p->protocols & TW_PROTO_PI4) && rc < max_spots) {
        int np = tw_pi4_decode(x, n, fs, p, spots + rc, max_spots - rc);
        if (np < 0 && rc == 0) return -1;
        if (np > 0) { rc += np; qsort(spots, (size_t)rc, sizeof(tw_spot_t), cmp_spot_dt); }
    }
#endif
    return rc;
}

int tw_decode_iq(const double *I, const double *Q, long n, int fs,
                    const tw_params_t *p, tw_spot_t *spots, int max_spots)
{
#if !TW_WITH_OOK
    (void)I; (void)Q; (void)n; (void)fs; (void)p; (void)spots; (void)max_spots;
    return 0;
#else
    tw_bb_t bb;
    int rc, fs_bb = choose_fs_bb(p->tol, tw_tchip(p->mode_n), p->spread_hz);
    if (tw_baseband_iq(I, Q, n, fs, p->f_center, fs_bb, &bb) != 0) return -1;
    rc = run(&bb, p, spots, max_spots);
    tw_baseband_free(&bb);
    return rc;
#endif
}

/* ---------------------------------------------------------------- */
/* Wideband: scan, then decode narrow tiles around each detection    */
/* ---------------------------------------------------------------- */

#define WIDE_TILE_TOL  20.0     /* Hz, half-width of each decoding tile */

int tw_decode_wide(const double *I, const double *Q, long n, int fs,
                      const tw_params_t *p, double scan_z, int scan_max,
                      tw_spot_t *spots, int max_spots)
{
#if !TW_WITH_OOK
    (void)I; (void)Q; (void)n; (void)fs; (void)p; (void)scan_z; (void)scan_max; (void)spots; (void)max_spots;
    return 0;
#else
    double tchip = tw_tchip(p->mode_n), dur = TW_SHORT_CHIPS * tchip;
    tw_scanhit_t *hits;
    tw_spectrum_t spec;
    tw_params_t tp;
    long nbins = 0;
    int nh, h, nspots = 0, fs_bb, rc;

    hits = (tw_scanhit_t *)malloc(sizeof(tw_scanhit_t) * (size_t)(scan_max > 0 ? scan_max : 1));
    if (!hits) return -1;
    nh = tw_scan(I, Q, n, fs, tchip, p->f_center - p->tol, p->f_center + p->tol,
                    scan_z, 2.0 * WIDE_TILE_TOL * 0.75, scan_max, hits, &nbins, p->verbose);
    if (nh < 0) { free(hits); return -1; }
    if (p->verbose)
        fprintf(stderr, "Scan: %d frequenc%s above %.1f sigma\n", nh, nh == 1 ? "y" : "ies", scan_z);
    if (nh == 0) { free(hits); return 0; }

    /* every tile has the same width, so one forward FFT serves them all */
    tp = *p;
    tp.tol = WIDE_TILE_TOL + 0.5 * p->spread_hz;
    fs_bb = choose_fs_bb(tp.tol, tchip, p->spread_hz);
    rc = Q ? tw_spectrum_iq(I, Q, n, fs, fs_bb, &spec)
           : tw_spectrum_real(I, n, fs, fs_bb, &spec);
    if (rc != 0) { free(hits); return -1; }

    /* The deep search runs once per tile: share the accepted false-
     * alarm budget between the tiles. */
    tp.deep_fa = p->deep_fa / nh;

    for (h = 0; h < nh && nspots < max_spots; h++) {
        tw_bb_t bb;
        tw_spot_t ts[16];
        int nts, k, s;
        if (p->verbose)
            fprintf(stderr, "--- tile %d/%d: %.1f Hz (scan z=%.1f)\n", h + 1, nh, hits[h].freq, hits[h].z);
        tp.f_center = hits[h].freq;
        if (tw_baseband_extract(&spec, tp.f_center, &bb) != 0) continue;
        nts = run(&bb, &tp, ts, 16);
        tw_baseband_free(&bb);
        for (k = 0; k < nts && nspots < max_spots; k++) {
            int dup = -1;
            for (s = 0; s < nspots; s++)
                if (!strcmp(spots[s].call, ts[k].call) && fabs(spots[s].dt - ts[k].dt) < 0.5 * dur) { dup = s; break; }
            if (dup < 0) spots[nspots++] = ts[k];
            else if (!ts[k].deep && (spots[dup].deep || ts[k].snr > spots[dup].snr)) spots[dup] = ts[k];
        }
    }
    tw_spectrum_free(&spec);
    free(hits);
    qsort(spots, (size_t)nspots, sizeof(tw_spot_t), cmp_spot_dt);
    return nspots;
#endif
}
