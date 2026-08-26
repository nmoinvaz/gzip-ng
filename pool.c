/* pool.c -- the thread pool and slot ring behind the gzblock engine
 * For conditions of distribution and use, see LICENSE.md
 */

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE   /* sysconf(_SC_NPROCESSORS_ONLN) is hidden under strict POSIX */
#endif

#include "gzblock_p.h"

#ifdef GZBLOCK_THREADS
#  include <unistd.h>
#endif

int pool_default_threads(void) {
#if defined(GZBLOCK_THREADS) && defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0)
        return (int)MIN(n, 64);
#endif
    return 1;
}

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
static int pool_start_inline(pool_t *pool) {
    pool->inline_run = 1;
    return pool->codec.init(pool, &pool->z) == Z_OK ? 0 : -1;
}

static void pool_stop_inline(pool_t *pool) {
    pool->codec.end(pool, &pool->z);
}

static void slot_wait_inline(pool_t *pool, slot_t *slot) {
    if (slot->state == SLOT_FILLED)
        pool->codec.run(pool, &pool->z, slot);
    slot->state = SLOT_DONE;
}

#ifdef GZBLOCK_THREADS

static void *worker(void *arg) {
    pool_t *pool = (pool_t *)arg;
    zng_stream z;

    if (pool->codec.init(pool, &z) != Z_OK)
        return NULL;
    for (;;) {
        slot_t *slot;

        pthread_mutex_lock(&pool->mu);
        while (!pool->abort && pool->qhead == pool->qtail)
            pthread_cond_wait(&pool->work_cv, &pool->mu);
        if (pool->abort) {
            pthread_mutex_unlock(&pool->mu);
            break;
        }
        slot = pool->queue[pool->qhead++ % pool->nring];
        slot->state = SLOT_CLAIMED;
        pthread_mutex_unlock(&pool->mu);

        pool->codec.run(pool, &z, slot);

        pthread_mutex_lock(&pool->mu);
        slot->state = SLOT_DONE;
        pthread_cond_broadcast(&pool->done_cv);
        pthread_mutex_unlock(&pool->mu);
    }
    pool->codec.end(pool, &z);
    return NULL;
}

int pool_start(pool_t *pool, int nthreads) {
    if (nthreads <= 1)
        return pool_start_inline(pool);
    pthread_mutex_init(&pool->mu, NULL);
    pthread_cond_init(&pool->work_cv, NULL);
    pthread_cond_init(&pool->done_cv, NULL);
    pool->threads = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    if (pool->threads == NULL)
        return -1;
    for (pool->started = 0; pool->started < nthreads; pool->started++) {
        if (pthread_create(&pool->threads[pool->started], NULL, worker, pool) != 0)
            break;
    }
    return pool->started > 0 ? 0 : -1;
}

void pool_stop(pool_t *pool) {
    int i;
    if (pool->inline_run) {
        pool_stop_inline(pool);
        pool->inline_run = 0;
        return;
    }
    if (pool->threads == NULL)
        return;
    pthread_mutex_lock(&pool->mu);
    pool->abort = 1;
    pthread_cond_broadcast(&pool->work_cv);
    pthread_mutex_unlock(&pool->mu);
    for (i = 0; i < pool->started; i++)
        pthread_join(pool->threads[i], NULL);
    free(pool->threads);
    pool->threads = NULL;
    pthread_mutex_destroy(&pool->mu);
    pthread_cond_destroy(&pool->work_cv);
    pthread_cond_destroy(&pool->done_cv);
}

void pool_submit(pool_t *pool, slot_t *slot) {
    if (pool->inline_run) {
        slot->state = SLOT_FILLED;
        return;
    }
    pthread_mutex_lock(&pool->mu);
    slot->state = SLOT_FILLED;
    pool->queue[pool->qtail++ % pool->nring] = slot;
    pthread_cond_signal(&pool->work_cv);
    pthread_mutex_unlock(&pool->mu);
}

void pool_wait(pool_t *pool, slot_t *slot) {
    if (pool->inline_run) {
        slot_wait_inline(pool, slot);
        return;
    }
    pthread_mutex_lock(&pool->mu);
    while (slot->state != SLOT_DONE)
        pthread_cond_wait(&pool->done_cv, &pool->mu);
    pthread_mutex_unlock(&pool->mu);
}

void pool_release(pool_t *pool, slot_t *slot) {
    if (pool->inline_run) {
        slot->state = SLOT_FREE;
        return;
    }
    pthread_mutex_lock(&pool->mu);
    slot->state = SLOT_FREE;
    pthread_mutex_unlock(&pool->mu);
}

#else /* !GZBLOCK_THREADS */

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
    slot_wait_inline(pool, slot);
}

void pool_release(pool_t *pool, slot_t *slot) {
    (void)pool;
    slot->state = SLOT_FREE;
}

#endif
