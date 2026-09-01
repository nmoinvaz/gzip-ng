/* pool.h -- the thread pool and slot ring behind the gzblock engine
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_POOL_H_
#define GZNG_POOL_H_

#include <stddef.h>
#include <stdint.h>

#include "buf.h"
#include "zlib-ng.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RING_BYTES (1ULL << 30) /* upper bound on block buffers held in flight */

/* Ring of blocks, filled in order by the calling thread, worked on by the pool, drained in
   order. Decompression inflates segments into blocks, compression deflates blocks into pieces. */
enum { SLOT_FREE, SLOT_FILLED, SLOT_CLAIMED, SLOT_DONE };
enum { POOL_INFLATE, POOL_DEFLATE };

typedef struct {
    uint8_t *in; /* input block or compressed segment, owned by the slot */
    size_t in_len;
    size_t in_size;
    int32_t last;    /* final piece of the input */
    int32_t pair;    /* the segment ends with a marker pair, a boundary in its own right */
    int32_t members; /* whole gzip members in the input, 0 for a raw deflate segment */
    uint8_t *out;
    size_t out_size; /* grows past block_size for pair-terminated and final segments */
    int32_t level;   /* deflate settings for this block */
    int32_t strategy;
    int32_t status; /* SEGMENT_* for inflate, BLOCK_* for deflate */
    size_t out_len;
    size_t in_used;
    uint32_t crc; /* crc32 of the uncompressed side */
    int32_t state;
} slot_t;

void slot_swap_in(slot_t *slot, buf_t *seg);
void slot_append(slot_t *slot, const uint8_t *buf, size_t len);

typedef struct pool_s {
    int32_t mode; /* POOL_INFLATE or POOL_DEFLATE */
    uint32_t block_size;
    int32_t level; /* deflate settings */
    int32_t strategy;
    size_t out_size; /* bytes in each slot's out buffer */
    slot_t *ring;
    size_t nring;
    slot_t **queue; /* filled slots in fill order, at most nring */
    size_t queue_head;
    size_t queue_tail;
    int32_t abort;
    zng_stream strm;             /* stream for working slots on the calling thread */
    int32_t inline_run;          /* no worker threads, slots are worked on demand */
    struct pool_threads *thread; /* workers, mutex, and the two condition variables */
} pool_t;

int32_t pool_default_threads(void);

slot_t *pool_slot(pool_t *pool, size_t i);
int32_t pool_alloc(pool_t *pool, int32_t nthreads, size_t in_size, size_t out_size);
void pool_free(pool_t *pool);
int32_t pool_start(pool_t *pool, int32_t nthreads);
void pool_stop(pool_t *pool);
void pool_submit(pool_t *pool, slot_t *slot);
void pool_wait(pool_t *pool, slot_t *slot);
int32_t pool_cancel(pool_t *pool, slot_t *slot);
void pool_release(pool_t *pool, slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_POOL_H_ */
