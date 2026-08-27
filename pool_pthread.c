/* pool_pthread.c -- pthread workers for the gzblock pool
 * For conditions of distribution and use, see LICENSE.md
 */

#if defined(GZBLOCK_THREADS) && !defined(_WIN32)

#  if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE /* sysconf(_SC_NPROCESSORS_ONLN) is hidden under strict POSIX */
#  endif

#  include "pool_p.h"
#  include "util.h"

#  include <pthread.h>
#  include <stdlib.h>
#  include <unistd.h>

struct pool_threads {
    pthread_mutex_t mu;
    pthread_cond_t work_cv; /* a slot was queued, or abort */
    pthread_cond_t done_cv; /* a slot became done */
    pthread_t *threads;
    int started;
};

int pool_default_threads(void) {
#  ifdef _SC_NPROCESSORS_ONLN
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0)
        return (int)MIN(n, 64);
#  endif
    return 1;
}

static void *worker(void *arg) {
    pool_t *pool = (pool_t *)arg;
    struct pool_threads *th = pool->th;
    zng_stream z;

    if (pool->codec.init(pool, &z) != Z_OK)
        return NULL;
    for (;;) {
        slot_t *slot;

        pthread_mutex_lock(&th->mu);
        while (!pool->abort && pool->qhead == pool->qtail)
            pthread_cond_wait(&th->work_cv, &th->mu);
        if (pool->abort) {
            pthread_mutex_unlock(&th->mu);
            break;
        }
        slot = pool->queue[pool->qhead++ % pool->nring];
        slot->state = SLOT_CLAIMED;
        pthread_mutex_unlock(&th->mu);

        pool->codec.run(pool, &z, slot);

        pthread_mutex_lock(&th->mu);
        slot->state = SLOT_DONE;
        pthread_cond_broadcast(&th->done_cv);
        pthread_mutex_unlock(&th->mu);
    }
    pool->codec.end(pool, &z);
    return NULL;
}

int pool_start(pool_t *pool, int nthreads) {
    struct pool_threads *th;

    if (nthreads <= 1)
        return pool_start_inline(pool);

    th = (struct pool_threads *)calloc(1, sizeof(*th));
    if (th == NULL)
        return -1;
    pthread_mutex_init(&th->mu, NULL);
    pthread_cond_init(&th->work_cv, NULL);
    pthread_cond_init(&th->done_cv, NULL);
    th->threads = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    if (th->threads == NULL) {
        pthread_mutex_destroy(&th->mu);
        pthread_cond_destroy(&th->work_cv);
        pthread_cond_destroy(&th->done_cv);
        free(th);
        return -1;
    }
    pool->th = th;
    for (th->started = 0; th->started < nthreads; th->started++) {
        if (pthread_create(&th->threads[th->started], NULL, worker, pool) != 0)
            break;
    }
    if (th->started == 0) {
        free(th->threads);
        pthread_mutex_destroy(&th->mu);
        pthread_cond_destroy(&th->work_cv);
        pthread_cond_destroy(&th->done_cv);
        free(th);
        pool->th = NULL;
        return -1;
    }
    return 0;
}

void pool_stop(pool_t *pool) {
    struct pool_threads *th = pool->th;
    int i;

    if (pool->inline_run) {
        pool_stop_inline(pool);
        pool->inline_run = 0;
        return;
    }
    if (th == NULL)
        return;
    pthread_mutex_lock(&th->mu);
    pool->abort = 1;
    pthread_cond_broadcast(&th->work_cv);
    pthread_mutex_unlock(&th->mu);
    for (i = 0; i < th->started; i++)
        pthread_join(th->threads[i], NULL);
    free(th->threads);
    pthread_mutex_destroy(&th->mu);
    pthread_cond_destroy(&th->work_cv);
    pthread_cond_destroy(&th->done_cv);
    free(th);
    pool->th = NULL;
}

void pool_submit(pool_t *pool, slot_t *slot) {
    struct pool_threads *th = pool->th;

    if (pool->inline_run) {
        slot->state = SLOT_FILLED;
        return;
    }
    pthread_mutex_lock(&th->mu);
    slot->state = SLOT_FILLED;
    pool->queue[pool->qtail++ % pool->nring] = slot;
    pthread_cond_signal(&th->work_cv);
    pthread_mutex_unlock(&th->mu);
}

void pool_wait(pool_t *pool, slot_t *slot) {
    struct pool_threads *th = pool->th;

    if (pool->inline_run) {
        pool_wait_inline(pool, slot);
        return;
    }
    pthread_mutex_lock(&th->mu);
    while (slot->state != SLOT_DONE)
        pthread_cond_wait(&th->done_cv, &th->mu);
    pthread_mutex_unlock(&th->mu);
}

void pool_release(pool_t *pool, slot_t *slot) {
    struct pool_threads *th = pool->th;

    if (pool->inline_run) {
        slot->state = SLOT_FREE;
        return;
    }
    pthread_mutex_lock(&th->mu);
    slot->state = SLOT_FREE;
    pthread_mutex_unlock(&th->mu);
}

#endif /* GZBLOCK_THREADS && !_WIN32 */
