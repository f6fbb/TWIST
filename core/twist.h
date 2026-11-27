#ifndef TWIST_H
#define TWIST_H
#include <stdint.h>
#include "ldpc.h"

#define TW_TYPE_S 0     /* callsign only            */
#define TW_TYPE_L 1     /* callsign + locator       */
#define TW_MAXK   128

int tw_kbits(int type);
int tw_ncoded(int type);              /* coded bits: 119 (S), 160 (L) */
const ldpc_t *tw_code(int type);      /* the frozen code of a frame type */
int tw_pack_loc(const char *loc, uint32_t *v);
int tw_unpack_loc(uint32_t v, char out[7]);
/* chips[2n+1] */
int tw_encode(int type, const char *call, const char *loc, unsigned char *chips);
/* LLR (>0 = bit 1) from the two chip energies of each coded bit; returns S estimate.
 * mode 0: exact noncoherent (ln I0), 1: linear approximation */
double tw_llr(const float *ea, const float *eb, int n, double mu, float *llr, int mode);
/* descramble + CRC + unpack; 0 if valid */
int tw_check(int type, const unsigned char *msgbits, char call[7], char loc[7]);
#endif
