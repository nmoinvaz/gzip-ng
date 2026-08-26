/* pool.h -- the thread pool and slot ring behind the gzblock engine
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_POOL_H_
#define GZNG_POOL_H_

#include <stddef.h>
#include <stdint.h>

#include "zlib-ng.h"

#ifdef GZBLOCK_THREADS
#  include <pthread.h>
#endif

#define RING_BYTES (1ULL << 30)   /* upper bound on block buffers held in flight */

/* Ring of blocks, filled in order by the calling thread, worked on by the pool, drained in
   order. Decompression inflates segments into blocks, compression deflates blocks into pieces. */
enum { SLOT_FREE, SLOT_FILLED, SLOT_CLAIMED, SLOT_DONE };
enum { POOL_INFLATE, POOL_DEFLATE };

typedef struct {
    uint8_t *in;         /* input block or compressed segment, owned by the slot */
    size_t in_len, in_cap;
    int last;            /* final piece of the input */
    int pair;            /* the segment ends with a marker pair, a boundary in its own right */
    uint8_t *out;
    size_t out_cap;      /* grows past block_size for pair-terminated and final segments */
    int level, strategy; /* deflate settings for this block */
    int status;          /* SEG_* for inflate, 0 or -1 for deflate */
    size_t out_len, in_used;
    uint32_t crc;        /* crc32 of the uncompressed side */
    int state;
} slot_t;

typedef struct {
    int mode;            /* POOL_INFLATE or POOL_DEFLATE */
    uint32_t block_size;
    int level, strategy; /* deflate settings */
    size_t out_cap;      /* bytes in each slot's out buffer */
    slot_t *ring;
    size_t nring;
    slot_t **queue;      /* filled slots in fill order, at most nring */
    size_t qhead, qtail;
    int abort;
    zng_stream z;   /* stream for working slots on the calling thread */
    int inline_run;      /* no worker threads, slots are worked on demand */
#ifdef GZBLOCK_THREADS
    pthread_mutex_t mu;
    pthread_cond_t work_cv;   /* a slot was queued, or abort */
    pthread_cond_t done_cv;   /* a slot became done */
    pthread_t *threads;
    int started;
#endif
} pool_t;

/* Codec hooks the pool drives, implemented by the codec, one persistent stream per worker
   running one slot at a time. */
int pool_codec_init(pool_t *p, zng_stream *z);
void pool_codec_end(pool_t *p, zng_stream *z);
void pool_codec_run(pool_t *p, zng_stream *z, slot_t *slot);

int pool_default_threads(void);

slot_t *pool_slot(pool_t *p, size_t i);
int pool_alloc(pool_t *p, int nthreads, size_t in_cap, size_t out_cap);
void pool_free(pool_t *p);
int pool_start(pool_t *p, int nthreads);
void pool_stop(pool_t *p);
void pool_submit(pool_t *p, slot_t *slot);
void pool_wait(pool_t *p, slot_t *slot);
void pool_release(pool_t *p, slot_t *slot);

#endif /* GZNG_POOL_H_ */
