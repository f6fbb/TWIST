/*
 * wavread.c - Minimal WAV reader (RIFF/PCM), for twistd
 *
 * Parses RIFF chunks (fmt , data) without assuming a fixed 44-byte
 * header - some WAV files have extra chunks (LIST, fact, etc.) before
 * "data". Supports 8/16/24/32-bit integer PCM, mono or multi-channel
 * (downmixed by averaging).
 *
 * File positions are 64-bit (twfile.h): a RIFF file can reach 4 GiB,
 * and a "long" is 32 bits on Windows. The samples are converted block
 * by block, so no copy of the whole raw file is kept in memory.
 *
 * Build:
 *   gcc -O2 -Wall -c wavread.c
 */

#include "twfile.h"     /* first: 64-bit off_t on 32-bit POSIX systems */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "wavread.h"

#define WAV_BLOCK_FRAMES 65536  /* frames converted per fread() */

static uint32_t rd_u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16le(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* One integer PCM sample, normalized to [-1, 1]. */
static double pcm_value(const unsigned char *p, int bits)
{
    switch (bits) {
    case 8:                     /* 8-bit WAV = unsigned, centered on 128 */
        return ((double)p[0] - 128.0) / 128.0;
    case 16:
        return (double)(int16_t)(p[0] | (p[1] << 8)) / 32768.0;
    case 24: {
        int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
        if (s & 0x00800000) s |= (int32_t)0xFF000000;   /* sign extension */
        return (double)s / 8388608.0;
    }
    default:
        return (double)(int32_t)rd_u32le(p) / 2147483648.0;
    }
}

int wav_read(const char *path, wavfile_t *w)
{
    FILE *fp;
    unsigned char hdr[12];
    unsigned char chunk_hdr[8];
    unsigned char fmt_buf[40];
    int have_fmt = 0, have_data = 0;
    uint16_t audio_format = 0, channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    int64_t data_offset = 0, file_size, available;
    uint64_t data_size = 0;
    int64_t frame_count, f;
    int bytes_per_sample, frame_bytes, ch;
    unsigned char *raw = NULL;

    memset(w, 0, sizeof(*w));

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "wav_read: cannot open '%s'\n", path);
        return -1;
    }

    if (fread(hdr, 1, 12, fp) != 12 ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "wav_read: '%s' is not a valid RIFF/WAVE file\n", path);
        fclose(fp);
        return -1;
    }

    while (fread(chunk_hdr, 1, 8, fp) == 8) {
        uint32_t chunk_size = rd_u32le(chunk_hdr + 4);
        int64_t chunk_data_pos = tw_ftell64(fp);
        /* next chunk: data + padding byte when the size is odd */
        int64_t next = chunk_data_pos + (int64_t)chunk_size + (int64_t)(chunk_size & 1);

        if (chunk_data_pos < 0) break;

        if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
            uint32_t n = chunk_size < sizeof(fmt_buf) ? chunk_size : sizeof(fmt_buf);
            if (n < 16 || fread(fmt_buf, 1, n, fp) != n) break;
            audio_format    = rd_u16le(fmt_buf + 0);
            channels        = rd_u16le(fmt_buf + 2);
            sample_rate     = rd_u32le(fmt_buf + 4);
            bits_per_sample = rd_u16le(fmt_buf + 14);
            have_fmt = 1;
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_offset = chunk_data_pos;
            data_size = chunk_size;
            have_data = 1;
            if (have_fmt) break;    /* nothing after "data" is needed */
            /* otherwise "fmt " comes after "data" (unusual): keep scanning */
        }
        if (tw_fseek64(fp, next, SEEK_SET) != 0) break;
    }

    if (!have_fmt || !have_data) {
        fprintf(stderr, "wav_read: '%s' - missing fmt or data chunk\n", path);
        fclose(fp);
        return -1;
    }
    if (audio_format != 1 && audio_format != 0xFFFE) {
        fprintf(stderr, "wav_read: unsupported audio format %u (integer PCM required)\n",
                audio_format);
        fclose(fp);
        return -1;
    }
    if (bits_per_sample != 8 && bits_per_sample != 16 &&
        bits_per_sample != 24 && bits_per_sample != 32) {
        fprintf(stderr, "wav_read: unsupported bit depth %u\n", bits_per_sample);
        fclose(fp);
        return -1;
    }
    if (channels == 0) {
        fprintf(stderr, "wav_read: invalid channel count\n");
        fclose(fp);
        return -1;
    }

    bytes_per_sample = bits_per_sample / 8;
    frame_bytes = bytes_per_sample * channels;

    /* Some recorders (SDR software, beacon monitoring tools) write a
     * "data" chunk size slightly larger than what is actually present
     * in the file (header written before the recording finished,
     * truncation, etc.). We clamp to what's actually available rather
     * than failing outright. */
    file_size = tw_fsize64(fp);
    if (file_size < 0) {
        fprintf(stderr, "wav_read: cannot determine the size of '%s'\n", path);
        fclose(fp);
        return -1;
    }
    available = file_size - data_offset;
    if (available < 0) available = 0;
    if (data_size > (uint64_t)available) {
        fprintf(stderr,
                "wav_read: warning - data chunk claims %llu bytes but only "
                "%lld are available in the file; truncating.\n",
                (unsigned long long)data_size, (long long)available);
        data_size = (uint64_t)available;
    }

    frame_count = (int64_t)(data_size / (uint64_t)frame_bytes);
    if (frame_count <= 0) {
        fprintf(stderr, "wav_read: no usable audio data in '%s'\n", path);
        fclose(fp);
        return -1;
    }
    /* The decoder counts samples in a long (32 bits on Windows). */
    if (frame_count > LONG_MAX || (uint64_t)frame_count > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "wav_read: '%s' is too long (%lld samples, max %ld)\n",
                path, (long long)frame_count, (long)LONG_MAX);
        fclose(fp);
        return -1;
    }

    w->samples = (double *)malloc(sizeof(double) * (size_t)frame_count);
    raw = (unsigned char *)malloc((size_t)WAV_BLOCK_FRAMES * (size_t)frame_bytes);
    if (!w->samples || !raw) {
        fprintf(stderr, "wav_read: memory allocation failed (%lld samples)\n",
                (long long)frame_count);
        goto fail;
    }

    if (tw_fseek64(fp, data_offset, SEEK_SET) != 0) {
        fprintf(stderr, "wav_read: seek error in '%s'\n", path);
        goto fail;
    }
    for (f = 0; f < frame_count; ) {
        int64_t nb = frame_count - f, k;
        if (nb > WAV_BLOCK_FRAMES) nb = WAV_BLOCK_FRAMES;
        if (fread(raw, (size_t)frame_bytes, (size_t)nb, fp) != (size_t)nb) {
            fprintf(stderr, "wav_read: incomplete read of audio data\n");
            goto fail;
        }
        for (k = 0; k < nb; k++, f++) {
            const unsigned char *p = raw + (size_t)k * (size_t)frame_bytes;
            double acc = 0.0;
            for (ch = 0; ch < channels; ch++)
                acc += pcm_value(p + ch * bytes_per_sample, bits_per_sample);
            w->samples[f] = acc / (double)channels;
        }
    }
    free(raw);
    fclose(fp);

    w->sample_rate = (int)sample_rate;
    w->channels = (int)channels;
    w->bits_per_sample = (int)bits_per_sample;
    w->nsamples = (long)frame_count;
    return 0;

fail:
    free(raw);
    fclose(fp);
    wav_free(w);
    return -1;
}

void wav_free(wavfile_t *w)
{
    free(w->samples);
    w->samples = NULL;
    w->nsamples = 0;
}
