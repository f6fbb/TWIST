/*
 * libpi4.c - libpi4 (libpi4.h): PI4, a thin layer over the receive
 * pipeline (twpipe.c built with TW_WITH_PI4, pi4pipe.c) and the
 * encoder (pi4enc.c).
 */

#include <stdio.h>
#include <string.h>
#include "libpi4.h"
#include "twdefs.h"
#include "twdec.h"
#include "pi4.h"
#include "pi4pipe.h"

void pi4_make_symbols(const char *msg, unsigned char *symbols);   /* pi4enc.c */

int pi4l_api_version(void) { return PI4L_API_VERSION; }
const char *pi4l_version(void) { return TW_VERSION; }
void pi4l_settings_default(twl_settings_t *s) { twdec_settings_default(s); }
pi4l_decoder *pi4l_create(void) { return (pi4l_decoder *)twdec_create(TWL_PROTO_PI4); }
void pi4l_destroy(pi4l_decoder *d) { twdec_destroy((twdec_t *)d); }
int pi4l_set_known(pi4l_decoder *d, const char *calls) { return twdec_set_known((twdec_t *)d, calls); }
int pi4l_decode(pi4l_decoder *d, const float *samples, int n, int fs,
               const twl_settings_t *s, twl_spot_t *spots, int max_spots)
{
    return twdec_decode((twdec_t *)d, samples, n, fs, s, spots, max_spots);
}
int pi4l_decode_recent(pi4l_decoder *d, const float *samples, int n, int fs,
                      const twl_settings_t *s, double lookback, twl_spot_t *spots, int max_spots)
{
    return twdec_decode_recent((twdec_t *)d, samples, n, fs, s, lookback, spots, max_spots);
}
const char *pi4l_last_error(const pi4l_decoder *d) { return twdec_error((const twdec_t *)d); }

const char *pi4l_proto_name(int proto)
{
    return proto == TWL_PROTO_PI4 ? "PI4" : "";
}

double pi4l_frame_seconds(void) { return tw_pi4_duration(); }

/* The PI4 message: call, or "/" + loc; 0, or -1 if it is empty,
 * longer than 8 or has a character out of the alphabet. */
static int pi4_message(const char *call, const char *loc, char msg[PI4_MSGLEN + 1])
{
    char m[32];
    int i, n;
    if (call && call[0]) snprintf(m, sizeof m, "%s", call);
    else if (loc && loc[0]) snprintf(m, sizeof m, "/%s", loc);
    else return -1;
    n = (int)strlen(m);
    if (n > PI4_MSGLEN) return -1;
    for (i = 0; i < n; i++) {
        char c = m[i] >= 'a' && m[i] <= 'z' ? (char)(m[i] - 32) : m[i];
        if (!strchr(PI4_ALPHABET, c) || c == '\0') return -1;
        msg[i] = c;
    }
    msg[n] = '\0';
    return 0;
}

int pi4l_frame_step(const char *call, const char *loc, twl_step_t *st)
{
    char msg[PI4_MSGLEN + 1];
    if (!st) return -1;
    memset(st, 0, sizeof *st);
    if (pi4_message(call, loc, msg) != 0) return -1;
    pi4_make_symbols(msg, st->sym);
    st->kind = TWL_STEP_FRAME;
    st->proto = TWL_PROTO_PI4;
    st->nsym = PI4_NSYM;
    st->sym_s = 2000.0 / 12000.0;
    st->spacing_hz = PI4_TONESPACING;
    st->offset_hz = PI4_TONE0;
    snprintf(st->text, sizeof st->text, "%s", msg);
    return 0;
}
