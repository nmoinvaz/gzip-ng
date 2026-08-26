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

slot_t *pool_slot(pool_t *p, size_t i) {
    return &p->ring[i % p->nring];
}

/* Allocate the ring, nthreads * 4 slots of in_cap + out_cap bytes, within RING_BYTES. */
int pool_alloc(pool_t *p, int nthreads, size_t in_cap, size_t out_cap) {
    size_t i;
#ifdef GZBLOCK_THREADS
    p->nring = nthreads <= 1 ? 1 : (size_t)nthreads * 4;
    while (p->nring > 2 && (unsigned long long)p->nring * (in_cap + out_cap) > RING_BYTES)
        p->nring /= 2;
#else
    (void)nthreads;
    p->nring = 1;
#endif
    p->out_cap = out_cap;
    p->ring = (slot_t *)calloc(p->nring, sizeof(slot_t));
    p->queue = (slot_t **)calloc(p->nring, sizeof(slot_t *));
    if (p->ring == NULL || p->queue == NULL)
        return -1;
    for (i = 0; i < p->nring; i++) {
        p->ring[i].out = (uint8_t *)malloc(out_cap);
        p->ring[i].out_cap = out_cap;
        if (p->ring[i].out == NULL)
            return -1;
        if (in_cap != 0) {
            p->ring[i].in = (uint8_t *)malloc(in_cap);
            p->ring[i].in_cap = in_cap;
            if (p->ring[i].in == NULL)
                return -1;
        }
    }
    return 0;
}

void pool_free(pool_t *p) {
    size_t i;
    if (p->ring != NULL) {
        for (i = 0; i < p->nring; i++) {
            free(p->ring[i].in);
            free(p->ring[i].out);
        }
        free(p->ring);
    }
    free(p->queue);
    p->ring = NULL;
    p->queue = NULL;
    p->nring = 0;
    p->qhead = p->qtail = 0;
    p->abort = 0;
}

/* Without worker threads the slots are worked on demand by the calling thread. */
static int pool_start_inline(pool_t *p) {
    p->inline_run = 1;
    return p->codec.init(p, &p->z) == Z_OK ? 0 : -1;
}

static void pool_stop_inline(pool_t *p) {
    p->codec.end(p, &p->z);
}

static void slot_wait_inline(pool_t *p, slot_t *slot) {
    if (slot->state == SLOT_FILLED)
        p->codec.run(p, &p->z, slot);
    slot->state = SLOT_DONE;
}

#ifdef GZBLOCK_THREADS

static void *worker(void *arg) {
    pool_t *p = (pool_t *)arg;
    zng_stream z;

    if (p->codec.init(p, &z) != Z_OK)
        return NULL;
    for (;;) {
        slot_t *slot;

        pthread_mutex_lock(&p->mu);
        while (!p->abort && p->qhead == p->qtail)
            pthread_cond_wait(&p->work_cv, &p->mu);
        if (p->abort) {
            pthread_mutex_unlock(&p->mu);
            break;
        }
        slot = p->queue[p->qhead++ % p->nring];
        slot->state = SLOT_CLAIMED;
        pthread_mutex_unlock(&p->mu);

        p->codec.run(p, &z, slot);

        pthread_mutex_lock(&p->mu);
        slot->state = SLOT_DONE;
        pthread_cond_broadcast(&p->done_cv);
        pthread_mutex_unlock(&p->mu);
    }
    p->codec.end(p, &z);
    return NULL;
}

int pool_start(pool_t *p, int nthreads) {
    if (nthreads <= 1)
        return pool_start_inline(p);
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->work_cv, NULL);
    pthread_cond_init(&p->done_cv, NULL);
    p->threads = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    if (p->threads == NULL)
        return -1;
    for (p->started = 0; p->started < nthreads; p->started++) {
        if (pthread_create(&p->threads[p->started], NULL, worker, p) != 0)
            break;
    }
    return p->started > 0 ? 0 : -1;
}

void pool_stop(pool_t *p) {
    int i;
    if (p->inline_run) {
        pool_stop_inline(p);
        p->inline_run = 0;
        return;
    }
    if (p->threads == NULL)
        return;
    pthread_mutex_lock(&p->mu);
    p->abort = 1;
    pthread_cond_broadcast(&p->work_cv);
    pthread_mutex_unlock(&p->mu);
    for (i = 0; i < p->started; i++)
        pthread_join(p->threads[i], NULL);
    free(p->threads);
    p->threads = NULL;
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->work_cv);
    pthread_cond_destroy(&p->done_cv);
}

void pool_submit(pool_t *p, slot_t *slot) {
    if (p->inline_run) {
        slot->state = SLOT_FILLED;
        return;
    }
    pthread_mutex_lock(&p->mu);
    slot->state = SLOT_FILLED;
    p->queue[p->qtail++ % p->nring] = slot;
    pthread_cond_signal(&p->work_cv);
    pthread_mutex_unlock(&p->mu);
}

void pool_wait(pool_t *p, slot_t *slot) {
    if (p->inline_run) {
        slot_wait_inline(p, slot);
        return;
    }
    pthread_mutex_lock(&p->mu);
    while (slot->state != SLOT_DONE)
        pthread_cond_wait(&p->done_cv, &p->mu);
    pthread_mutex_unlock(&p->mu);
}

void pool_release(pool_t *p, slot_t *slot) {
    if (p->inline_run) {
        slot->state = SLOT_FREE;
        return;
    }
    pthread_mutex_lock(&p->mu);
    slot->state = SLOT_FREE;
    pthread_mutex_unlock(&p->mu);
}

#else /* !GZBLOCK_THREADS */

int pool_start(pool_t *p, int nthreads) {
    (void)nthreads;
    return pool_start_inline(p);
}

void pool_stop(pool_t *p) {
    pool_stop_inline(p);
    p->inline_run = 0;
}

void pool_submit(pool_t *p, slot_t *slot) {
    (void)p;
    slot->state = SLOT_FILLED;
}

void pool_wait(pool_t *p, slot_t *slot) {
    slot_wait_inline(p, slot);
}

void pool_release(pool_t *p, slot_t *slot) {
    (void)p;
    slot->state = SLOT_FREE;
}

#endif
