/* sync.WaitGroup and the sync.Once family.
 *
 * Two different questions in one file, because they are the two halves of the
 * same slice of the port.
 *
 * The WaitGroup rows are about the counter. An Add and a Done with nobody
 * waiting is one atomic add each and should cost that on both sides, and the
 * row that starts goroutines and waits for them is mostly the scheduler rather
 * than the group, which is why both rows are here: the first says what the
 * counter costs and the second says what using it costs.
 *
 * The Once rows are about the fast path. A Once that has already run is one
 * atomic load and a branch, which is the number that matters, because the whole
 * point of a Once is that the second call and the millionth call are cheap. The
 * first call is a mutex and it happens once in the life of the program, so it
 * is not worth a row.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/func.h"
#include "burrow/iface.h"
#include "burrow/proc.h"
#include "burrow/sync.h"
#include "burrow/type.h"

#include <stdint.h>

/* ---------------------------------------------------------------- WaitGroup
 *
 * No goroutines and no runtime for the counter row, for the reason the
 * uncontended mutex rows give: an Add nobody is waiting on is one atomic add on
 * a line the caller already has, and it costs the same whoever is running. */

static SyncWaitGroup bench_wg;

/* Add one and take it straight back off, which is the pair every use of a
 * WaitGroup is made of. Two atomics on one word, plus the misuse checks, and
 * those checks are the interesting part: Go runs them too, so if this row is
 * behind, the checks are not the reason. */
BENCH(wait_group_add_done) {
    SyncWaitGroup *wg = (SyncWaitGroup *)bench_hide(&bench_wg);

    BENCH_LOOP(b) {
        sync_wait_group_add(wg, 1);
        sync_wait_group_done(wg);
    }
}

/* A Wait on a group that is already at zero, which returns without queueing.
 * One load and a compare. It is the shape of a Wait called after everything has
 * already finished, which happens more often than it sounds like it would. */
BENCH(wait_group_wait_empty) {
    SyncWaitGroup *wg = (SyncWaitGroup *)bench_hide(&bench_wg);

    BENCH_LOOP(b) {
        sync_wait_group_wait(wg);
    }
}

/* ----------------------------------------------------------- Go, then Wait
 *
 * Four goroutines started through WaitGroup.Go and waited for, per iteration.
 * This is the row that says what the thing costs in a program rather than on a
 * counter: a goroutine each way, a park and an unpark, and the group in the
 * middle of it. Four rather than one because one goroutine finishing before the
 * Wait even starts measures nothing, and four is the same crowd the contended
 * mutex rows use so the results files can be read together. */

#define WG_WORKERS 4

static Bench *cur;

static void nothing(void *env) {
    (void)env;
}

static void go_wait_body(void *env) {
    (void)env;
    Bench *b = cur;
    SyncWaitGroup wg;

    bench_resume(b);

    BENCH_LOOP(b) {
        wg = (SyncWaitGroup){0, 0};

        for (int i = 0; i < WG_WORKERS; i++) {
            if (!sync_wait_group_go(&wg, BURROW_FN(Func, nothing, NULL)))
                break;
        }
        sync_wait_group_wait(&wg);
    }

    bench_pause(b);
}

BENCH(wait_group_go_wait) {
    bench_pause(b);
    cur = b;

    /* As many Ps as there are goroutines, so the row is about the group and the
     * park rather than about four goroutines queued behind one P. The Go side
     * sets GOMAXPROCS to the same number. */
    int old = runtime_gomaxprocs(WG_WORKERS);
    runtime_main(BURROW_FN(Func, go_wait_body, NULL));
    (void)runtime_gomaxprocs(old);
}

/* --------------------------------------------------------------------- Once
 *
 * Every row below is the already ran path, measured after one warm up call, so
 * the loop is the branch that a real program takes every time but the first.
 *
 * The once_do row is about a nanosecond and Go's is about half of one, and the
 * difference is not the Once. Both sides are one load, a test and a branch. The
 * loop gcc builds around it has two taken branches per iteration and most cores
 * retire one of those a cycle, which is the whole gap. A row where the body is
 * a single load is a row where the loop is most of the measurement, and the
 * useful reading of both figures is that an already initialised Once is free. */

static int64_t once_counter;

static void bump(void *env) {
    (void)env;

    once_counter++;
}

static SyncOnce bench_once;

BENCH(once_do) {
    SyncOnce *o = (SyncOnce *)bench_hide(&bench_once);

    /* Built once and kept in a local, because the Go side passes a package
     * level function and building a two word Func inside the loop would be
     * measuring the compound literal rather than the Once. */
    Func f = BURROW_FN(Func, bump, NULL);

    sync_once_do(o, f);

    BENCH_LOOP(b) {
        sync_once_do(o, f);
    }
}

/* Through OnceFunc, which is the same load plus the check that f returned
 * rather than panicked. Go's OnceFunc is a closure and a call through a
 * function value, so the two sides are paying for different things here and the
 * row is worth reading as a whole rather than as a ratio. */
static SyncOnceFunc bench_once_func = SYNC_ONCE_FUNC(BURROW_FN(Func, bump, NULL));

BENCH(once_func_call) {
    SyncOnceFunc *of = (SyncOnceFunc *)bench_hide(&bench_once_func);

    sync_once_func_call(of);

    BENCH_LOOP(b) {
        sync_once_func_call(of);
    }
}

static int64_t once_result = 42;

static Any produce(void *env) {
    (void)env;

    return BURROW_ANY(TYPE_INT64, &once_result);
}

static SyncOnceValue bench_once_value =
    SYNC_ONCE_VALUE(BURROW_FN(AnyFunc, produce, NULL));

BENCH(once_value_get) {
    SyncOnceValue *ov = (SyncOnceValue *)bench_hide(&bench_once_value);

    (void)sync_once_value_get(ov);

    BENCH_LOOP(b) {
        Any v = sync_once_value_get(ov);
        bench_keep(v.data);
    }
}

void register_waitgroup_benchmarks(void);

void register_waitgroup_benchmarks(void) {
    BENCH_RUN(wait_group_add_done);
    BENCH_RUN(wait_group_wait_empty);
    BENCH_RUN(wait_group_go_wait);
    BENCH_RUN(once_do);
    BENCH_RUN(once_func_call);
    BENCH_RUN(once_value_get);
}
