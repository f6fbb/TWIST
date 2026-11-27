#ifndef PI4METTAB_H
#define PI4METTAB_H

/* Fills mettab[2][256] (C index i <-> Fortran index i-128) and
 * computes ndelta, the search-margin parameter used by fano() (see
 * fano.h: int fano(..., int mettab[2][256], int delta, ...)). */
void pi4_getmet(int mettab[2][256], int *ndelta);

#endif /* PI4METTAB_H */
