/* pool_p.h -- private interfaces shared by the pool and its workers
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_POOL_P_H_
#define GZNG_POOL_P_H_

#include "pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The deflate or inflate a worker runs, one persistent stream per worker over one slot at a
   time, chosen by the pool's mode. */
int32_t codec_init(pool_t *pool, zng_stream *strm);
void codec_end(pool_t *pool, zng_stream *strm);
void codec_run(pool_t *pool, zng_stream *strm, slot_t *slot);

/* One worker on the calling thread, used when only one was asked for. */
int32_t pool_start_inline(pool_t *pool);
void pool_stop_inline(pool_t *pool);
void pool_wait_inline(pool_t *pool, slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_POOL_P_H_ */
