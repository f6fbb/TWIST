/*
 * twcl.c - The classic frame's library (twcl.h): a thin layer over the
 * receive pipeline (twpipe.c built with TW_WITH_CLASSIC) and the frame
 * encoder (twcenc.c).
 */

#include <stdio.h>
#include <string.h>
#include "twcl.h"
#include "twdefs.h"
#include "twdec.h"
#include "twcenc.h"
#include "twnames.h"

int twcl_api_version(void) { return TWCL_API_VERSION; }
const char *twcl_version(void) { return TW_VERSION; }
void twcl_settings_default(twl_settings_t *s) { twdec_settings_default(s); }
twcl_decoder *twcl_create(void) { return (twcl_decoder *)twdec_create(TWL_PROTO_CLASSIC); }
void twcl_destroy(twcl_decoder *d) { twdec_destroy((twdec_t *)d); }
int twcl_set_known(twcl_decoder *d, const char *calls) { return twdec_set_known((twdec_t *)d, calls); }
int twcl_decode(twcl_decoder *d, const float *samples, int n, int fs,
               const twl_settings_t *s, twl_spot_t *spots, int max_spots)
{
    return twdec_decode((twdec_t *)d, samples, n, fs, s, spots, max_spots);
}
int twcl_decode_recent(twcl_decoder *d, const float *samples, int n, int fs,
                      const twl_settings_t *s, double lookback, twl_spot_t *spots, int max_spots)
{
    return twdec_decode_recent((twdec_t *)d, samples, n, fs, s, lookback, spots, max_spots);
}
const char *twcl_last_error(const twcl_decoder *d) { return twdec_error((const twdec_t *)d); }

const char *twcl_proto_name(int proto)
{
    return proto == TWL_PROTO_CLASSIC ? TW_CLASSIC_NAME " 05" : "";
}

int twcl_encode(const char *call, unsigned char *chips, int max_chips)
{
    unsigned char tmp[TWC_NCHIPS];
    if (!call || !chips || max_chips < TWC_NCHIPS) return -1;
    if (twc_encode(call, tmp, NULL) != 0) return -1;
    memcpy(chips, tmp, TWC_NCHIPS);
    return TWC_NCHIPS;
}

int twcl_frame_step(const char *call, twl_step_t *st)
{
    if (!st) return -1;
    memset(st, 0, sizeof *st);
    if (twcl_encode(call, st->sym, TWL_MAX_SYMBOLS) != TWC_NCHIPS) return -1;
    st->kind = TWL_STEP_FRAME;
    st->proto = TWL_PROTO_CLASSIC;
    st->nsym = TWC_NCHIPS;
    st->sym_s = tw_tchip(0.5);
    snprintf(st->text, sizeof st->text, "%s", call);
    return 0;
}
