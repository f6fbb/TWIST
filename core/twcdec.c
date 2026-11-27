/*
 * twcdec.c - Soft-decision decoding of a classic frame. See
 * twcdec.h.
 */

#include <stdlib.h>
#include <string.h>
#include "twcdec.h"
#include "twcall.h"
#include "twcenc.h"
#include "twctwins.h"

/* Codewords allowed in group g: group 0 carries scrambled sync bits
 * 0,1,2 (known), group 1's first bit is scrambled sync bit 3 (known).
 * Derived from TWC_PSEUDO rather than hard-coded. */
static int allowed(int g, int c)
{
    if (g == 0)
        return c == ((TWC_PSEUDO[0] << 2) | (TWC_PSEUDO[1] << 1) | TWC_PSEUDO[2]);
    if (g == 1)
        return ((c >> 2) & 1) == TWC_PSEUDO[3];
    return 1;
}

static void group_metrics(const float soft[TWC_NCODED], int g, double m[8])
{
    int c, i;
    for (c = 0; c < 8; c++) {
        double s = 0.0;
        for (i = 0; i < 7; i++) {
            float v = soft[i * TWC_NGROUPS + g];
            s += TWC_WALSH[c][i] ? v : -v;
        }
        m[c] = s;
    }
}

double twc_frame_metric(const float soft[TWC_NCODED])
{
    double total = 0.0;
    int g, c;
    for (g = 0; g < TWC_NGROUPS; g++) {
        double m[8], best = -1e300;
        group_metrics(soft, g, m);
        for (c = 0; c < 8; c++)
            if (allowed(g, c) && m[c] > best) best = m[c];
        total += best;
    }
    return total;
}

/* The classic frame's scores for the receiver's search (twsync.h,
 * tw_code_ops_t): for each frequency bin, the sum over the 17 groups
 * of the best allowed Walsh codeword correlation - the soft-decision
 * likelihood of the best frame starting there. */
static void score_row(const float *D, int nbs, float *Srow, float *acc, float *best)
{
    int g, i, c, b;
    for (g = 0; g < TWC_NGROUPS; g++) {
        const float *r[7];
        for (i = 0; i < 7; i++) r[i] = D + (i * TWC_NGROUPS + g) * nbs;
        for (b = 0; b < nbs; b++) best[b] = -1e30f;
        for (c = 0; c < 8; c++) {
            if (!allowed(g, c)) continue;
            for (b = 0; b < nbs; b++) acc[b] = 0.0f;
            for (i = 0; i < 7; i++) {
                const float *ri = r[i];
                if (TWC_WALSH[c][i]) for (b = 0; b < nbs; b++) acc[b] += ri[b];
                else                   for (b = 0; b < nbs; b++) acc[b] -= ri[b];
            }
            for (b = 0; b < nbs; b++) if (acc[b] > best[b]) best[b] = acc[b];
        }
        for (b = 0; b < nbs; b++) Srow[b] += best[b];
    }
}

const tw_code_ops_t twc_code_ops = { score_row, twc_frame_metric };

typedef struct {
    double loss;
    int n;               /* number of substitutions (0..3) */
    int g[3], c[3];
} cand_t;

static int cmp_cand(const void *a, const void *b)
{
    double la = ((const cand_t *)a)->loss, lb = ((const cand_t *)b)->loss;
    return (la > lb) - (la < lb);
}

int twc_check_frame(const twc_frame_t *fr, char call[7], uint32_t *packed)
{
    unsigned char vec[TWC_NVEC];
    int g, j;
    for (g = 0; g < TWC_NGROUPS; g++) {
        vec[3 * g]     = (unsigned char)((fr->choice[g] >> 2) & 1);
        vec[3 * g + 1] = (unsigned char)((fr->choice[g] >> 1) & 1);
        vec[3 * g + 2] = (unsigned char)(fr->choice[g] & 1);
    }
    for (j = 0; j < TWC_NVEC; j++) vec[j] ^= TWC_PSEUDO[j];
    if (twc_check_vector(vec, packed) != 0) return -1;
    if (tw_unpack_call(*packed, call) != 0) return -1;
    return 0;
}

#define MAX_SINGLES (TWC_NGROUPS * 7)
#define MAX_CANDS   (1 + MAX_SINGLES + 120 + 56)

