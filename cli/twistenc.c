/*
 * twistenc.c - TWIST frame encoder (reference for beacon firmware and
 * test tool).
 *
 *   twistenc CALL            TWIST short frame (callsign)
 *   twistenc CALL LOCATOR    TWIST long frame (callsign + 4/6-char locator)
 *
 * Prints the chip sequence (1 = carrier ON, 0 = OFF; one chip lasts
 * 0.128 s in Op05), as a 0/1 string and packed in hex (MSB first,
 * first chip = MSB of the first byte, last byte padded with 0), and
 * the frame's message bits, CRC and codeword - everything a beacon
 * implementation can be checked against.
 *
 *   twistenc -w FILE.wav [-f HZ] [-r RATE] CALL [LOCATOR]
 *
 *   twistenc -p pi4 [-w FILE.wav [-f HZ] [-r RATE]] MESSAGE
 *
 * PI4 (since 1.8.0): MESSAGE is a callsign or "/" + locator (8
 * characters at most); prints the 146 symbols (0 to 3) and writes the
 * 4-FSK audio (-f: the PI4 nominal frequency, default 800 Hz).
 *
 * also writes the frame as audio (16-bit mono WAV): the keyed tone the
 * application sends to the transceiver (core/twtx.h, 5 ms ramps), with
 * 1 s of silence before and after - to play into a transmitter, or to
 * decode with twistd.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twdefs.h"
#include "twist.h"
#include "twtx.h"
#include "pi4.h"

void pi4_make_symbols(const char *msg, unsigned char *symbols);   /* pi4enc.c */

static void wr16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void wr32(FILE *f, unsigned long v) { wr16(f, v & 0xFFFF); wr16(f, (v >> 16) & 0xFFFF); }

