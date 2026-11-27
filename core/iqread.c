/*
 * iqread.c - Reader for wideband IQ capture files (RIFF/RF64), for
 * twistd's --iq / -i mode.
 *
 * Shares its chunk-parsing structure with wavread.c but differs in
 * two essential ways:
 *   1. RF64 support (ds64 chunk for 64-bit sizes) - common for large
 *      SDR captures, which regular RIFF's 32-bit size fields cannot
 *      address beyond 4 GiB.
 *   2. The two channels are kept SEPARATE (I, Q), never downmixed -
 *      averaging them would destroy the very frequency information
 *      that makes I/Q useful in the first place.
 *
 * File positions are 64-bit (twfile.h): a "long" is 32 bits on
 * Windows, and SDR captures go past 2 GiB. The samples are converted
 * block by block, so no copy of the whole raw file is kept in memory.
 *
 * Build:
 *   gcc -O2 -Wall -c iqread.c
 */

#include "twfile.h"     /* first: 64-bit off_t on 32-bit POSIX systems */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "iqread.h"

#define IQ_BLOCK_FRAMES 65536   /* frames converted per fread() */

static uint32_t rd_u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16le(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint64_t rd_u64le(const unsigned char *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* One sample, normalized to [-1, 1]. */
static double pcm_value(const unsigned char *p, int bits, int is_float)
{
    if (is_float) {
        /* IEEE float32, already normalized: no scaling, unlike integer
         * PCM. memcpy rather than a pointer cast: p is not guaranteed
         * to be 4-byte aligned. */
        float v;
        memcpy(&v, p, sizeof(float));
        return (double)v;
    }
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

int iq_read(const char *path, iqfile_t *w)
{
    FILE *fp;
    unsigned char hdr[12];
    unsigned char chunk_hdr[8];
    unsigned char fmt_buf[40];
    int have_fmt = 0, have_data = 0, have_ds64 = 0;
    uint16_t audio_format = 0, channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    int64_t data_offset = 0, file_size, available;
    uint64_t data_size = 0;
    uint64_t ds64_data_size = 0;
    int64_t frame_count, f;
    int bytes_per_sample, frame_bytes, is_float;
    unsigned char *raw;

    memset(w, 0, sizeof(*w));

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "iq_read: cannot open '%s'\n", path);
        return -1;
    }

    if (fread(hdr, 1, 12, fp) != 12 || memcmp(hdr + 8, "WAVE", 4) != 0 ||
        (memcmp(hdr, "RF64", 4) != 0 && memcmp(hdr, "RIFF", 4) != 0)) {
        fprintf(stderr, "iq_read: '%s' is not a valid RIFF/RF64 WAVE file\n", path);
        fclose(fp);
        return -1;
    }

    while (fread(chunk_hdr, 1, 8, fp) == 8) {
        uint32_t chunk_size32 = rd_u32le(chunk_hdr + 4);
        int64_t chunk_data_pos = tw_ftell64(fp);
        /* next chunk: data + padding byte when the size is odd */
        int64_t next = chunk_data_pos + (int64_t)chunk_size32 + (int64_t)(chunk_size32 & 1);

        if (chunk_data_pos < 0) break;

        if (memcmp(chunk_hdr, "ds64", 4) == 0) {
            unsigned char ds64_buf[28];
            if (fread(ds64_buf, 1, 28, fp) == 28) {
                /* riffSize64, dataSize64, sampleCount64, tableLength */
                ds64_data_size = rd_u64le(ds64_buf + 8);
                have_ds64 = 1;
            }
        } else if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
            uint32_t n = chunk_size32 < sizeof(fmt_buf) ? chunk_size32 : sizeof(fmt_buf);
            if (n < 16 || fread(fmt_buf, 1, n, fp) != n) break;
            audio_format    = rd_u16le(fmt_buf + 0);
            channels        = rd_u16le(fmt_buf + 2);
            sample_rate     = rd_u32le(fmt_buf + 4);
            bits_per_sample = rd_u16le(fmt_buf + 14);
            have_fmt = 1;
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_offset = chunk_data_pos;
            data_size = (chunk_size32 == 0xFFFFFFFFu && have_ds64)
                            ? ds64_data_size : (uint64_t)chunk_size32;
            have_data = 1;
            if (have_fmt || chunk_size32 == 0xFFFFFFFFu) break;
            /* "fmt " after "data" (unusual but legal): keep scanning */
            next = data_offset + (int64_t)data_size + (int64_t)(data_size & 1);
        }
        if (tw_fseek64(fp, next, SEEK_SET) != 0) break;
    }

    if (!have_fmt || !have_data) {
        fprintf(stderr, "iq_read: '%s' - missing fmt or data chunk\n", path);
        fclose(fp);
        return -1;
    }
    if (audio_format != 1 && audio_format != 3 && audio_format != 0xFFFE) {
        fprintf(stderr, "iq_read: unsupported audio format %u (integer or float32 PCM required)\n",
                audio_format);
        fclose(fp);
        return -1;
    }
    is_float = (audio_format == 3);
    if (is_float && bits_per_sample != 32) {
        fprintf(stderr, "iq_read: float format requires 32-bit samples (got %u-bit)\n", bits_per_sample);
        fclose(fp);
        return -1;
    }
    if (bits_per_sample != 8 && bits_per_sample != 16 &&
        bits_per_sample != 24 && bits_per_sample != 32) {
        fprintf(stderr, "iq_read: unsupported bit depth %u\n", bits_per_sample);
        fclose(fp);
        return -1;
    }
    if (channels != 2) {
        fprintf(stderr,
                "iq_read: '%s' has %u channel(s) - I/Q capture requires exactly 2 "
                "(left=I, right=Q)\n", path, channels);
        fclose(fp);
        return -1;
    }

    bytes_per_sample = bits_per_sample / 8;
    frame_bytes = 2 * bytes_per_sample;

    /* As in wavread.c: tolerate a data-chunk size larger than what is
     * actually present (unfinished header, truncated capture, etc.). */
    file_size = tw_fsize64(fp);
    if (file_size < 0) {
        fprintf(stderr, "iq_read: cannot determine the size of '%s'\n", path);
        fclose(fp);
        return -1;
    }
    available = file_size - data_offset;
    if (available < 0) available = 0;
    if (data_size > (uint64_t)available) {
        fprintf(stderr,
                "iq_read: warning - data chunk claims %llu bytes but only "
                "%lld are available; truncating.\n",
                (unsigned long long)data_size, (long long)available);
        data_size = (uint64_t)available;
    }
    frame_count = (int64_t)(data_size / (uint64_t)frame_bytes);
    if (frame_count <= 0) {
        fprintf(stderr, "iq_read: no usable I/Q data in '%s'\n", path);
        fclose(fp);
        return -1;
    }
    /* The decoder counts samples in a long (32 bits on Windows). */
    if (frame_count > LONG_MAX || (uint64_t)frame_count > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "iq_read: '%s' is too long (%lld samples per channel, max %ld)\n",
                path, (long long)frame_count, (long)LONG_MAX);
        fclose(fp);
        return -1;
    }

    w->i_samples = (double *)malloc(sizeof(double) * (size_t)frame_count);
    w->q_samples = (double *)malloc(sizeof(double) * (size_t)frame_count);
    raw = (unsigned char *)malloc((size_t)IQ_BLOCK_FRAMES * (size_t)frame_bytes);
    if (!w->i_samples || !w->q_samples || !raw) {
        fprintf(stderr, "iq_read: memory allocation failed (%lld samples per channel)\n",
                (long long)frame_count);
        goto fail;
    }

    if (tw_fseek64(fp, data_offset, SEEK_SET) != 0) {
        fprintf(stderr, "iq_read: seek error in '%s'\n", path);
        goto fail;
    }
    for (f = 0; f < frame_count; ) {
        int64_t nb = frame_count - f, k;
        if (nb > IQ_BLOCK_FRAMES) nb = IQ_BLOCK_FRAMES;
        if (fread(raw, (size_t)frame_bytes, (size_t)nb, fp) != (size_t)nb) {
            fprintf(stderr, "iq_read: incomplete read of I/Q data\n");
            goto fail;
        }
        for (k = 0; k < nb; k++, f++) {
            const unsigned char *p = raw + (size_t)k * (size_t)frame_bytes;
            w->i_samples[f] = pcm_value(p, bits_per_sample, is_float);
            w->q_samples[f] = pcm_value(p + bytes_per_sample, bits_per_sample, is_float);
        }
    }
    free(raw);
    fclose(fp);

    w->sample_rate = (int)sample_rate;
    w->bits_per_sample = (int)bits_per_sample;
    w->nsamples = (long)frame_count;
    return 0;

fail:
    free(raw);
    fclose(fp);
    iq_free(w);
    return -1;
}

void iq_free(iqfile_t *w)
{
    free(w->i_samples);
    free(w->q_samples);
    w->i_samples = NULL;
    w->q_samples = NULL;
    w->nsamples = 0;
}
