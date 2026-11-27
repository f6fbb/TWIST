/*
 * ldpc.c - see ldpc.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ldpc.h"

static inline int bget(const bvec_t *v, int i) { return (int)((v->w[i >> 6] >> (i & 63)) & 1u); }
static inline void bset(bvec_t *v, int i) { v->w[i >> 6] |= (uint64_t)1 << (i & 63); }
static inline void bxor(bvec_t *a, const bvec_t *b) { int j; for (j = 0; j < LDPC_W; j++) a->w[j] ^= b->w[j]; }

static uint32_t rng_state;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

/* ---------------- PEG construction ---------------- */

static int peg(ldpc_t *c)
{
    int n = c->n, m = c->m, j, e;
    static int dist[2 * LDPC_MAXN], queue[2 * LDPC_MAXN];
    memset(c->ncheck_deg, 0, sizeof c->ncheck_deg);
    memset(c->nvar_deg, 0, sizeof c->nvar_deg);
    for (j = 0; j < n; j++) {
        for (e = 0; e < c->dv; e++) {
            int best = -1, bestdeg = 1 << 30, nb = 0, i;
            static int cands[LDPC_MAXN];
            if (e == 0) {
                for (i = 0; i < m; i++) {
                    if (c->ncheck_deg[i] < bestdeg) { bestdeg = c->ncheck_deg[i]; nb = 0; }
                    if (c->ncheck_deg[i] == bestdeg) cands[nb++] = i;
                }
            } else {
                /* BFS from variable j: nodes 0..n-1 variables, n..n+m-1 checks */
                int qh = 0, qt = 0, maxd = -1, unreached = 0;
                for (i = 0; i < n + m; i++) dist[i] = -1;
                dist[j] = 0; queue[qt++] = j;
                while (qh < qt) {
                    int u = queue[qh++], a;
                    if (u < n) {
                        for (a = 0; a < c->nvar_deg[u]; a++) {
                            int ch = n + c->var_check[u][a];
                            if (dist[ch] < 0) { dist[ch] = dist[u] + 1; queue[qt++] = ch; }
                        }
                    } else {
                        int ci = u - n;
                        for (a = 0; a < c->ncheck_deg[ci]; a++) {
                            int v = c->check_var[ci][a];
                            if (dist[v] < 0) { dist[v] = dist[u] + 1; queue[qt++] = v; }
                        }
                    }
                }
                for (i = 0; i < m; i++) if (dist[n + i] < 0) unreached = 1;
                if (!unreached) for (i = 0; i < m; i++) if (dist[n + i] > maxd) maxd = dist[n + i];
                for (i = 0; i < m; i++) {
                    int ok = unreached ? (dist[n + i] < 0) : (dist[n + i] == maxd);
                    if (!ok) continue;
                    if (c->ncheck_deg[i] >= 31) continue;
                    if (c->ncheck_deg[i] < bestdeg) { bestdeg = c->ncheck_deg[i]; nb = 0; }
                    if (c->ncheck_deg[i] == bestdeg) cands[nb++] = i;
                }
                if (nb == 0) {   /* fallback: any check not already connected */
                    for (i = 0; i < m; i++) {
                        int a, dup = 0;
                        for (a = 0; a < c->nvar_deg[j]; a++) if (c->var_check[j][a] == i) dup = 1;
                        if (dup || c->ncheck_deg[i] >= 31) continue;
                        if (c->ncheck_deg[i] < bestdeg) { bestdeg = c->ncheck_deg[i]; nb = 0; }
                        if (c->ncheck_deg[i] == bestdeg) cands[nb++] = i;
                    }
                }
            }
            if (nb == 0) return -1;
            best = cands[rnd() % (uint32_t)nb];
            c->var_check[j][c->nvar_deg[j]++] = best;
            c->check_var[best][c->ncheck_deg[best]++] = j;
        }
    }
    return 0;
}

