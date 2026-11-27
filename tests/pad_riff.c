/*
 * pad_riff.c - Copies a WAV file, inserting a "JUNK" chunk of PAD_MIB
 * MiB between "fmt " and "data", so that the audio data starts beyond
 * 2 GiB. Used by run_tests.sh to check the readers past the 2 GiB
 * limit of a 32-bit "long" (Windows), without needing gigabytes of
 * memory: the decoded signal stays the small test recording.
 *
 *   usage: pad_riff IN.wav OUT.wav PAD_MIB [rf64]
 *
 *   rf64   writes an RF64 container (magic "RF64", "ds64" chunk with
 *          the 64-bit sizes, 0xFFFFFFFF in the 32-bit size fields),
 *          as SDR software does for captures over 4 GiB.
 *
 * The padding is not written: the program seeks over it, so the file
 * is sparse on Linux/macOS and costs no disk space there. On Windows
 * (NTFS) the space is allocated and filled with zeros.
 */

#include "twfile.h"
#include <stdlib.h>
#include <string.h>

static void wr32(FILE *f, uint32_t v)
{
    unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8),
                           (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    fwrite(b, 1, 4, f);
}
static void wr64(FILE *f, uint64_t v) { wr32(f, (uint32_t)v); wr32(f, (uint32_t)(v >> 32)); }
static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    unsigned char *buf, *fmt = NULL, *data = NULL;
    uint32_t fmt_len = 0, data_len = 0, pad;
    int64_t len;
    long pos;
    int rf64;

    if (argc < 4) {
        fprintf(stderr, "usage: %s IN.wav OUT.wav PAD_MIB [rf64]\n", argv[0]);
        return 2;
    }
    pad = (uint32_t)atol(argv[3]) * 1048576u;  /* < 4096 MiB: 32-bit JUNK size */
    rf64 = argc > 4 && !strcmp(argv[4], "rf64");

    in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    len = tw_fsize64(in);
    if (len < 12 || len > 64 * 1048576) { fprintf(stderr, "%s: unexpected size\n", argv[1]); return 1; }
    buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, in) != (size_t)len) { fprintf(stderr, "read error\n"); return 1; }
    fclose(in);
    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) { fprintf(stderr, "not a RIFF/WAVE file\n"); return 1; }

    for (pos = 12; pos + 8 <= (long)len; ) {
        uint32_t n = rd32(buf + pos + 4);
        if (!memcmp(buf + pos, "fmt ", 4)) { fmt = buf + pos + 8; fmt_len = n; }
        if (!memcmp(buf + pos, "data", 4)) { data = buf + pos + 8; data_len = n; }
        pos += 8 + (long)n + (long)(n & 1);
    }
    if (!fmt || !data || data + data_len > buf + len) { fprintf(stderr, "fmt or data chunk missing\n"); return 1; }

    out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }
    {
        uint64_t riff = 4 + (rf64 ? 8 + 28 : 0) + 8 + fmt_len + 8 + (uint64_t)pad + 8 + data_len;
        fwrite(rf64 ? "RF64" : "RIFF", 1, 4, out);
        wr32(out, rf64 ? 0xFFFFFFFFu : (uint32_t)riff);
        fwrite("WAVE", 1, 4, out);
        if (rf64) {       /* ds64: riffSize, dataSize, sampleCount, table length */
            fwrite("ds64", 1, 4, out); wr32(out, 28);
            wr64(out, riff); wr64(out, data_len); wr64(out, 0); wr32(out, 0);
        }
    }
    fwrite("fmt ", 1, 4, out); wr32(out, fmt_len); fwrite(fmt, 1, fmt_len, out);
    fwrite("JUNK", 1, 4, out); wr32(out, pad);
    if (tw_fseek64(out, (int64_t)pad, SEEK_CUR) != 0) { fprintf(stderr, "seek error\n"); return 1; }
    fwrite("data", 1, 4, out); wr32(out, rf64 ? 0xFFFFFFFFu : data_len);
    if (fwrite(data, 1, data_len, out) != data_len || fclose(out) != 0) {
        fprintf(stderr, "%s: write error (disk full?)\n", argv[2]);
        remove(argv[2]);
        return 1;
    }
    printf("%s: data at offset %lld, %u bytes%s\n", argv[2],
           (long long)(12 + (rf64 ? 36 : 0) + 8 + fmt_len + 8 + (int64_t)pad + 8), data_len, rf64 ? " (RF64)" : "");
    free(buf);
    return 0;
}
