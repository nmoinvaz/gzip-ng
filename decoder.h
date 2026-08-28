/* decoder.h -- incremental decoder for one independent block
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_DECODER_H_
#define GZNG_DECODER_H_

#include <stddef.h>
#include <stdint.h>

#include "zlib-ng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How one piece of a block ended, see decoder_feed(). */
enum { SEG_FULL, SEG_END, SEG_SHORT, SEG_OVERFLOW, SEG_ERROR };

/* Incremental decoder for one independent block, fed one piece of input at a time. */
typedef struct {
    zng_stream *strm;
    int32_t want_marker;    /* output complete, the trailing empty stored block is still to come */
    int32_t accept_partial; /* the input ends at a marker pair, so any clean output size is a block */
} decoder;

void decoder_init(decoder *dec, zng_stream *strm, uint8_t *out, uint32_t block_size);
int32_t decoder_feed(decoder *dec, const uint8_t *in, size_t in_len, size_t *used);
const char *decoder_status_name(int32_t status);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_DECODER_H_ */
