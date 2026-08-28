/* gzblock_p.h -- private interfaces shared by the gzblock core, reader, and writer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZBLOCK_P_H_
#define GZBLOCK_P_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockdec.h"
#include "buf.h"
#include "format.h"
#include "gzblock.h"
#include "pool.h"
#include "rolling.h"
#include "util.h"
#include "zlib-ng.h"

#define IO_CHUNK (256 * 1024) /* read and write in this much at a time */
#define MSG_LEN  128          /* room for one error message */

typedef struct {
    pool_t pool;
    size_t next_produce, next_emit;
    int pool_up;
} pipeline_t;

/* The deflate and inflate codec, which the reader and writer hand to their pool. */
int gzblock_codec_init(pool_t *pool, zng_stream *strm);
void gzblock_codec_end(pool_t *pool, zng_stream *strm);
void gzblock_codec_run(pool_t *pool, zng_stream *strm, slot_t *slot);

static inline void pipeline_bind_codec(pipeline_t *pipeline) {
    pipeline->pool.codec.init = gzblock_codec_init;
    pipeline->pool.codec.end = gzblock_codec_end;
    pipeline->pool.codec.run = gzblock_codec_run;
}

static inline int pipeline_start(pipeline_t *pipeline, int nthreads, size_t in_size, size_t out_size) {
    if (pool_alloc(&pipeline->pool, nthreads, in_size, out_size) != 0)
        return -1;
    if (pool_start(&pipeline->pool, nthreads) != 0) {
        pool_free(&pipeline->pool);
        return -2;
    }
    pipeline->pool_up = 1;
    return 0;
}

static inline void pipeline_submit(pipeline_t *pipeline, slot_t *slot) {
    pool_submit(&pipeline->pool, slot);
    pipeline->next_produce++;
}

static inline int pipeline_has_pending(const pipeline_t *pipeline) {
    return pipeline->next_emit < pipeline->next_produce;
}

static inline slot_t *pipeline_wait(pipeline_t *pipeline, size_t index) {
    slot_t *slot = pool_slot(&pipeline->pool, index);
    pool_wait(&pipeline->pool, slot);
    return slot;
}

static inline void pipeline_reset(pipeline_t *pipeline) {
    pipeline->next_produce = pipeline->next_emit = 0;
}

static inline void pipeline_free(pipeline_t *pipeline) {
    if (pipeline->pool_up)
        pool_stop(&pipeline->pool);
    pool_free(&pipeline->pool);
    pipeline->pool_up = 0;
}

#endif /* GZBLOCK_P_H_ */