/* girth (shortest cycle length) by BFS from each variable */
static int girth_of(const ldpc_t *c)
{
    int n = c->n, m = c->m, j, g = 1 << 30;
    static int dist[2 * LDPC_MAXN], par[2 * LDPC_MAXN], queue[2 * LDPC_MAXN];
    for (j = 0; j < n; j++) {
        int qh = 0, qt = 0, i;
        for (i = 0; i < n + m; i++) { dist[i] = -1; par[i] = -1; }
        dist[j] = 0; queue[qt++] = j;
        while (qh < qt) {
            int u = queue[qh++], a, deg;
            deg = u < n ? c->nvar_deg[u] : c->ncheck_deg[u - n];
            for (a = 0; a < deg; a++) {
                int v = u < n ? n + c->var_check[u][a] : c->check_var[u - n][a];
                if (v == par[u]) continue;
                if (dist[v] < 0) { dist[v] = dist[u] + 1; par[v] = u; queue[qt++] = v; }
                else if (dist[v] + dist[u] + 1 < g) g = dist[v] + dist[u] + 1;
            }
        }
    }
    return g;
}

int ldpc_build(ldpc_t *c, int n, int k, int dv, uint32_t seed)
{
    int attempt;
    if (n > LDPC_MAXN || k >= n) return -1;
    for (attempt = 0; attempt < 50; attempt++) {
        static bvec_t H[LDPC_MAXN];
        int m = n - k, r, col, rank = 0, i, j, a;
        static int pivcol[LDPC_MAXN], isfree[LDPC_MAXN];
        memset(c, 0, sizeof *c);
        c->n = n; c->k = k; c->m = m; c->dv = dv;
        rng_state = seed * 2654435761u + 12345u + (uint32_t)attempt * 7919u;
        if (!rng_state) rng_state = 1;
        if (peg(c) != 0) continue;
        /* dense H, RREF */
        for (i = 0; i < m; i++) {
            memset(&H[i], 0, sizeof H[i]);
            for (a = 0; a < c->ncheck_deg[i]; a++) bset(&H[i], c->check_var[i][a]);
        }
        for (col = 0; col < n && rank < m; col++) {
            int p = -1;
            for (r = rank; r < m; r++) if (bget(&H[r], col)) { p = r; break; }
            if (p < 0) continue;
            if (p != rank) { bvec_t t = H[p]; H[p] = H[rank]; H[rank] = t; }
            for (r = 0; r < m; r++) if (r != rank && bget(&H[r], col)) bxor(&H[r], &H[rank]);
            pivcol[rank++] = col;
        }
        if (rank < m) continue;      /* rank deficient: try another seed */
        for (j = 0; j < n; j++) isfree[j] = 1;
        for (r = 0; r < m; r++) isfree[pivcol[r]] = 0;
        /* generator rows: one per free column */
        i = 0;
        for (j = 0; j < n; j++) {
            if (!isfree[j]) continue;
            memset(&c->G[i], 0, sizeof c->G[i]);
            bset(&c->G[i], j);
            for (r = 0; r < m; r++) if (bget(&H[r], j)) bset(&c->G[i], pivcol[r]);
            c->pivot[i] = j;
            i++;
        }
        c->girth = girth_of(c);
        return 0;
    }
    return -1;
}

int ldpc_load(ldpc_t *c, int n, int k, const unsigned char *check_deg, const unsigned char *check_var,
              const unsigned long long (*G)[3], const unsigned char *pivot)
{
    int i, a, e = 0, j;
    if (n > LDPC_MAXN || k >= n || (n + 63) / 64 > 3) return -1;
    memset(c, 0, sizeof *c);
    c->n = n; c->k = k; c->m = n - k; c->dv = 3;
    for (i = 0; i < c->m; i++) {
        c->ncheck_deg[i] = check_deg[i];
        for (a = 0; a < check_deg[i]; a++, e++) {
            int v = check_var[e];
            c->check_var[i][a] = v;
            if (c->nvar_deg[v] >= 8) return -1;
            c->var_check[v][c->nvar_deg[v]++] = i;
        }
    }
    for (i = 0; i < k; i++) {
        for (j = 0; j < 3; j++) c->G[i].w[j] = G[i][j];
        c->pivot[i] = pivot[i];
    }
    c->girth = 0;
    return 0;
}

