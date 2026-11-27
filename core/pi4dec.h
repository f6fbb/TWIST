#ifndef PI4DEC_H
#define PI4DEC_H

#include "pi4.h"

/*
 * pi4_decode_message() - Deinterleaves, decodes (Fano), and recovers
 * the text message from the raw symbol metrics.
 *
 *   sym         : PI4_NSYM metrics, in transmission order (output of
 *                 pi4_symbol_metrics)
 *   msg_out     : output buffer, PI4_MSGLEN+1 bytes (message + '\0')
 *   fano_metric : if not NULL, receives the final Fano path metric
 *                 (a quality indicator, not yet calibrated into a
 *                 confidence unit)
 *
 * Returns: 0 if the Fano decoder finished without timing out, -1
 * otherwise. NOTE: the absence of a timeout does not guarantee a
 * correct message on a very noisy signal - a checksum/plausibility
 * check remains to be added for production use.
 */
int pi4_decode_message(const float sym[PI4_NSYM], char msg_out[PI4_MSGLEN + 1],
                        long *fano_metric);

#endif /* PI4DEC_H */
