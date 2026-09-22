/* What a bubble costs.
 *
 * synctest is the package that lets a test with concurrency in it be written
 * without a sleep, and the way it does that is by putting a set of counters
 * through the middle of the scheduler. Every park in the bubble goes through
 * one more branch than it did before, and the clock the bubble runs on is not
 * the machine's. Neither of those is free and both of them are on the path of
 * every test that ever uses the package, so they are worth a number.
 *
 * Most of these rows have a twin next door rather than a Go column being the
 * only thing to read them against. `synctest_chan_pingpong` belongs beside
 * `chan_pingpong_unbuffered` in bench/chan_bench.c and `synctest_go` beside
 * `goroutine_start` in bench/sched_bench.c, and in both cases the distance
 * between the two is the answer: it is what a goroutine pays for being inside a
 * bubble rather than outside one. The Go column says whether that price is in
 * line with the package this is a port of.
 *
 * One P everywhere, the same as chan_bench.c and sched_bench.c and for the same
 * reason, and the Go side sets GOMAXPROCS to 1 to match. A bubble does not care
 * how many Ps there are, but the ping pong row does, and a row that moves when
 * the machine is busy is not measuring the bubble.
 *
 * `synctest_sleep` is the row that looks wrong and is not. A sleep of a second
 * inside a bubble costs what a function call costs, because the clock moves
 * when everybody is blocked rather than when the machine says so. That is the
 * whole point of the package and this is the row that shows it. It is not a
 * comparison against anything, because the thing it would be compared against
 * takes a second.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/atomic.h"
#include "burrow/chan.h"
#include "burrow/func.h"
#include "burrow/mem/heap.h"
#include "burrow/proc.h"
#include "burrow/synctest.h"
#include "burrow/time.h"
#include "burrow/type.h"

#include <stddef.h>
#include <stdint.h>

/* The Bench the goroutine bodies are running for. A file static for the reason
 * sched_bench.c gives: only one runtime_main runs at a time, so a benchmark
 * that reached the wrong one would fail loudly rather than quietly. */
static Bench *cur;

static Chan *c1;
static Chan *c2;

static void run_with_one_p(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    cur = b;

    int old = runtime_gomaxprocs(1);
    runtime_main(BURROW_FN(Func, body, NULL));
    (void)runtime_gomaxprocs(old);
}

/* --------------------------------------------------------- opening and closing
 *
 * A bubble from the call to the return with nothing inside it. That is a
 * goroutine for the body, a set of counters, and the wait for the last
 * goroutine that was ever in the bubble to exit, which here is the body itself.
 *
 * It is the fixed cost of the package. A test suite pays it once per test and a
 * test does rather more than nothing, so this row is not a thing to optimise
 * against. It is a thing to know, and the one to notice if it ever starts
 * growing.
 *
 * The second row puts one goroutine in the bubble that returns immediately, so
 * the difference between the two is what tracking one extra member costs from
 * the bubble's side. */

static void empty_body(void *env) {
    (void)env;
}

static void run_empty_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        (void)synctest_run(BURROW_FN(Func, empty_body, NULL));
    }

    bench_pause(b);
}

BENCH(synctest_run_empty) {
    run_with_one_p(b, run_empty_body);
}

static uint32_t member_ran;

static void member_child(void *env) {
    (void)env;
    (void)burrow__atomic_add_u32(&member_ran, 1);
}

static void member_body(void *env) {
    (void)env;
    (void)go(BURROW_FN(Func, member_child, NULL));
}

static void run_member_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        (void)synctest_run(BURROW_FN(Func, member_body, NULL));
    }

    bench_pause(b);
}

BENCH(synctest_run_one_goroutine) {
    member_ran = 0;
    run_with_one_p(b, run_member_body);
    bench_keep_u64(member_ran);
}

/* ------------------------------------------------------------------ the wait
 *
 * synctest_wait is the call that replaces the sleep, so it is the one a test
 * makes over and over and the one that decides whether a suite written this way
 * is quick.
 *
 * Two rows, because the call has two jobs and a single number would hide one of
 * them. The first is a wait in an empty bubble, where the answer is already
 * true when the question is asked: nobody else is in here, so there is nothing
 * to be blocked. That is the check on its own with no scheduling in it.
 *
 * The second has a goroutine parked on a bubbled channel, so the wait has to
 * see that the park happened and come back, and then the body hands over a
 * value and the child goes round and parks again. That is one wait, one send
 * and one park per iteration, which is what the inside of a real test loop
 * looks like. Read it against `chan_pingpong_unbuffered`: the distance is what
 * asking the question costs on top of the handoff that answers it. */

static void wait_alone_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        synctest_wait();
    }

    bench_pause(b);
}

static void wait_alone_top(void *env) {
    (void)env;
    (void)synctest_run(BURROW_FN(Func, wait_alone_body, NULL));
}

BENCH(synctest_wait_alone) {
    run_with_one_p(b, wait_alone_top);
}

static uint32_t parked_got;

static void parked_child(void *env) {
    (void)env;

    for (;;) {
        Int v = 0;
        if (!chan_recv(c1, &v))
            break;
        (void)burrow__atomic_add_u32(&parked_got, 1);
    }
}

