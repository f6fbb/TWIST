#ifndef WAVREAD_H
#define WAVREAD_H

typedef struct {
    int sample_rate;
    int channels;          /* as read from the file, before downmixing */
    int bits_per_sample;
    double *samples;       /* mono, normalized to [-1, 1], allocated via malloc() */
    long nsamples;
} wavfile_t;

/* Reads a PCM WAV file (8/16/24/32-bit integer). Multi-channel files
 * are downmixed to mono by averaging the channels.
 * Returns: 0 on success, -1 on error (message on stderr). */
int wav_read(const char *path, wavfile_t *w);

/* Frees w->samples. */
void wav_free(wavfile_t *w);

#endif /* WAVREAD_H */