void ldpc_encode(const ldpc_t *c, const unsigned char *msg, unsigned char *cw)
{
    bvec_t v; int i;
    memset(&v, 0, sizeof v);
    for (i = 0; i < c->k; i++) if (msg[i]) bxor(&v, &c->G[i]);
    for (i = 0; i < c->n; i++) cw[i] = (unsigned char)bget(&v, i);
}

int ldpc_syndrome_weight(const ldpc_t *c, const unsigned char *cw)
{
    int i, a, w = 0;
    for (i = 0; i < c->m; i++) {
        int s = 0;
        for (a = 0; a < c->ncheck_deg[i]; a++) s ^= cw[c->check_var[i][a]];
        w += s;
    }
    return w;
}

/* ---------------- BP (sum-product) ---------------- */

static int bp(const ldpc_t *c, const float *llr, int iters, unsigned char *cw)
{
    /* internal convention L = log P(0)/P(1) = -llr */
    static double v2c[LDPC_MAXN][32], c2v[LDPC_MAXN][32];
    int n = c->n, m = c->m, it, i, a, j;
    double tot[LDPC_MAXN];
    for (i = 0; i < m; i++)
        for (a = 0; a < c->ncheck_deg[i]; a++) { v2c[i][a] = -llr[c->check_var[i][a]]; c2v[i][a] = 0; }
    for (it = 0; it < iters; it++) {
        /* check update */
        for (i = 0; i < m; i++) {
            int d = c->ncheck_deg[i];
            double t[32], prod = 1.0;
            int nz = 0;
            for (a = 0; a < d; a++) {
                double x = v2c[i][a];
                if (x > 30) x = 30;
                if (x < -30) x = -30;
                t[a] = tanh(0.5 * x);
                if (fabs(t[a]) < 1e-12) { t[a] = 1e-12; nz++; }
                prod *= t[a];
            }
            for (a = 0; a < d; a++) {
                double q = prod / t[a];
                if (q > 0.999999) q = 0.999999;
                if (q < -0.999999) q = -0.999999;
                c2v[i][a] = 2.0 * atanh(q);
            }
            (void)nz;
        }
        /* variable totals */
        for (j = 0; j < n; j++) tot[j] = -llr[j];
        for (i = 0; i < m; i++)
            for (a = 0; a < c->ncheck_deg[i]; a++) tot[c->check_var[i][a]] += c2v[i][a];
        for (j = 0; j < n; j++) cw[j] = tot[j] < 0;
        if (ldpc_syndrome_weight(c, cw) == 0) return it + 1;
        /* variable update */
        for (i = 0; i < m; i++)
            for (a = 0; a < c->ncheck_deg[i]; a++)
                v2c[i][a] = tot[c->check_var[i][a]] - c2v[i][a];
    }
    return 0;
}

/* ---------------- OSD ---------------- */

/* Index of the lowest set bit of a nonzero word. */
#if defined(_MSC_VER)
#include <intrin.h>
static int lowest_bit(uint64_t x) { unsigned long i; _BitScanForward64(&i, x); return (int)i; }
#else
static int lowest_bit(uint64_t x) { return __builtin_ctzll(x); }
#endif

/* Sum of |LLR| over the positions where the codeword differs from the
 * hard decisions. Jumps from one differing bit to the next (called a
 * few hundred thousand times per pass by the OSD): same additions in
 * the same order as a bit-by-bit scan, so the same result. */
static double discrepancy(const bvec_t *cwv, const bvec_t *hard, const float *absl, int n)
{
    double d = 0.0; int j;
    for (j = 0; j < LDPC_W; j++) {
        uint64_t x = cwv->w[j] ^ hard->w[j];
        while (x) {
            int pos = j * 64 + lowest_bit(x);
            if (pos < n) d += absl[pos];
            x &= x - 1;
        }
    }
    return d;
}

static const float *g_absl;
static int cmp_rel(const void *a, const void *b)
{
    float x = g_absl[*(const int *)a], y = g_absl[*(const int *)b];
    return (x < y) - (x > y);
}

