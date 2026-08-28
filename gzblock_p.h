/* gzblock_p.h -- private interfaces shared by the gzblock core, reader, and writer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZBLOCK_P_H_
#define GZBLOCK_P_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"
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
    size_t next_submit;
    size_t next_drain;
    int32_t started;
} pipeline_t;

static inline int32_t pipeline_start(pipeline_t *pipeline, int32_t nthreads, size_t in_size, size_t out_size) {
    if (pool_alloc(&pipeline->pool, nthreads, in_size, out_size) != 0)
        return -1;
    if (pool_start(&pipeline->pool, nthreads) != 0) {
        pool_free(&pipeline->pool);
        return -2;
    }
    pipeline->started = 1;
    return 0;
}

static inline void pipeline_submit(pipeline_t *pipeline, slot_t *slot) {
    pool_submit(&pipeline->pool, slot);
    pipeline->next_submit++;
}

static inline int32_t pipeline_has_pending(const pipeline_t *pipeline) {
    return pipeline->next_drain < pipeline->next_submit;
}

static inline slot_t *pipeline_wait(pipeline_t *pipeline, size_t index) {
    slot_t *slot = pool_slot(&pipeline->pool, index);
    pool_wait(&pipeline->pool, slot);
    return slot;
}

static inline void pipeline_reset(pipeline_t *pipeline) {
    pipeline->next_submit = pipeline->next_drain = 0;
}

static inline void pipeline_free(pipeline_t *pipeline) {
    if (pipeline->started)
        pool_stop(&pipeline->pool);
    pool_free(&pipeline->pool);
    pipeline->started = 0;
}

#endif /* GZBLOCK_P_H_ */