int twc_list_frames(const float soft[TWC_NCODED], int max_frames, twc_frame_t *out)
{
    double m[TWC_NGROUPS][8];
    int best[TWC_NGROUPS];
    double metric = 0.0;
    cand_t singles[MAX_SINGLES];
    cand_t cands[MAX_CANDS];
    int ns = 0, nc = 0;
    int g, c, a, b, d, k;

    for (g = 0; g < TWC_NGROUPS; g++) {
        double bv = -1e300;
        group_metrics(soft, g, m[g]);
        best[g] = -1;
        for (c = 0; c < 8; c++)
            if (allowed(g, c) && m[g][c] > bv) { bv = m[g][c]; best[g] = c; }
        metric += bv;
    }
    for (g = 0; g < TWC_NGROUPS; g++)
        for (c = 0; c < 8; c++)
            if (allowed(g, c) && c != best[g]) {
                singles[ns].loss = m[g][best[g]] - m[g][c];
                singles[ns].n = 1;
                singles[ns].g[0] = g; singles[ns].c[0] = c;
                ns++;
            }
    qsort(singles, (size_t)ns, sizeof(cand_t), cmp_cand);

    cands[nc].loss = 0.0; cands[nc].n = 0; nc++;
    for (a = 0; a < ns; a++) cands[nc++] = singles[a];
    /* pairs among the 16 cheapest single substitutions */
    for (a = 0; a < ns && a < 16; a++)
        for (b = a + 1; b < ns && b < 16; b++) {
            if (singles[a].g[0] == singles[b].g[0]) continue;
            cands[nc].loss = singles[a].loss + singles[b].loss;
            cands[nc].n = 2;
            cands[nc].g[0] = singles[a].g[0]; cands[nc].c[0] = singles[a].c[0];
            cands[nc].g[1] = singles[b].g[0]; cands[nc].c[1] = singles[b].c[0];
            nc++;
        }
    /* triples among the 8 cheapest */
    for (a = 0; a < ns && a < 8; a++)
        for (b = a + 1; b < ns && b < 8; b++)
            for (d = b + 1; d < ns && d < 8; d++) {
                if (singles[a].g[0] == singles[b].g[0] || singles[a].g[0] == singles[d].g[0] ||
                    singles[b].g[0] == singles[d].g[0]) continue;
                cands[nc].loss = singles[a].loss + singles[b].loss + singles[d].loss;
                cands[nc].n = 3;
                cands[nc].g[0] = singles[a].g[0]; cands[nc].c[0] = singles[a].c[0];
                cands[nc].g[1] = singles[b].g[0]; cands[nc].c[1] = singles[b].c[0];
                cands[nc].g[2] = singles[d].g[0]; cands[nc].c[2] = singles[d].c[0];
                nc++;
            }
    qsort(cands, (size_t)nc, sizeof(cand_t), cmp_cand);

    for (k = 0; k < nc && k < max_frames; k++) {
        for (g = 0; g < TWC_NGROUPS; g++) out[k].choice[g] = (unsigned char)best[g];
        for (a = 0; a < cands[k].n; a++) out[k].choice[cands[k].g[a]] = (unsigned char)cands[k].c[a];
        out[k].metric = metric - cands[k].loss;
        out[k].loss = cands[k].loss;
    }
    return k;
}

int twc_decode_soft(const float soft[TWC_NCODED], int list_max,
                      char call[7], uint32_t *packed, twc_decinfo_t *info)
{
    twc_frame_t *fr;
    int n, k;

    if (list_max < 1) list_max = 1;
    fr = (twc_frame_t *)malloc(sizeof(twc_frame_t) * (size_t)list_max);
    if (!fr) return -1;
    n = twc_list_frames(soft, list_max, fr);
    for (k = 0; k < n; k++) {
        if (twc_check_frame(&fr[k], call, packed) == 0) {
            if (info) {
                info->n_tried = k + 1;
                info->rank = k;
                info->metric = fr[k].metric;
                info->loss = fr[k].loss;
            }
            free(fr);
            return 0;
        }
    }
    if (info) {
        info->n_tried = n;
        info->rank = -1;
        info->metric = n > 0 ? fr[0].metric : 0.0;
        info->loss = 0.0;
    }
    free(fr);
    return -1;
}

double twc_twin_margin(const float soft[TWC_NCODED], const twc_frame_t *fr,
                         int plausible_only, char twin_call[7])
{
    double m[TWC_NGROUPS][8];
    double best = 1e30;
    int g, t, i;

    for (g = 0; g < TWC_NGROUPS; g++) group_metrics(soft, g, m[g]);
    if (twin_call) twin_call[0] = '\0';

    for (t = 0; t < TWC_NTWINS; t++) {
        const twc_twin_t *tw = &TWC_TWINS[t];
        twc_frame_t f2 = *fr;
        char cs[7];
        uint32_t pk;
        double gap = 0.0;
        for (i = 0; i < tw->n; i++) {
            int gg = tw->g[i];
            int c1 = fr->choice[gg], c2 = c1 ^ tw->x[i];
            f2.choice[gg] = (unsigned char)c2;
            gap += m[gg][c1] - m[gg][c2];
        }
        if (!allowed(1, f2.choice[1])) continue;
        if (gap >= best) continue;
        if (twc_check_frame(&f2, cs, &pk) != 0) continue;
        if (plausible_only && !tw_call_plausible(cs)) continue;
        best = gap;
        if (twin_call) memcpy(twin_call, cs, 7);
    }
    return best;
}
