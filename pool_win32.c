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
    CRITICAL_SECTION mu;
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
    struct pool_threads *th = pool->th;
    zng_stream strm;

    if (pool->codec.init(pool, &strm) != Z_OK)
        return 0;
    for (;;) {
        slot_t *slot;

        EnterCriticalSection(&th->mu);
        while (!pool->abort && pool->qhead == pool->qtail)
            SleepConditionVariableCS(&th->work_cv, &th->mu, INFINITE);
        if (pool->abort) {
            LeaveCriticalSection(&th->mu);
            break;
        }
        slot = pool->queue[pool->qhead++ % pool->nring];
        slot->state = SLOT_CLAIMED;
        LeaveCriticalSection(&th->mu);

        pool->codec.run(pool, &strm, slot);

        EnterCriticalSection(&th->mu);
        slot->state = SLOT_DONE;
        WakeAllConditionVariable(&th->done_cv);
        LeaveCriticalSection(&th->mu);
    }
    pool->codec.end(pool, &strm);
    return 0;
}

int pool_start(pool_t *pool, int nthreads) {
    struct pool_threads *th;

    if (nthreads <= 1)
        return pool_start_inline(pool);

    th = (struct pool_threads *)calloc(1, sizeof(*th));
    if (th == NULL)
        return -1;
    InitializeCriticalSection(&th->mu);
    InitializeConditionVariable(&th->work_cv);
    InitializeConditionVariable(&th->done_cv);
    th->threads = (HANDLE *)calloc((size_t)nthreads, sizeof(HANDLE));
    if (th->threads == NULL) {
        DeleteCriticalSection(&th->mu);
        free(th);
        return -1;
    }
    pool->th = th;
    for (th->started = 0; th->started < nthreads; th->started++) {
        uintptr_t h = _beginthreadex(NULL, 0, worker, pool, 0, NULL);
        if (h == 0)
            break;
        th->threads[th->started] = (HANDLE)h;
    }
    if (th->started == 0) {
        free(th->threads);
        DeleteCriticalSection(&th->mu);
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
    EnterCriticalSection(&th->mu);
    pool->abort = 1;
    WakeAllConditionVariable(&th->work_cv);
    LeaveCriticalSection(&th->mu);
    for (i = 0; i < th->started; i++) {
        WaitForSingleObject(th->threads[i], INFINITE);
        CloseHandle(th->threads[i]);
    }
    free(th->threads);
    DeleteCriticalSection(&th->mu);
    free(th);
    pool->th = NULL;
}

void pool_submit(pool_t *pool, slot_t *slot) {
    struct pool_threads *th = pool->th;

    if (pool->inline_run) {
        slot->state = SLOT_FILLED;
        return;
    }
    EnterCriticalSection(&th->mu);
    slot->state = SLOT_FILLED;
    pool->queue[pool->qtail++ % pool->nring] = slot;
    WakeConditionVariable(&th->work_cv);
    LeaveCriticalSection(&th->mu);
}

void pool_wait(pool_t *pool, slot_t *slot) {
    struct pool_threads *th = pool->th;

    if (pool->inline_run) {
        pool_wait_inline(pool, slot);
        return;
    }
    EnterCriticalSection(&th->mu);
    while (slot->state != SLOT_DONE)
        SleepConditionVariableCS(&th->done_cv, &th->mu, INFINITE);
    LeaveCriticalSection(&th->mu);
}

void pool_release(pool_t *pool, slot_t *slot) {
    struct pool_threads *th = pool->th;

    if (pool->inline_run) {
        slot->state = SLOT_FREE;
        return;
    }
    EnterCriticalSection(&th->mu);
    slot->state = SLOT_FREE;
    LeaveCriticalSection(&th->mu);
}

#endif /* GZBLOCK_THREADS && _WIN32 */
