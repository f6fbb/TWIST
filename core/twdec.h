#ifndef TWDEC_H
#define TWDEC_H

#include "twltypes.h"

/*
 * twdec.h - The decoder behind the C API of each protocol library
 * (twt_*, twcl_*, pi4l_*): the known callsigns, the float -> double
 * conversion, the settings turned into pipeline parameters (twpipe.h),
 * the spots turned into twl_spot_t. Compiled into each library with
 * its own protocols (TW_WITH_*), like twpipe.c.
 */

typedef struct twdec twdec_t;

/* protos: the TWL_PROTO_* bits this library decodes. */
twdec_t    *twdec_create(int protos);
void        twdec_destroy(twdec_t *d);
void        twdec_settings_default(twl_settings_t *s);
int         twdec_set_known(twdec_t *d, const char *calls);
int         twdec_decode(twdec_t *d, const float *samples, int n, int fs,
                         const twl_settings_t *s, twl_spot_t *spots, int max_spots);
int         twdec_decode_recent(twdec_t *d, const float *samples, int n, int fs,
                                const twl_settings_t *s, double lookback,
                                twl_spot_t *spots, int max_spots);
const char *twdec_error(const twdec_t *d);

#endif /* TWDEC_H */
