/* sync.Mutex and sync.RWMutex, the locks a program written against burrow
 * actually reaches for.
 *
 * bench/sync_bench.c measures the runtime's own lock and its notes, which are
 * the things underneath. This file measures the layer on top, and the two are
 * worth keeping apart because they answer different questions. The runtime lock
 * blocks a thread and is allowed to, since the runtime is the thing that owns
 * the threads. These park a goroutine instead, so a thousand goroutines queued
 * here are a thousand parked goroutines and not a thousand blocked threads.
 *
 * Two groups of rows. The uncontended ones are a compare and swap and nothing
 * else: no queue exists, the scheduler is never consulted, and the number
 * should be a handful of nanoseconds on both sides. If it is not, something in
 * the fast path failed to inline or grew a branch it does not need.
 *
 * The contended ones are four goroutines locking and unlocking an empty
 * critical section as fast as they can, which is the shape that made Go add
 * starvation mode in the first place. The absolute number depends on the core
 * count and the cache topology of whatever machine ran it, so read the C figure
 * against the Go figure on the same row and ignore the machine. What that ratio
 * says is whether burrow's port of the state word, the spin budget and the
 * handoff came out the same as Go's, because the algorithm is the same
 * algorithm and a gap means the port lost something.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/atomic.h"
#include "burrow/func.h"
#include "burrow/proc.h"
#include "burrow/sync.h"

#include <stdint.h>

/* ------------------------------------------------------------- uncontended
 *
 * No goroutines and no runtime, because there is nothing here for a scheduler
 * to do. A lock nobody is holding is one compare and swap and an unlock with
 * nobody queued is one atomic add, and both of those cost the same whether the
 * caller is a goroutine or the thread that called main. Running these inside
 * runtime_main would add a runtime to the setup and measure exactly the same
 * instructions. */

static SyncMutex bench_mu;
static SyncRWMutex bench_rw;

BENCH(mutex_lock_unlock) {
    SyncMutex *m = (SyncMutex *)bench_hide(&bench_mu);

    BENCH_LOOP(b) {
        sync_mutex_lock(m);
        sync_mutex_unlock(m);
    }
}

/* The same pair through TryLock, which can never wait.
 *
 * It comes out dearer than the row above rather than cheaper, and that is worth
 * knowing before reaching for it. Lock goes straight at a compare and swap
 * against zero, while TryLock has to read the state first to find out whether
 * the mutex is starving, because taking the lock out from under a queue that is
 * being served in order is the one thing it must not do. That read is the whole
 * difference, and it means TryLock is not a cheaper Lock. It is a Lock with a
 * different answer when the lock is busy. */
BENCH(mutex_try_lock_unlock) {
    SyncMutex *m = (SyncMutex *)bench_hide(&bench_mu);

    BENCH_LOOP(b) {
        if (sync_mutex_try_lock(m))
            sync_mutex_unlock(m);
    }
}

/* Through a Locker, which is one indirect call each way over the row above.
 * Worth its own row because the indirect call is the thing that stops the
 * compiler inlining the lock, and code that takes a Locker rather than a Mutex
 * pays for that on every call. Go pays the same price for the same reason. */
BENCH(mutex_locker_lock_unlock) {
    SyncLocker l = sync_mutex_locker(&bench_mu);
    l.data = (void *)bench_hide(l.data);

    BENCH_LOOP(b) {
        sync_locker_lock(l);
        sync_locker_unlock(l);
    }
}

BENCH(rw_mutex_lock_unlock) {
    SyncRWMutex *rw = (SyncRWMutex *)bench_hide(&bench_rw);

    BENCH_LOOP(b) {
        sync_rw_mutex_lock(rw);
        sync_rw_mutex_unlock(rw);
    }
}

/* A read lock with no writer anywhere near it, which is one add on the way in
 * and one on the way out. It is the row that says what a read lock costs when
 * it wins, and it should be cheaper than the write lock above, which has to go
 * through the inner Mutex first. */
BENCH(rw_mutex_r_lock_unlock) {
    SyncRWMutex *rw = (SyncRWMutex *)bench_hide(&bench_rw);

    BENCH_LOOP(b) {
        sync_rw_mutex_r_lock(rw);
        sync_rw_mutex_r_unlock(rw);
    }
}

