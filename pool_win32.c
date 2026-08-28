/* pool_win32.c -- Windows threads for the gzblock pool
 * For conditions of distribution and use, see LICENSE.md
 */

#if defined(GZBLOCK_THREADS) && defined(_WIN32)

#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0600 /* Vista, condition variables */
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif

#  include "pool_p.h"
#  include "util.h"

#  include <stdlib.h>
#  include <windows.h>
#  include <process.h>

struct pool_threads {
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE work_cv; /* a slot was queued, or abort */
    CONDITION_VARIABLE done_cv; /* a slot became done */
    HANDLE *threads;
    int started;
};

int pool_default_threads(void) {
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors > 0)
        return (int)MIN(si.dwNumberOfProcessors, 64);
    return 1;
}

static unsigned __stdcall worker(void *arg) {
    pool_t *pool = (pool_t *)arg;
    struct pool_threads *thread = pool->thread;
    zng_stream strm;

    if (pool->codec.init(pool, &strm) != Z_OK)
        return 0;
    for (;;) {
        slot_t *slot;

        EnterCriticalSection(&thread->mutex);
        while (!pool->abort && pool->queue_head == pool->queue_tail)
            SleepConditionVariableCS(&thread->work_cv, &thread->mutex, INFINITE);
        if (pool->abort) {
            LeaveCriticalSection(&thread->mutex);
            break;
        }
        slot = pool->queue[pool->queue_head++ % pool->nring];
        slot->state = SLOT_CLAIMED;
        LeaveCriticalSection(&thread->mutex);

        pool->codec.run(pool, &strm, slot);

        EnterCriticalSection(&thread->mutex);
        slot->state = SLOT_DONE;
        WakeAllConditionVariable(&thread->done_cv);
        LeaveCriticalSection(&thread->mutex);
    }
    pool->codec.end(pool, &strm);
    return 0;
}

int pool_start(pool_t *pool, int nthreads) {
    struct pool_threads *thread;

    if (nthreads <= 1)
        return pool_start_inline(pool);

    thread = (struct pool_threads *)calloc(1, sizeof(*thread));
    if (thread == NULL)
        return -1;
    InitializeCriticalSection(&thread->mutex);
    InitializeConditionVariable(&thread->work_cv);
    InitializeConditionVariable(&thread->done_cv);
    thread->threads = (HANDLE *)calloc((size_t)nthreads, sizeof(HANDLE));
    if (thread->threads == NULL) {
        DeleteCriticalSection(&thread->mutex);
        free(thread);
        return -1;
    }
    pool->thread = thread;
    for (thread->started = 0; thread->started < nthreads; thread->started++) {
        uintptr_t h = _beginthreadex(NULL, 0, worker, pool, 0, NULL);
        if (h == 0)
            break;
        thread->threads[thread->started] = (HANDLE)h;
    }
    if (thread->started == 0) {
        free(thread->threads);
        DeleteCriticalSection(&thread->mutex);
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
    EnterCriticalSection(&thread->mutex);
    pool->abort = 1;
    WakeAllConditionVariable(&thread->work_cv);
    LeaveCriticalSection(&thread->mutex);
    for (i = 0; i < thread->started; i++) {
        WaitForSingleObject(thread->threads[i], INFINITE);
        CloseHandle(thread->threads[i]);
    }
    free(thread->threads);
    DeleteCriticalSection(&thread->mutex);
    free(thread);
    pool->thread = NULL;
}

void pool_submit(pool_t *pool, slot_t *slot) {
    struct pool_threads *thread = pool->thread;

    if (pool->inline_run) {
        slot->state = SLOT_FILLED;
        return;
    }
    EnterCriticalSection(&thread->mutex);
    slot->state = SLOT_FILLED;
    pool->queue[pool->queue_tail++ % pool->nring] = slot;
    WakeConditionVariable(&thread->work_cv);
    LeaveCriticalSection(&thread->mutex);
}

void pool_wait(pool_t *pool, slot_t *slot) {
    struct pool_threads *thread = pool->thread;

    if (pool->inline_run) {
        pool_wait_inline(pool, slot);
        return;
    }
    EnterCriticalSection(&thread->mutex);
    while (slot->state != SLOT_DONE)
        SleepConditionVariableCS(&thread->done_cv, &thread->mutex, INFINITE);
    LeaveCriticalSection(&thread->mutex);
}

void pool_release(pool_t *pool, slot_t *slot) {
    struct pool_threads *thread = pool->thread;

    if (pool->inline_run) {
        slot->state = SLOT_FREE;
        return;
    }
    EnterCriticalSection(&thread->mutex);
    slot->state = SLOT_FREE;
    LeaveCriticalSection(&thread->mutex);
}

#endif /* GZBLOCK_THREADS && _WIN32 */
