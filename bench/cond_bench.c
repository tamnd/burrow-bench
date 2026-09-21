/* sync.Cond.
 *
 * Two questions, and they are far apart.
 *
 * The empty rows are what a signal costs when nobody is waiting, which is the
 * case a producer hits on most of its calls in any queue that keeps up with its
 * consumers. Both sides answer it with two atomic loads and a compare, so the
 * row should be a couple of nanoseconds and a gap there is a gap in the port.
 *
 * The ping pong row is what a handoff costs: one park and one unpark each way,
 * plus the lock in the middle. That is mostly the scheduler rather than the
 * Cond, and it is here for the same reason the contended mutex rows are, which
 * is that the scheduler is the thing a condition variable is really made of.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/func.h"
#include "burrow/proc.h"
#include "burrow/sync.h"
#include "burrow/sync/atomic.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------- nobody is waiting
 *
 * No goroutines and no runtime, for the reason the uncontended mutex rows give:
 * a signal with an empty queue never reaches the scheduler, so it costs the
 * same whoever is running it. */

static SyncMutex empty_mu;
static SyncCond empty_cond;

static void init_empty(void) {
    memset(&empty_mu, 0, sizeof empty_mu);
    empty_cond = SYNC_COND(sync_mutex_locker(&empty_mu));
}

BENCH(cond_signal_empty) {
    init_empty();
    SyncCond *c = (SyncCond *)bench_hide(&empty_cond);

    BENCH_LOOP(b) {
        sync_cond_signal(c);
    }
}

BENCH(cond_broadcast_empty) {
    init_empty();
    SyncCond *c = (SyncCond *)bench_hide(&empty_cond);

    BENCH_LOOP(b) {
        sync_cond_broadcast(c);
    }
}

/* ------------------------------------------------------------- ping pong
 *
 * Two goroutines taking turns through one Cond, one round per iteration. Each
 * round is a signal that wakes a parked goroutine and a wait that parks this
 * one, both ways, so the number is two handoffs and not one.
 *
 * Two Ps, so that neither of them is queued behind the other on one processor.
 * The Go side does the same. */

static SyncMutex pp_mu;
static SyncCond pp_cond;
static int64_t pp_turn;
static bool pp_stop;
static SyncAtomicInt64 pp_done;

static Bench *cur;

static void pp_partner(void *env) {
    (void)env;

    for (;;) {
        sync_mutex_lock(&pp_mu);
        while (pp_turn != 1)
            sync_cond_wait(&pp_cond);
        bool stop = pp_stop;
        pp_turn = 0;
        sync_mutex_unlock(&pp_mu);
        sync_cond_signal(&pp_cond);

        if (stop)
            break;
    }

    sync_atomic_int64_store(&pp_done, 1);
}

static void pp_body(void *env) {
    (void)env;
    Bench *b = cur;

    memset(&pp_mu, 0, sizeof pp_mu);
    pp_cond = SYNC_COND(sync_mutex_locker(&pp_mu));
    pp_turn = 0;
    pp_stop = false;
    sync_atomic_int64_store(&pp_done, 0);

    if (!go(BURROW_FN(Func, pp_partner, NULL)))
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        sync_mutex_lock(&pp_mu);
        pp_turn = 1;
        sync_mutex_unlock(&pp_mu);
        sync_cond_signal(&pp_cond);

        sync_mutex_lock(&pp_mu);
        while (pp_turn != 0)
            sync_cond_wait(&pp_cond);
        sync_mutex_unlock(&pp_mu);
    }

    bench_pause(b);

    /* Out of the loop and shut the partner down, so that the next run starts
     * with one goroutine rather than a pile of them. */
    sync_mutex_lock(&pp_mu);
    pp_stop = true;
    pp_turn = 1;
    sync_mutex_unlock(&pp_mu);
    sync_cond_broadcast(&pp_cond);

    while (sync_atomic_int64_load(&pp_done) == 0)
        runtime_gosched();
}

BENCH(cond_ping_pong) {
    bench_pause(b);
    cur = b;

    int old = runtime_gomaxprocs(2);
    runtime_main(BURROW_FN(Func, pp_body, NULL));
    (void)runtime_gomaxprocs(old);
}

void register_cond_benchmarks(void);

void register_cond_benchmarks(void) {
    BENCH_RUN(cond_signal_empty);
    BENCH_RUN(cond_broadcast_empty);
    BENCH_RUN(cond_ping_pong);
}