/* --------------------------------------------------------------- contended
 *
 * Four goroutines on four Ps, and an empty critical section. Four rather than
 * however many cores the machine has, so that a laptop and a server are running
 * the same benchmark and the numbers in results/ can be read against each
 * other. Empty because a critical section with work in it measures the work,
 * and because an empty one is the worst case for fairness: a goroutine that
 * locks and unlocks in a tight loop is the one that starves a queue, which is
 * the case starvation mode exists for.
 *
 * The iteration count is split across the goroutines, and the Go side splits it
 * the same way across the same number of goroutines with GOMAXPROCS set to
 * match, so the per operation figure means the same thing on both sides. */

#define MUTEX_WORKERS 4

static Bench *cur;
static int64_t each;
static uint32_t workers_done;

static void run_contended(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    cur = b;
    each = b->n / MUTEX_WORKERS;
    if (each < 1)
        each = 1;
    workers_done = 0;

    /* As many Ps as there are goroutines, so that all four really do run at
     * once and the row is about the lock rather than about the run queue. The
     * Go side sets GOMAXPROCS to the same number for the same reason, which it
     * has to do by hand because RunParallel would otherwise start one goroutine
     * per P and the two sides would be counting different crowds. */
    int old = runtime_gomaxprocs(MUTEX_WORKERS);
    runtime_main(BURROW_FN(Func, body, NULL));
    (void)runtime_gomaxprocs(old);
}

/* Started by the body, waited for by the body. Yielding rather than parking,
 * since a WaitGroup is the next thing to be written and not this one. */
static void wait_for_workers(void) {
    while (burrow__atomic_load_acquire_u32(&workers_done) < MUTEX_WORKERS)
        runtime_gosched();
}

static void mutex_child(void *env) {
    (void)env;

    for (int64_t i = 0; i < each; i++) {
        sync_mutex_lock(&bench_mu);
        sync_mutex_unlock(&bench_mu);
    }

    (void)burrow__atomic_add_u32(&workers_done, 1);
}

static void mutex_contended_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    for (int i = 0; i < MUTEX_WORKERS; i++) {
        if (!go(BURROW_FN(Func, mutex_child, NULL))) {
            (void)burrow__atomic_add_u32(&workers_done, 1);
        }
    }
    wait_for_workers();

    bench_pause(b);
}

BENCH(mutex_contended) {
    run_contended(b, mutex_contended_body);
}

/* All four reading, which is the case an RWMutex is bought for. Nobody has to
 * wait for anybody, so what this measures is four cores writing the same
 * counter, and that is the honest answer to the question of whether a read lock
 * is free. It is not: two readers on two cores contend for the cache line just
 * as hard as two writers would, and the only reason to reach for an RWMutex is
 * a critical section long enough to pay for that. */
static void rw_read_child(void *env) {
    (void)env;

    for (int64_t i = 0; i < each; i++) {
        sync_rw_mutex_r_lock(&bench_rw);
        sync_rw_mutex_r_unlock(&bench_rw);
    }

    (void)burrow__atomic_add_u32(&workers_done, 1);
}

static void rw_contended_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    for (int i = 0; i < MUTEX_WORKERS; i++) {
        if (!go(BURROW_FN(Func, rw_read_child, NULL))) {
            (void)burrow__atomic_add_u32(&workers_done, 1);
        }
    }
    wait_for_workers();

    bench_pause(b);
}

BENCH(rw_mutex_contended_read) {
    run_contended(b, rw_contended_body);
}

void register_mutex_benchmarks(void);

void register_mutex_benchmarks(void) {
    BENCH_RUN(mutex_lock_unlock);
    BENCH_RUN(mutex_try_lock_unlock);
    BENCH_RUN(mutex_locker_lock_unlock);
    BENCH_RUN(rw_mutex_lock_unlock);
    BENCH_RUN(rw_mutex_r_lock_unlock);
    BENCH_RUN(mutex_contended);
    BENCH_RUN(rw_mutex_contended_read);
}
