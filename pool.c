/* pool.c -- the thread pool and slot ring behind the gzblock engine
 * For conditions of distribution and use, see LICENSE.md
 */

#include "pool_p.h"

#include <stdlib.h>

#ifndef GZBLOCK_THREADS
int pool_default_threads(void) {
    return 1;
}
#endif

slot_t *pool_slot(pool_t *pool, size_t i) {
    return &pool->ring[i % pool->nring];
}

/* Allocate the ring, nthreads * 4 slots of in_cap + out_cap bytes, within RING_BYTES. */
int pool_alloc(pool_t *pool, int nthreads, size_t in_cap, size_t out_cap) {
    size_t i;
#ifdef GZBLOCK_THREADS
    pool->nring = nthreads <= 1 ? 1 : (size_t)nthreads * 4;
    while (pool->nring > 2 && (unsigned long long)pool->nring * (in_cap + out_cap) > RING_BYTES)
        pool->nring /= 2;
#else
    (void)nthreads;
    pool->nring = 1;
#endif
    pool->out_cap = out_cap;
    pool->ring = (slot_t *)calloc(pool->nring, sizeof(slot_t));
    pool->queue = (slot_t **)calloc(pool->nring, sizeof(slot_t *));
    if (pool->ring == NULL || pool->queue == NULL)
        return -1;
    for (i = 0; i < pool->nring; i++) {
        pool->ring[i].out = (uint8_t *)malloc(out_cap);
        pool->ring[i].out_cap = out_cap;
        if (pool->ring[i].out == NULL)
            return -1;
        if (in_cap != 0) {
            pool->ring[i].in = (uint8_t *)malloc(in_cap);
            pool->ring[i].in_cap = in_cap;
            if (pool->ring[i].in == NULL)
                return -1;
        }
    }
    return 0;
}

void pool_free(pool_t *pool) {
    size_t i;
    if (pool->ring != NULL) {
        for (i = 0; i < pool->nring; i++) {
            free(pool->ring[i].in);
            free(pool->ring[i].out);
        }
        free(pool->ring);
    }
    free(pool->queue);
    pool->ring = NULL;
    pool->queue = NULL;
    pool->nring = 0;
    pool->qhead = pool->qtail = 0;
    pool->abort = 0;
}

/* Without worker threads the slots are worked on demand by the calling thread. */
int pool_start_inline(pool_t *pool) {
    pool->inline_run = 1;
    return pool->codec.init(pool, &pool->z) == Z_OK ? 0 : -1;
}

void pool_stop_inline(pool_t *pool) {
    pool->codec.end(pool, &pool->z);
}

void pool_wait_inline(pool_t *pool, slot_t *slot) {
    if (slot->state == SLOT_FILLED)
        pool->codec.run(pool, &pool->z, slot);
    slot->state = SLOT_DONE;
}

#ifndef GZBLOCK_THREADS

int pool_start(pool_t *pool, int nthreads) {
    (void)nthreads;
    return pool_start_inline(pool);
}

void pool_stop(pool_t *pool) {
    pool_stop_inline(pool);
    pool->inline_run = 0;
}

void pool_submit(pool_t *pool, slot_t *slot) {
    (void)pool;
    slot->state = SLOT_FILLED;
}

void pool_wait(pool_t *pool, slot_t *slot) {
    pool_wait_inline(pool, slot);
}

void pool_release(pool_t *pool, slot_t *slot) {
    (void)pool;
    slot->state = SLOT_FREE;
}

#endif