/* Ordered-statistics decoding: the codeword nearest to the received
 * soft values among the reprocessed ones. Returns its discrepancy, and
 * in *second that of the next nearest one: noise gives a nearest
 * codeword barely better than many others, a real frame one that
 * stands out (ldpc_decode's margin). */
static double osd(const ldpc_t *c, const float *llr, int order, int k2, bvec_t *best, double *second)
{
    double d2 = 1e30;
    int n = c->n, k = c->k, i, j, r, npiv = 0;
    static bvec_t G[LDPC_MAXN];
    int order_idx[LDPC_MAXN], mrb[LDPC_MAXN];
    float absl[LDPC_MAXN];
    bvec_t hard, c0, cur;
    double dbest, d;
    memset(&hard, 0, sizeof hard);
    for (j = 0; j < n; j++) { absl[j] = fabsf(llr[j]); order_idx[j] = j; if (llr[j] > 0) bset(&hard, j); }
    g_absl = absl;
    qsort(order_idx, (size_t)n, sizeof(int), cmp_rel);
    memcpy(G, c->G, sizeof(bvec_t) * (size_t)k);
    for (i = 0; i < n && npiv < k; i++) {
        int col = order_idx[i], p = -1;
        for (r = npiv; r < k; r++) if (bget(&G[r], col)) { p = r; break; }
        if (p < 0) continue;
        if (p != npiv) { bvec_t t = G[p]; G[p] = G[npiv]; G[npiv] = t; }
        for (r = 0; r < k; r++) if (r != npiv && bget(&G[r], col)) bxor(&G[r], &G[npiv]);
        mrb[npiv++] = col;
    }
    /* order 0 */
    memset(&c0, 0, sizeof c0);
    for (i = 0; i < k; i++) if (bget(&hard, mrb[i])) bxor(&c0, &G[i]);
    *best = c0; dbest = discrepancy(&c0, &hard, absl, n);
    if (order >= 1) {
        for (i = 0; i < k; i++) {
            cur = c0; bxor(&cur, &G[i]);
            d = discrepancy(&cur, &hard, absl, n);
            if (d < dbest) { d2 = dbest; dbest = d; *best = cur; } else if (d < d2) d2 = d;
        }
    }
    if (order >= 2) {
        int lo = (k2 > 0 && k2 < k) ? k - k2 : 0, a, b;
        for (a = lo; a < k; a++)
            for (b = a + 1; b < k; b++) {
                cur = c0; bxor(&cur, &G[a]); bxor(&cur, &G[b]);
                d = discrepancy(&cur, &hard, absl, n);
                if (d < dbest) { d2 = dbest; dbest = d; *best = cur; } else if (d < d2) d2 = d;
            }
    }
    *second = d2;
    return dbest;
}

int ldpc_decode(const ldpc_t *c, const float *llr, int bp_iter, int osd_order, int osd_k2,
                unsigned char *cw, unsigned char *msg, double *disc, double *margin, int *how)
{
    int i, j;
    double d1 = 0.0, d2 = 1e30;
    float absl[LDPC_MAXN];
    bvec_t hard, cv;
    for (j = 0; j < c->n; j++) absl[j] = fabsf(llr[j]);
    if (bp_iter > 0 && bp(c, llr, bp_iter, cw) > 0) {
        *how = 1;
    } else {
        if (osd_order < 0) return -1;
        d1 = osd(c, llr, osd_order, osd_k2, &cv, &d2);
        for (j = 0; j < c->n; j++) cw[j] = (unsigned char)bget(&cv, j);
        *how = 2;
    }
    memset(&hard, 0, sizeof hard);
    memset(&cv, 0, sizeof cv);
    for (j = 0; j < c->n; j++) { if (llr[j] > 0) bset(&hard, j); if (cw[j]) bset(&cv, j); }
    *disc = discrepancy(&cv, &hard, absl, c->n);
    if (margin) *margin = *how == 2 ? d2 - d1 : 1e30;
    for (i = 0; i < c->k; i++) msg[i] = cw[c->pivot[i]];
    return 0;
}