/* The frame as a 16-bit mono WAV: 1 s silence, frame, 1 s silence. */
static int write_wav(const char *path, int type, const char *call, const char *loc,
                     double tone, int fs, int pi4)
{
    twl_step_t steps[3];
    twl_tx_settings_t st = { fs, tone, 0.5, 0.005 };
    twtx_t *tx = twtx_create();
    float buf[4096];
    unsigned long n = 0;
    int k, m;
    FILE *f;

    memset(steps, 0, sizeof steps);
    steps[0].kind = TWL_STEP_PAUSE; steps[0].seconds = 1.0;
    /* the frame's symbols, as libtwist and libpi4 give them (twt_frame_step,
     * pi4l_frame_step) */
    steps[1].kind = TWL_STEP_FRAME;
    if (pi4) {
        char msg[PI4_MSGLEN + 1];
        for (k = 0; call[k] && k < PI4_MSGLEN; k++)
            msg[k] = call[k] >= 'a' && call[k] <= 'z' ? (char)(call[k] - 32) : call[k];
        msg[k] = '\0';
        pi4_make_symbols(msg, steps[1].sym);
        steps[1].proto = TWL_PROTO_PI4;
        steps[1].nsym = PI4_NSYM;
        steps[1].sym_s = 1.0 / 6.0;
        steps[1].spacing_hz = PI4_TONESPACING;
        steps[1].offset_hz = PI4_TONE0;
    } else {
        if (tw_encode(type, call, loc, steps[1].sym) != 0) { twtx_destroy(tx); return -1; }
        steps[1].proto = type == TW_TYPE_L ? TWL_PROTO_TWL : TWL_PROTO_TWS;
        steps[1].nsym = 2 * tw_ncoded(type) + 1;
        steps[1].sym_s = tw_tchip(0.5);
    }
    steps[2].kind = TWL_STEP_PAUSE; steps[2].seconds = 1.0;
    if (!tx || twtx_start(tx, &st, steps, 3) != 0) {
        fprintf(stderr, "twistenc: %s\n", tx ? twtx_error(tx) : "out of memory");
        twtx_destroy(tx);
        return -1;
    }
    f = fopen(path, "wb");
    if (!f) { perror(path); twtx_destroy(tx); return -1; }
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
    wr32(f, 16); wr16(f, 1); wr16(f, 1); wr32(f, (unsigned long)fs); wr32(f, 2UL * (unsigned long)fs);
    wr16(f, 2); wr16(f, 16);
    fwrite("data\0\0\0\0", 1, 8, f);
    while ((m = twtx_render(tx, buf, 4096)) > 0) {
        for (k = 0; k < m; k++) wr16(f, (unsigned)(int)lrintf(buf[k] * 32767.0f) & 0xFFFF);
        n += (unsigned long)m;
    }
    fseek(f, 4, SEEK_SET); wr32(f, 36 + 2 * n);
    fseek(f, 40, SEEK_SET); wr32(f, 2 * n);
    twtx_destroy(tx);
    if (fclose(f) != 0) { perror(path); return -1; }
    printf("wav      : %s, %d Hz, tone %.1f Hz, %.1f s\n", path, fs, tone, (double)n / fs);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char chips[TW_MAXCHIPS];
    const char *prog = argv[0], *wav = NULL;
    double tone = 0.0;
    int type, nch, i, fs = 48000, pi4 = 0;

    while (argc > 2 && argv[1][0] == '-' && argv[1][1] && !argv[1][2] && strchr("wfrp", argv[1][1])) {
        if (argv[1][1] == 'w') wav = argv[2];
        else if (argv[1][1] == 'f') tone = atof(argv[2]);
        else if (argv[1][1] == 'p') {
            if (strcmp(argv[2], "pi4") != 0) { fprintf(stderr, "%s: -p pi4 only\n", prog); return 2; }
            pi4 = 1;
        }
        else fs = atoi(argv[2]);
        argc -= 2; argv += 2;
    }
    if (pi4) {
        unsigned char sym[PI4_NSYM];
        char msg[PI4_MSGLEN + 1];
        if (argc != 2) { fprintf(stderr, "usage: %s -p pi4 [-w FILE.wav [-f HZ] [-r RATE]] MESSAGE\n", prog); return 2; }
        for (i = 0; argv[1][i]; i++) {
            char c = argv[1][i] >= 'a' && argv[1][i] <= 'z' ? (char)(argv[1][i] - 32) : argv[1][i];
            if (i >= PI4_MSGLEN || !strchr(PI4_ALPHABET, c)) {
                fprintf(stderr, "%s: cannot encode the PI4 message %s (8 characters at most: A-Z 0-9 space /)\n", prog, argv[1]);
                return 1;
            }
            msg[i] = c;
        }
        if (i == 0) { fprintf(stderr, "%s: empty PI4 message\n", prog); return 1; }
        msg[i] = '\0';
        pi4_make_symbols(msg, sym);
        printf("frame    : PI4 \"%-8s\", %d symbols of 1/6 s, %.2f s\n", msg, PI4_NSYM, PI4_NSYM / 6.0);
        printf("symbols  : ");
        for (i = 0; i < PI4_NSYM; i++) putchar('0' + sym[i]);
        printf("\n");
        if (wav && write_wav(wav, 0, argv[1], NULL, tone > 0 ? tone : 800.0, fs, 1) != 0) return 1;
        return 0;
    }
    if (tone <= 0) tone = 1500.0;
    if (argc < 2 || argc > 3 || argv[1][0] == '-') {
        fprintf(stderr, "usage: %s [-w FILE.wav [-f HZ] [-r RATE]] CALL [LOCATOR]\n"
                        "       %s -p pi4 [-w FILE.wav [-f HZ] [-r RATE]] MESSAGE\n"
                        "  without LOCATOR: TWIST short frame (239 chips, 30.6 s in Op05)\n"
                        "  with LOCATOR:    TWIST long frame (321 chips, 41.1 s in Op05)\n"
                        "  -w FILE.wav      also writes the frame as audio: keyed tone, 5 ms\n"
                        "                   ramps, 1 s of silence before and after\n"
                        "  -f HZ            tone frequency (default 1500)\n"
                        "  -r RATE          sample rate (default 48000)\n"
                        "  -p pi4 MESSAGE   PI4 frame: a callsign or /LOCATOR (8 characters\n"
                        "                   at most); -f: the PI4 nominal frequency (800)\n", prog, prog);
        return 2;
    }
    type = argc == 3 ? TW_TYPE_L : TW_TYPE_S;
    if (tw_encode(type, argv[1], argc == 3 ? argv[2] : NULL, chips) != 0) {
        fprintf(stderr, "%s: cannot encode %s%s%s\n", prog, argv[1], argc == 3 ? " " : "", argc == 3 ? argv[2] : "");
        return 1;
    }
    nch = 2 * tw_ncoded(type) + 1;
    printf("frame    : TWIST %s, %d coded bits, %d chips, %.1f s in Op05\n",
           type == TW_TYPE_S ? "short (TWS)" : "long (TWL)", tw_ncoded(type), nch, nch * 0.128);
    printf("chips    : ");
    for (i = 0; i < nch; i++) putchar('0' + chips[i]);
    printf("\nhex      : ");
    for (i = 0; i < nch; i += 8) {
        int b = 0, j;
        for (j = 0; j < 8; j++) b = (b << 1) | (i + j < nch ? chips[i + j] : 0);
        printf("%02X", b);
    }
    printf("\n");
    if (wav && write_wav(wav, type, argv[1], argc == 3 ? argv[2] : NULL, tone, fs, 0) != 0) return 1;
    return 0;
}
