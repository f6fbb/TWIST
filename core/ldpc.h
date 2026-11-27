/*
 * ldpc.h - Short binary LDPC codes for the classic "twist" bench.
 *
 * Construction: progressive edge growth (PEG, Hu-Eleftheriou-Arnold),
 * column weight dv (3 by default), checks as regular as possible,
 * deterministic (fixed seed) so that transmitter and receiver build
 * the same matrix. Encoding: generator matrix in reduced row echelon
 * form - the k message bits appear unchanged at k "pivot" positions of
 * the codeword (systematic), the others are XOR combinations.
 *
 * Decoding: belief propagation (sum-product, LLR domain), then, if it
 * does not converge, ordered statistics decoding (OSD) of order 1 or 2
 * on the most reliable basis.
 */
#ifndef LDPC_H
#define LDPC_H

#include <stdint.h>

#define LDPC_MAXN 256
#define LDPC_W    4               /* 64-bit words per bit vector */

typedef struct { uint64_t w[LDPC_W]; } bvec_t;

typedef struct {
    int n, k, m;                  /* code length, message bits, checks  */
    int dv;                       /* column weight                      */
    /* parity-check matrix, sparse */
    int ncheck_deg[LDPC_MAXN];    /* degree of each check               */
    int check_var[LDPC_MAXN][32]; /* variables of each check            */
    int nvar_deg[LDPC_MAXN];
    int var_check[LDPC_MAXN][8];  /* checks of each variable            */
    /* generator, RREF: row i has a 1 at pivot[i] and 0 at the other pivots */
    bvec_t G[LDPC_MAXN];
    int pivot[LDPC_MAXN];         /* codeword position of message bit i */
    int girth;
} ldpc_t;

/* Builds an (n, k) code. Returns 0, or -1 if H is not full rank
 * (then k is reduced... not supported: -1). */
int ldpc_build(ldpc_t *c, int n, int k, int dv, uint32_t seed);

/* Loads a frozen code (tables made by tools/mktables.c, n <= 192). */
int ldpc_load(ldpc_t *c, int n, int k, const unsigned char *check_deg, const unsigned char *check_var,
              const unsigned long long (*G)[3], const unsigned char *pivot);

/* msg[k] (0/1) -> cw[n] (0/1) */
void ldpc_encode(const ldpc_t *c, const unsigned char *msg, unsigned char *cw);

/* Soft decoding. llr[n]: > 0 means bit 0 more likely... convention
 * here: llr > 0 -> bit 1 (matches the Manchester soft value b - a,
 * positive when the second chip is ON, i.e. bit 1).
 * osd_order: -1 (BP only), 0, 1 or 2 (OSD order after a BP failure). osd_k2: for order 2, pairs among the
 * osd_k2 least reliable basis positions (0 = all).
 * Returns 0 if a codeword is produced: cw[n], msg[k], and *disc = sum of
 * |llr| over the positions where the codeword disagrees with the hard
 * decisions (0 for a perfect match), *how = 1 (BP) or 2 (OSD).
 * *margin (may be NULL): for OSD, how much farther the next nearest
 * codeword is (same units); 1e30 for BP. On noise the OSD nearest
 * codeword leads the next by little (median 0.2 to 0.4), a real frame
 * by much more - see twpipe's osd_margin.
 * Returns -1 if nothing (BP failed and osd_order < 0). */
int ldpc_decode(const ldpc_t *c, const float *llr, int bp_iter, int osd_order, int osd_k2,
                unsigned char *cw, unsigned char *msg, double *disc, double *margin, int *how);

/* Checks a hard word against H. Returns the number of unsatisfied checks. */
int ldpc_syndrome_weight(const ldpc_t *c, const unsigned char *cw);

#endif
