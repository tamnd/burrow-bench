/* select, which is a channel operation with a decision on the front of it.
 *
 * Every row here has a row in bench/chan_bench.c that it should be read
 * against, because the question this file exists to answer is what the decision
 * costs. A select over one ready case does the same copy a bare receive does
 * and everything else it does is overhead: lock every channel in the list in
 * address order, walk them in a random order looking for one that can run, and
 * unlock them again. `select_uncontended` next to `chan_uncontended` is that
 * overhead with no scheduler anywhere near it, and `select_pingpong` next to
 * `chan_pingpong_unbuffered` is the same question asked of a select that has to
 * wait, where the claim and the leftover entries are extra work a plain receive
 * never does.
 *
 * `select_eight_arms` is the row that watches one specific decision. burrow
 * takes the locks by walking the case list for the lowest address above the
 * last one it locked, which is quadratic in the number of arms, where Go sorts
 * a scratch array and walks that. Sorting is better asymptotically and the
 * claim in docs/design/06-runtime.md is that at the sizes real code uses the
 * walk is cheaper, because it needs no storage and never leaves the cache line
 * the case list is already in. Eight arms against two is what that claim looks
 * like as a number.
 *
 * One P everywhere, set before the runtime starts, for the reason
 * bench/sched_bench.c gives: with more than one P a ping pong measures the
 * cache line two cores are fighting over, which is a real thing to measure and
 * a different benchmark.
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

#include <stdint.h>

/* How many arms the wide row has. Eight because it is past the point where a
 * select is written by hand and still inside the sixteen that fit in the
 * caller's frame, so this measures the walk and not the allocation. */
#define ARMS 8

/* The Bench the goroutine body is running for, and the channels it is using.
 * File statics for the reason bench/chan_bench.c gives: only one runtime_main
 * runs at a time, so a benchmark that reached the wrong one would fail loudly
 * rather than quietly measure something else. */
static Bench *cur;
static Chan *c1;
static Chan *c2;
static Chan *quit;
static Chan *wide[ARMS];

static void run_with_one_p(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    cur = b;

    int old = runtime_gomaxprocs(1);
    runtime_main(BURROW_FN(Func, body, NULL));
    (void)runtime_gomaxprocs(old);
}

/* --------------------------------------------------------------- one ready
 *
 * One value bouncing between two buffered channels, so that exactly one of the
 * two arms can run on every pass and neither of them ever waits. This is Go's
 * own BenchmarkSelectUncontended written without the parallel wrapper, and it
 * is the floor under every other row here.
 *
 * What it costs is two channel locks, the shuffle, one pass over two cases, the
 * copy, and the unlock. `chan_uncontended` next door is the same copy with none
 * of the rest of it. */

static void uncontended_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 1);
    c2 = chan_make(heap_allocator(), TYPE_INT, 1);
    if (c1 == NULL || c2 == NULL)
        return;

    Int seed = 1;
    chan_send(c1, &seed);

    bench_resume(b);

    BENCH_LOOP(b) {
        Int got = 0;
        SelectCase cases[] = {
            BURROW_RECV(c1, &got),
            BURROW_RECV(c2, &got),
        };

        if (chan_select(cases, 2) == 0)
            chan_send(c2, &got);
        else
            chan_send(c1, &got);
    }

    bench_pause(b);

    chan_free(c1);
    chan_free(c2);
    c1 = NULL;
    c2 = NULL;
}

BENCH(select_uncontended) {
    run_with_one_p(b, uncontended_body);
}

/* ------------------------------------------------------------ nothing ready
 *
 * Two empty channels and a default arm, which is the poll loop: a select that
 * answers rather than waiting, every time, for as long as the program keeps
 * asking.
 *
 * It is the row that would notice a select which takes the locks before it
 * works out that it has a default arm, and it is the one to compare against
 * `chan_nonblocking`, which is the same question with one channel and no case
 * list at all. */

static void default_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 1);
    c2 = chan_make(heap_allocator(), TYPE_INT, 1);
    if (c1 == NULL || c2 == NULL)
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        Int got = 0;
        SelectCase cases[] = {
            BURROW_RECV(c1, &got),
            BURROW_RECV(c2, &got),
            BURROW_DEFAULT,
        };

        bench_keep_u64((uint64_t)chan_select(cases, 3));
    }

    bench_pause(b);

    chan_free(c1);
    chan_free(c2);
    c1 = NULL;
    c2 = NULL;
}

BENCH(select_default) {
    run_with_one_p(b, default_body);
}

