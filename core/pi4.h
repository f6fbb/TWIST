/*
 * pi4.h - Constants for the PI4 mode (PharusIgnis4)
 *
 * Specification source: Bo Hansen, OZ2M
 *   https://rudius.net/oz2m/ngnb/pi4_.htm
 *
 * Direct translation of pi4.f90 (Fortran module) into a C header,
 * intended for a standalone PI4 decoding CLI in the style of
 * wsprd/jt9.
 */

#ifndef PI4_H
#define PI4_H

/* MSVC does not define M_PI in <math.h> unless _USE_MATH_DEFINES is
 * set before including it. Defining it here, guarded, avoids needing
 * that compiler flag and keeps math.h usage portable across
 * platforms (Linux/gcc already defines it, so this is a no-op there). */
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* pi4d project version - see CHANGELOG.md for history.
 * Increment on every notable change (new feature, sensitivity/
 * accuracy fix, API change between modules). */
#define PI4D_VERSION "1.6.0"

/* Build/deployment identification string, printed by `pi4d -v`. */
#define PI4D_BUILD_INFO "F6FBB 2026"

/* -------------------------------------------------------------- */
/* Message and error-correcting code sizing                        */
/* -------------------------------------------------------------- */
#define PI4_NINFO   42                  /* Useful information bits        */
#define PI4_NTAIL   31                  /* Flush bits (K-1, K=32)         */
#define PI4_NCONV   (PI4_NINFO + PI4_NTAIL)   /* = 73                      */
#define PI4_NSYM    (2 * PI4_NCONV)     /* = 146 symbols                  */

/* -------------------------------------------------------------- */
/* Source alphabet: 38 characters, 8-character message              */
/* -------------------------------------------------------------- */
#define PI4_NCHARS  38
#define PI4_MSGLEN  8
static const char PI4_ALPHABET[PI4_NCHARS + 1] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /";
/* Numeric value of a character = its position in PI4_ALPHABET */

/* -------------------------------------------------------------- */
/* Modulation parameters (Table 1 of the spec, standard PI4 case,   */
/* 1 kHz beacon spacing, K=40)                                      */
/* -------------------------------------------------------------- */
#define PI4_FS         12000.0          /* Nominal sample rate (Hz)       */
#define PI4_NSPS       2000             /* Samples per symbol             */
#define PI4_TSYM       (PI4_NSPS / PI4_FS)          /* = 0.166667 s       */
#define PI4_KFACTOR    40               /* K multiplier                   */
#define PI4_DFBIN      (PI4_FS / 2048.0)            /* = 5.859375 Hz      */
#define PI4_TONESPACING (PI4_KFACTOR * PI4_DFBIN)   /* = 234.375 Hz       */

#define PI4_TONE0      (-0.5 * PI4_TONESPACING)     /* = -117.1875 Hz     */
#define PI4_TONE1      ( 0.5 * PI4_TONESPACING)     /* =  117.1875 Hz     */
#define PI4_TONE2      ( 1.5 * PI4_TONESPACING)     /* =  351.5625 Hz     */
#define PI4_TONE3      ( 2.5 * PI4_TONESPACING)     /* =  585.9375 Hz     */
#define PI4_BW         (3.0 * PI4_TONESPACING + PI4_FS / 2000.0)  /* = 709.125 Hz */

#define PI4_MSGDUR     (PI4_NSYM * PI4_TSYM)        /* = 24.3333 s        */

/* -------------------------------------------------------------- */
/* Hadamard synchronization vector (Klaus, DJ5HG), 146 bits.        */
/* Symbol merge = PI4_SYNCVEC[n] + 2*data[n]                        */
/* -------------------------------------------------------------- */
static const unsigned char PI4_SYNCVEC[PI4_NSYM] = {
    0,0,1,0,0,1,1,1,1,0,1,0,1,0,1,0,0,1,0,0,
    0,1,0,0,0,1,1,0,0,1,1,1,1,0,0,1,1,1,1,1,
    0,0,1,1,0,1,1,1,1,0,1,0,1,1,0,1,1,0,1,0,
    0,0,0,0,1,1,1,1,1,0,1,0,1,0,0,0,0,0,1,1,
    1,1,1,0,1,0,0,1,0,0,1,0,1,0,0,0,0,1,0,0,
    1,1,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,1,1,
    0,1,1,1,0,1,1,0,1,0,1,0,1,0,0,0,0,1,1,1,
    0,0,0,0,1,1
};

#endif /* PI4_H */
