/*
 * twfile.h - 64-bit file positions, portable (Linux, macOS, Windows).
 *
 * fseek() and ftell() take and return a "long". On Windows "long" is
 * 32 bits even in 64-bit builds (LLP64 model), so any position past
 * 2 GiB is unreachable: ftell() returns -1 and fseek() with a size
 * cast to long jumps backwards. Recordings from an SDR (I/Q) easily
 * exceed 2 GiB, hence these two wrappers, which carry positions in an
 * int64_t everywhere:
 *
 *   Windows (MSVC, MinGW/MSYS2): _fseeki64 / _ftelli64
 *   POSIX:                       fseeko / ftello, with a 64-bit off_t
 *
 * INCLUDE THIS HEADER FIRST in a .c file: on 32-bit POSIX systems
 * (e.g. Raspberry Pi OS 32-bit) off_t is only 64 bits if
 * _FILE_OFFSET_BITS is defined before the first system header. The
 * Makefile and CMakeLists.txt also pass it on the command line.
 */

#ifndef TWFILE_H
#define TWFILE_H

#ifndef _WIN32
#  ifndef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#  ifndef _LARGEFILE_SOURCE
#    define _LARGEFILE_SOURCE 1     /* declares fseeko/ftello even with -std=c99 */
#  endif
#endif

#include <stdio.h>
#include <stdint.h>
#ifndef _WIN32
#  include <sys/types.h>
#endif

/* Same contract as fseek(): 0 on success, nonzero on error. */
static inline int tw_fseek64(FILE *f, int64_t off, int whence)
{
#ifdef _WIN32
    return _fseeki64(f, (__int64)off, whence);
#else
    return fseeko(f, (off_t)off, whence);
#endif
}

/* Same contract as ftell(): the position, or -1 on error. */
static inline int64_t tw_ftell64(FILE *f)
{
#ifdef _WIN32
    return (int64_t)_ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}

/* File size in bytes (position restored), or -1 on error. */
static inline int64_t tw_fsize64(FILE *f)
{
    int64_t here = tw_ftell64(f), end;
    if (here < 0 || tw_fseek64(f, 0, SEEK_END) != 0) return -1;
    end = tw_ftell64(f);
    if (tw_fseek64(f, here, SEEK_SET) != 0) return -1;
    return end;
}

#endif /* TWFILE_H */
