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
    pthread_mutex_t mutex;
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
    struct pool_threads *thread = pool->thread;
    zng_stream strm;

    if (pool->codec.init(pool, &strm) != Z_OK)
        return NULL;
    for (;;) {
        slot_t *slot;

        pthread_mutex_lock(&thread->mutex);
        while (!pool->abort && pool->qhead == pool->qtail)
            pthread_cond_wait(&thread->work_cv, &thread->mutex);
        if (pool->abort) {
            pthread_mutex_unlock(&thread->mutex);
            break;
        }
        slot = pool->queue[pool->qhead++ % pool->nring];
        slot->state = SLOT_CLAIMED;
        pthread_mutex_unlock(&thread->mutex);

        pool->codec.run(pool, &strm, slot);

        pthread_mutex_lock(&thread->mutex);
        slot->state = SLOT_DONE;
        pthread_cond_broadcast(&thread->done_cv);
        pthread_mutex_unlock(&thread->mutex);
    }
    pool->codec.end(pool, &strm);
    return NULL;
}

int pool_start(pool_t *pool, int nthreads) {
    struct pool_threads *thread;

    if (nthreads <= 1)
        return pool_start_inline(pool);

    thread = (struct pool_threads *)calloc(1, sizeof(*thread));
    if (thread == NULL)
        return -1;
    pthread_mutex_init(&thread->mutex, NULL);
    pthread_cond_init(&thread->work_cv, NULL);
    pthread_cond_init(&thread->done_cv, NULL);
    thread->threads = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    if (thread->threads == NULL) {
        pthread_mutex_destroy(&thread->mutex);
        pthread_cond_destroy(&thread->work_cv);
        pthread_cond_destroy(&thread->done_cv);
        free(thread);
        return -1;
    }
    pool->thread = thread;
    for (thread->started = 0; thread->started < nthreads; thread->started++) {
        if (pthread_create(&thread->threads[thread->started], NULL, worker, pool) != 0)
            break;
    }
    if (thread->started == 0) {
        free(thread->threads);
        pthread_mutex_destroy(&thread->mutex);
        pthread_cond_destroy(&thread->work_cv);
        pthread_cond_destroy(&thread->done_cv);
        free(thread);
        pool->thread = NULL;
        return -1;
    }
    return 0;
}

void pool_stop(pool_t *pool) {
    struct pool_threads *thread = pool->thread;
    int i;

    if (pool->inline_run) {
        pool_stop_inline(pool);
        pool->inline_run = 0;
        return;
    }
    if (thread == NULL)
        return;
    pthread_mutex_lock(&thread->mutex);
    pool->abort = 1;
    pthread_cond_broadcast(&thread->work_cv);
    pthread_mutex_unlock(&thread->mutex);
    for (i = 0; i < thread->started; i++)
        pthread_join(thread->threads[i], NULL);
    free(thread->threads);
    pthread_mutex_destroy(&thread->mutex);
    pthread_cond_destroy(&thread->work_cv);
    pthread_cond_destroy(&thread->done_cv);
    free(thread);
    pool->thread = NULL;
}

void pool_submit(pool_t *pool, slot_t *slot) {
    struct pool_threads *thread = pool->thread;

    if (pool->inline_run) {
        slot->state = SLOT_FILLED;
        return;
    }
    pthread_mutex_lock(&thread->mutex);
    slot->state = SLOT_FILLED;
    pool->queue[pool->qtail++ % pool->nring] = slot;
    pthread_cond_signal(&thread->work_cv);
    pthread_mutex_unlock(&thread->mutex);
}

void pool_wait(pool_t *pool, slot_t *slot) {
    struct pool_threads *thread = pool->thread;

    if (pool->inline_run) {
        pool_wait_inline(pool, slot);
        return;
    }
    pthread_mutex_lock(&thread->mutex);
    while (slot->state != SLOT_DONE)
        pthread_cond_wait(&thread->done_cv, &thread->mutex);
    pthread_mutex_unlock(&thread->mutex);
}

void pool_release(pool_t *pool, slot_t *slot) {
    struct pool_threads *thread = pool->thread;

    if (pool->inline_run) {
        slot->state = SLOT_FREE;
        return;
    }
    pthread_mutex_lock(&thread->mutex);
    slot->state = SLOT_FREE;
    pthread_mutex_unlock(&thread->mutex);
}

#endif /* GZBLOCK_THREADS && !_WIN32 */
