/* codec.h -- the deflate and inflate the pool runs over one slot at a time
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_CODEC_H_
#define GZNG_CODEC_H_

#include <stdint.h>

#include "pool.h"
#include "zlib-ng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How inflating a segment ended, the slot's status. FULL is a block whose output and trailing
   marker were consumed, END is the block that ends the deflate stream, SHORT ran out of input
   before either, OVERFLOW wanted more output than a block may have, ERROR is invalid data. */
enum { SEGMENT_FULL, SEGMENT_END, SEGMENT_SHORT, SEGMENT_OVERFLOW, SEGMENT_ERROR };

const char *segment_status_name(int32_t status);

/* One persistent stream per worker, deflate or inflate by the pool's mode. */
int32_t codec_init(pool_t *pool, zng_stream *strm);
void codec_end(pool_t *pool, zng_stream *strm);
void codec_run(pool_t *pool, zng_stream *strm, slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_CODEC_H_ */
