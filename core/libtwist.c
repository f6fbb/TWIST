/*
 * libtwist.c - libtwist (libtwist.h): the TWIST frames, a thin layer
 * over the receive pipeline (twpipe.c built with TW_WITH_TWIST) and
 * the frame encoder (twist.c).
 */

#include <stdio.h>
#include <string.h>
#include "libtwist.h"
#include "twdefs.h"
#include "twdec.h"
#include "twist.h"

int twt_api_version(void) { return TWT_API_VERSION; }
const char *twt_version(void) { return TW_VERSION; }
void twt_settings_default(twl_settings_t *s) { twdec_settings_default(s); }
twt_decoder *twt_create(void) { return (twt_decoder *)twdec_create(TWL_PROTO_TWS | TWL_PROTO_TWL); }
void twt_destroy(twt_decoder *d) { twdec_destroy((twdec_t *)d); }
int twt_set_known(twt_decoder *d, const char *calls) { return twdec_set_known((twdec_t *)d, calls); }
int twt_decode(twt_decoder *d, const float *samples, int n, int fs,
               const twl_settings_t *s, twl_spot_t *spots, int max_spots)
{
    return twdec_decode((twdec_t *)d, samples, n, fs, s, spots, max_spots);
}
int twt_decode_recent(twt_decoder *d, const float *samples, int n, int fs,
                      const twl_settings_t *s, double lookback, twl_spot_t *spots, int max_spots)
{
    return twdec_decode_recent((twdec_t *)d, samples, n, fs, s, lookback, spots, max_spots);
}
const char *twt_last_error(const twt_decoder *d) { return twdec_error((const twdec_t *)d); }

const char *twt_proto_name(int proto)
{
    switch (proto) {
    case TWL_PROTO_TWS: return "TWIST S";
    case TWL_PROTO_TWL: return "TWIST L";
    default:            return "";
    }
}

int twt_frame_chips(int proto)
{
    switch (proto) {
    case TWL_PROTO_TWS: return 2 * tw_ncoded(TW_TYPE_S) + 1;
    case TWL_PROTO_TWL: return 2 * tw_ncoded(TW_TYPE_L) + 1;
    default:            return 0;
    }
}

int twt_encode(int proto, const char *call, const char *loc, unsigned char *chips, int max_chips)
{
    unsigned char tmp[TWL_MAX_CHIPS];
    int nch = twt_frame_chips(proto), rc;
    if (!nch || !call || !chips || max_chips < nch) return -1;
    if (proto == TWL_PROTO_TWS) rc = tw_encode(TW_TYPE_S, call, NULL, tmp);
    else {
        if (!loc || !*loc) return -1;
        rc = tw_encode(TW_TYPE_L, call, loc, tmp);
    }
    if (rc != 0) return -1;
    memcpy(chips, tmp, (size_t)nch);
    return nch;
}

int twt_frame_step(int proto, const char *call, const char *loc, twl_step_t *st)
{
    int n;
    if (!st) return -1;
    memset(st, 0, sizeof *st);
    n = twt_encode(proto, call, loc, st->sym, TWL_MAX_SYMBOLS);
    if (n <= 0) return -1;
    st->kind = TWL_STEP_FRAME;
    st->proto = proto;
    st->nsym = n;
    st->sym_s = tw_tchip(0.5);
    if (proto == TWL_PROTO_TWL) snprintf(st->text, sizeof st->text, "%s %s", call, loc);
    else snprintf(st->text, sizeof st->text, "%s", call);
    return 0;
}
