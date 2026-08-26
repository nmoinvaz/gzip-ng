/* blockdec.h -- incremental decoder for one independent block
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_BLOCKDEC_H_
#define GZNG_BLOCKDEC_H_

#include <stddef.h>
#include <stdint.h>

#include "zlib-ng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How one piece of a block ended, see blockdec_feed(). */
enum { SEG_FULL, SEG_END, SEG_SHORT, SEG_OVERFLOW, SEG_ERROR };

/* Incremental decoder for one independent block, fed one piece of input at a time. */
typedef struct {
    zng_stream *z;
    int want_marker;    /* output complete, the trailing empty stored block is still to come */
    int accept_partial; /* the input ends at a marker pair, so any clean output size is a block */
} block_dec;

void blockdec_begin(block_dec *d, zng_stream *z, uint8_t *out, uint32_t block_size);
int blockdec_feed(block_dec *d, const uint8_t *in, size_t in_len, size_t *used);
const char *blockdec_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_BLOCKDEC_H_ */
