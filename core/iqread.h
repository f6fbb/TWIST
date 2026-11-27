#ifndef IQREAD_H
#define IQREAD_H

/*
 * iqread.h - Reader for wideband IQ capture files (SDR), for the
 * --iq / -i mode of pi4d.
 *
 * Unlike wav_read() (wavread.h), which downmixes multi-channel audio
 * to mono by averaging - destroying the frequency information carried
 * by a genuine I/Q pair - this reader keeps the two channels separate
 * (left = I, right = Q) and returns them as two synchronized arrays.
 *
 * Supports both the standard RIFF/WAVE container (32-bit chunk sizes)
 * and RF64 (EBU/BWF variant for files that may exceed 4 GiB, common
 * for SDR captures - identified by the "RF64" magic instead of "RIFF"
 * and a "ds64" chunk carrying the real 64-bit sizes). PCM 8/16/24/32-
 * bit integer AND 32-bit IEEE float, stereo only (I/Q has no meaning
 * for mono or >2 channels).
 */

typedef struct {
    int sample_rate;
    int bits_per_sample;
    double *i_samples;     /* normalized to [-1, 1], malloc()'d */
    double *q_samples;     /* normalized to [-1, 1], malloc()'d */
    long nsamples;          /* per channel */
} iqfile_t;

/* Returns: 0 on success, -1 on error (message on stderr). Requires a
 * stereo file - a clear error is printed otherwise, since I/Q capture
 * is a two-channel concept by definition. */
int iq_read(const char *path, iqfile_t *w);

/* Frees w->i_samples and w->q_samples. */
void iq_free(iqfile_t *w);

#endif /* IQREAD_H */
