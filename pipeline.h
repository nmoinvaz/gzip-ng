/* pipeline.h -- ordered submit and drain of slots through the pool
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_PIPELINE_H_
#define GZNG_PIPELINE_H_

#include <stddef.h>
#include <stdint.h>

#include "pool.h"

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

/* The block at next_drain went out in order, the next one is up. */
static inline void pipeline_drained(pipeline_t *pipeline) {
    pipeline->next_drain++;
}

static inline int32_t pipeline_has_pending(const pipeline_t *pipeline) {
    return pipeline->next_drain < pipeline->next_submit;
}

static inline slot_t *pipeline_wait(pipeline_t *pipeline, size_t index) {
    slot_t *slot = pool_slot(&pipeline->pool, index);
    pool_wait(&pipeline->pool, slot);
    return slot;
}

/* Everything submitted but not yet drained was taken back, nothing is pending. */
static inline void pipeline_clear(pipeline_t *pipeline) {
    pipeline->next_submit = pipeline->next_drain;
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

#endif /* GZNG_PIPELINE_H_ */