static void wait_parked_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 0);
    if (c1 == NULL)
        return;
    if (!go(BURROW_FN(Func, parked_child, NULL)))
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        synctest_wait();
        Int v = 1;
        (void)chan_try_send(c1, &v);
    }

    bench_pause(b);

    /* Closing is what ends the child, and it has to happen before this returns
     * or the bubble has a goroutine blocked in it after its body is done, which
     * is the leak the package stops the program for. */
    chan_close(c1);
    synctest_wait();
}

static void wait_parked_top(void *env) {
    (void)env;

    (void)synctest_run(BURROW_FN(Func, wait_parked_body, NULL));
    chan_free(c1);
    c1 = NULL;
}

BENCH(synctest_wait_parked) {
    parked_got = 0;
    run_with_one_p(b, wait_parked_top);
    bench_keep_u64(parked_got);
}

/* ----------------------------------------------------------- inside a bubble
 *
 * The two rows that have a twin outside. Nothing here is about synctest as a
 * feature, it is the same work done in the same way with the bubble's counters
 * switched on, and the pair of numbers is the tax.
 *
 * The ping pong is the one from bench/chan_bench.c with the channels made
 * inside the bubble, which is what makes the waits durable and the counters
 * actually do something. If it were made outside, the receive would not be a
 * durable wait and this would be measuring a channel with a branch in front
 * of it.
 *
 * The goroutine row is `goroutine_start` from bench/sched_bench.c, launch and
 * switch in and switch out, with the bubble's membership bookkeeping on top:
 * every goroutine started in here joins the bubble and every one that exits
 * leaves it. */

static uint32_t pp_stop;
static uint32_t pp_gone;

static void pingpong_child(void *env) {
    (void)env;

    for (;;) {
        Int v = 0;
        if (!chan_recv(c1, &v))
            break;

        if (burrow__atomic_load_acquire_u32(&pp_stop) != 0)
            break;

        chan_send(c2, &v);
    }

    (void)burrow__atomic_add_u32(&pp_gone, 1);
}

static void pingpong_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 0);
    c2 = chan_make(heap_allocator(), TYPE_INT, 0);
    if (c1 == NULL || c2 == NULL)
        return;
    if (!go(BURROW_FN(Func, pingpong_child, NULL)))
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        Int v = 1;
        chan_send(c1, &v);
        Int got = 0;
        (void)chan_recv(c2, &got);
    }

    bench_pause(b);

    /* One more volley rather than a close, the same as the row next door, so
     * that the last iteration takes the same path through the channel as every
     * other one. */
    burrow__atomic_store_u32(&pp_stop, 1);
    Int v = 0;
    chan_send(c1, &v);
    synctest_wait();
}

static void pingpong_top(void *env) {
    (void)env;

    (void)synctest_run(BURROW_FN(Func, pingpong_body, NULL));
    chan_free(c1);
    chan_free(c2);
    c1 = NULL;
    c2 = NULL;
}

BENCH(synctest_chan_pingpong) {
    pp_stop = 0;
    pp_gone = 0;
    run_with_one_p(b, pingpong_top);
    bench_keep_u64(pp_gone);
}

static uint32_t start_ran;

static void start_child(void *env) {
    (void)env;
    (void)burrow__atomic_add_u32(&start_ran, 1);
}

static void start_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        if (!go(BURROW_FN(Func, start_child, NULL)))
            break;
        /* Hands the turn over so the child runs now rather than piling up. It
         * went into the runnext slot, so this is the switch straight into it. */
        runtime_gosched();
    }

    bench_pause(b);
}

static void start_top(void *env) {
    (void)env;
    (void)synctest_run(BURROW_FN(Func, start_body, NULL));
}

BENCH(synctest_go) {
    start_ran = 0;
    run_with_one_p(b, start_top);
    bench_keep_u64(start_ran);
}

/* ------------------------------------------------------------- the fake clock
 *
 * A sleep of a second that takes nanoseconds.
 *
 * Inside a bubble the clock does not come from the machine. It moves when every
 * goroutine in the bubble is blocked and there is a timer due, and it moves
 * straight to that timer. So a sleep is an arm, a park, a wake and a disarm,
 * and the length of it does not appear in the cost at all. A second is used
 * here rather than a millisecond to make that obvious.
 *
 * There is no row to read this against. The thing it replaces is a real sleep,
 * and a real sleep of a second costs a second no matter who implements it.
 * `timer_sleep_1ms` in bench/timer_bench.c is the closest thing and it is
 * measuring the other kind of clock. */

static void sleep_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        time_sleep(TIME_SECOND);
    }

    bench_pause(b);
}

static void sleep_top(void *env) {
    (void)env;
    (void)synctest_run(BURROW_FN(Func, sleep_body, NULL));
}

BENCH(synctest_sleep) {
    run_with_one_p(b, sleep_top);
}

void register_synctest_benchmarks(void);

void register_synctest_benchmarks(void) {
    BENCH_RUN(synctest_run_empty);
    BENCH_RUN(synctest_run_one_goroutine);
    BENCH_RUN(synctest_wait_alone);
    BENCH_RUN(synctest_wait_parked);
    BENCH_RUN(synctest_chan_pingpong);
    BENCH_RUN(synctest_go);
    BENCH_RUN(synctest_sleep);
}