/* -------------------------------------------------------------- eight arms
 *
 * The same one ready arm as the first row, with six more that are not. The
 * value moves from one channel to the next around the ring, so the arm that is
 * ready is a different one every pass and no ordering of the case list is being
 * flattered.
 *
 * Divide the difference between this and `select_uncontended` by six and that
 * is what one more arm on a select costs. */

static void eight_arms_body(void *env) {
    (void)env;
    Bench *b = cur;

    for (int i = 0; i < ARMS; i++) {
        wide[i] = chan_make(heap_allocator(), TYPE_INT, 1);
        if (wide[i] == NULL)
            return;
    }

    Int seed = 1;
    chan_send(wide[0], &seed);

    bench_resume(b);

    BENCH_LOOP(b) {
        Int got = 0;
        SelectCase cases[] = {
            BURROW_RECV(wide[0], &got), BURROW_RECV(wide[1], &got),
            BURROW_RECV(wide[2], &got), BURROW_RECV(wide[3], &got),
            BURROW_RECV(wide[4], &got), BURROW_RECV(wide[5], &got),
            BURROW_RECV(wide[6], &got), BURROW_RECV(wide[7], &got),
        };

        Int which = chan_select(cases, ARMS);
        chan_send(wide[(which + 1) % ARMS], &got);
    }

    bench_pause(b);

    for (int i = 0; i < ARMS; i++) {
        chan_free(wide[i]);
        wide[i] = NULL;
    }
}

BENCH(select_eight_arms) {
    run_with_one_p(b, eight_arms_body);
}

/* ------------------------------------------------------------- the ping pong
 *
 * Two goroutines and a value going there and back, where every one of the four
 * operations in a volley is a select rather than a bare send or receive. The
 * second arm is a quit channel nobody ever sends on, which is how most real
 * select loops are written: one channel carrying work and one saying stop.
 *
 * This is the row that measures the parts of select a first pass never reaches.
 * A select that finds nothing has to put an entry on every channel in the list,
 * go to sleep, be claimed by exactly one of those entries, and then take the
 * others off again. Next to `chan_pingpong_unbuffered`, which is the same
 * volley through four bare operations, the difference is the price of being
 * able to wait on more than one thing at a time. */

static uint32_t pp_stop;
static uint32_t pp_gone;

static void pingpong_child(void *env) {
    (void)env;

    for (;;) {
        Int v = 0;
        SelectCase in[] = {
            BURROW_RECV(c1, &v),
            BURROW_RECV(quit, &v),
        };
        if (chan_select(in, 2) != 0)
            break;

        if (burrow__atomic_load_acquire_u32(&pp_stop) != 0)
            break;

        SelectCase out[] = {
            BURROW_SEND(c2, &v),
            BURROW_RECV(quit, &v),
        };
        if (chan_select(out, 2) != 0)
            break;
    }

    (void)burrow__atomic_add_u32(&pp_gone, 1);
}

static void pingpong_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 0);
    c2 = chan_make(heap_allocator(), TYPE_INT, 0);
    quit = chan_make(heap_allocator(), TYPE_INT, 0);
    if (c1 == NULL || c2 == NULL || quit == NULL)
        return;

    if (!go(BURROW_FN(Func, pingpong_child, NULL)))
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        Int v = 1;
        SelectCase out[] = {
            BURROW_SEND(c1, &v),
            BURROW_RECV(quit, &v),
        };
        (void)chan_select(out, 2);

        Int got = 0;
        SelectCase in[] = {
            BURROW_RECV(c2, &got),
            BURROW_RECV(quit, &got),
        };
        (void)chan_select(in, 2);
    }

    bench_pause(b);

    /* One more volley with the flag set, rather than a close, so that the last
     * iteration took the same path through select as every other one. */
    burrow__atomic_store_u32(&pp_stop, 1);
    Int v = 0;
    chan_send(c1, &v);
    while (burrow__atomic_load_acquire_u32(&pp_gone) == 0)
        runtime_gosched();

    chan_free(c1);
    chan_free(c2);
    chan_free(quit);
    c1 = NULL;
    c2 = NULL;
    quit = NULL;
}

BENCH(select_pingpong) {
    pp_stop = 0;
    pp_gone = 0;
    run_with_one_p(b, pingpong_body);
}

void register_select_benchmarks(void);

void register_select_benchmarks(void) {
    BENCH_RUN(select_uncontended);
    BENCH_RUN(select_default);
    BENCH_RUN(select_eight_arms);
    BENCH_RUN(select_pingpong);
}
