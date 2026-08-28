/* pool.h -- the thread pool and slot ring behind the gzblock engine
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_POOL_H_
#define GZNG_POOL_H_

#include <stddef.h>
#include <stdint.h>

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
    size_t in_len, in_cap;
    int last; /* final piece of the input */
    int pair; /* the segment ends with a marker pair, a boundary in its own right */
    uint8_t *out;
    size_t out_cap;      /* grows past block_size for pair-terminated and final segments */
    int level, strategy; /* deflate settings for this block */
    int status;          /* SEG_* for inflate, 0 or -1 for deflate */
    size_t out_len, in_used;
    uint32_t crc; /* crc32 of the uncompressed side */
    int state;
} slot_t;

struct pool_s;

/* The codec the pool drives, handed in before pool_start(), one persistent stream per worker
   running one slot at a time. */
typedef struct {
    int (*init)(struct pool_s *pool, zng_stream *strm);
    void (*end)(struct pool_s *pool, zng_stream *strm);
    void (*run)(struct pool_s *pool, zng_stream *strm, slot_t *slot);
} pool_codec;

typedef struct pool_s {
    pool_codec codec;
    int mode; /* POOL_INFLATE or POOL_DEFLATE */
    uint32_t block_size;
    int level, strategy; /* deflate settings */
    size_t out_cap;      /* bytes in each slot's out buffer */
    slot_t *ring;
    size_t nring;
    slot_t **queue; /* filled slots in fill order, at most nring */
    size_t qhead, qtail;
    int abort;
    zng_stream strm;         /* stream for working slots on the calling thread */
    int inline_run;          /* no worker threads, slots are worked on demand */
    struct pool_threads *th; /* workers, mutex, and the two condition variables */
} pool_t;

int pool_default_threads(void);

slot_t *pool_slot(pool_t *pool, size_t i);
int pool_alloc(pool_t *pool, int nthreads, size_t in_cap, size_t out_cap);
void pool_free(pool_t *pool);
int pool_start(pool_t *pool, int nthreads);
void pool_stop(pool_t *pool);
void pool_submit(pool_t *pool, slot_t *slot);
void pool_wait(pool_t *pool, slot_t *slot);
void pool_release(pool_t *pool, slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_POOL_H_ */
