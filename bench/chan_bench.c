/* Channels, which is the row the sync file has been waiting for.
 *
 * bench/sync_bench.c ends with a note saying that a note ping pong is two trips
 * into the kernel per volley, that a Go channel ping pong is two goroutine
 * switches that never leave the process, and that the size of that gap is the
 * size of the prize. This file is where the prize gets collected or does not.
 * `chan_pingpong_unbuffered` is the same program as `note_pingpong` written the
 * way a Go programmer writes it, and the two numbers belong side by side.
 *
 * The other row it has to be read against is `goroutine_handoff` in
 * bench/sched_bench.c. That one volleys through park and ready directly, using
 * a one waiter gate rather than a channel, because when it was written there
 * were no channels. It is the floor: a channel cannot be faster than the park
 * and ready underneath it, so the distance between `goroutine_handoff` and
 * `chan_pingpong_unbuffered` is what the channel costs on top of the primitives
 * it is built from. The Go side of `goroutine_handoff` is an unbuffered channel
 * volley, which is exactly the Go side of this file's ping pong row, so that
 * comparison was never quite fair to burrow and now it can be.
 *
 * One P everywhere, set before the runtime starts, for the reason sched_bench.c
 * gives: with more than one P a ping pong measures cache line traffic between
 * two cores, which is a real thing to measure and a different benchmark.
 *
 * Every channel here carries an Int, and the Go side uses `chan int`. Go's own
 * channel benchmarks mostly use `chan struct{}`, which is faster because there
 * is nothing to copy, and picking that would have measured the parts of a
 * channel that are not the copy. Eight bytes is what most real channels carry.
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

/* The Bench the goroutine body is running for, and the channels it is using.
 * File statics for the reason sched_bench.c gives: only one runtime_main runs
 * at a time, so a benchmark that reached the wrong one would fail loudly. */
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

/* --------------------------------------------------------------- uncontended
 *
 * A send and a receive on a buffered channel with nobody else in the program.
 * Neither one blocks, nobody is queued, and the buffer never has more than one
 * value in it, so what this costs is the lock, the ring arithmetic and the copy
 * and nothing else.
 *
 * It is the floor under every other row here and it is the row that says
 * whether one lock per channel was the right call. If a scheduler is never
 * involved and a channel still costs a hundred nanoseconds, the lock is the
 * problem. If it costs twenty, the lock is not the problem and the rows below
 * are measuring the scheduler. */

static void uncontended_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 1);
    if (c1 == NULL)
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        Int v = 1;
        chan_send(c1, &v);
        Int got = 0;
        (void)chan_recv(c1, &got);
        bench_keep_u64((uint64_t)got);
    }

    bench_pause(b);

    chan_free(c1);
    c1 = NULL;
}

BENCH(chan_uncontended) {
    run_with_one_p(b, uncontended_body);
}

/* ------------------------------------------------------------- not waiting
 *
 * A receive from an empty channel that answers rather than blocking, which is
 * Go's select with a default arm and is what `chan_try_recv` exists for.
 *
 * This is the path with the unlocked early reject on it. A poll loop that never
 * finds anything pays exactly this per turn, so it is worth knowing that it
 * does not take the lock, and it is worth having a row that would notice if
 * somebody made it take the lock. */

static void nonblocking_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, 1);
    if (c1 == NULL)
        return;

    bench_resume(b);

    BENCH_LOOP(b) {
        Int got = 0;
        bench_keep_u64(chan_try_recv(c1, &got, NULL) ? 1u : 0u);
    }

    bench_pause(b);

    chan_free(c1);
    c1 = NULL;
}

BENCH(chan_nonblocking) {
    run_with_one_p(b, nonblocking_body);
}

/* ------------------------------------------------------------- the ping pong
 *
 * Two goroutines and a value going there and back. One iteration is a volley:
 * a send on one channel, a receive on the other, and the same in reverse on the
 * far side. Two goroutine switches, and on the unbuffered pair, four blocking
 * operations.
 *
 * This is the row. It is what a request handler handing work to a worker and
 * waiting for the answer costs, it is what `note_pingpong` costs the expensive
 * way, and it is the thing Go's scheduler is famous for being fast at.
 *
 * Two versions of it, because they take different paths through the channel and
 * a single number would hide that. The unbuffered one is a rendezvous every
 * time: the sender parks because the receiver has not arrived yet, or hands
 * straight over because it has. The buffered one has a capacity of one, so a
 * send with the buffer empty never blocks at all and only the receive does. A
 * program that does not need lockstep gets the second one by changing a zero to
 * a one, and the difference between these two rows is what that change buys. */

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

static void pingpong(Bench *b, Int cap) {
    c1 = chan_make(heap_allocator(), TYPE_INT, cap);
    c2 = chan_make(heap_allocator(), TYPE_INT, cap);
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

    /* Stop the child with one more volley rather than by closing, because a
     * close would make the last iteration take a different path through the
     * channel than every other one. */
    burrow__atomic_store_u32(&pp_stop, 1);
    Int v = 0;
    chan_send(c1, &v);
    while (burrow__atomic_load_acquire_u32(&pp_gone) == 0)
        runtime_gosched();

    chan_free(c1);
    chan_free(c2);
    c1 = NULL;
    c2 = NULL;
}

static void pingpong_unbuffered_body(void *env) {
    (void)env;
    pingpong(cur, 0);
}

BENCH(chan_pingpong_unbuffered) {
    pp_stop = 0;
    pp_gone = 0;
    run_with_one_p(b, pingpong_unbuffered_body);
}

static void pingpong_buffered_body(void *env) {
    (void)env;
    pingpong(cur, 1);
}

BENCH(chan_pingpong_buffered) {
    pp_stop = 0;
    pp_gone = 0;
    run_with_one_p(b, pingpong_buffered_body);
}

/* ------------------------------------------------------ producer to consumer
 *
 * One value moving one way down a buffered channel, which is the shape of every
 * pipeline anybody has ever written. The producer runs ahead until the buffer
 * fills and then blocks, the consumer drains and unblocks it, and the cost per
 * value is what this reports.
 *
 * A capacity of a hundred rather than one, because that is the point of a
 * buffer: a value that arrives while there is room costs a lock and a copy and
 * no scheduling at all, and a pipeline only pays for the scheduler once per
 * buffer full rather than once per value. If this row is not several times
 * cheaper than the ping pong rows, the buffered path is not doing its job. */

#define PRODCONS_CAP 100

static void prodcons_producer(void *env) {
    (void)env;
    Bench *b = cur;

    for (int64_t i = 0; i < b->n; i++) {
        Int v = (Int)i;
        chan_send(c1, &v);
    }

    chan_close(c1);
}

static void prodcons_body(void *env) {
    (void)env;
    Bench *b = cur;

    c1 = chan_make(heap_allocator(), TYPE_INT, PRODCONS_CAP);
    if (c1 == NULL)
        return;

    if (!go(BURROW_FN(Func, prodcons_producer, NULL)))
        return;

    bench_resume(b);

    Int got = 0;
    uint64_t sum = 0;
    while (chan_recv(c1, &got))
        sum += (uint64_t)got;

    bench_pause(b);
    bench_keep_u64(sum);

    chan_free(c1);
    c1 = NULL;
}

BENCH(chan_prodcons) {
    run_with_one_p(b, prodcons_body);
}

void register_chan_benchmarks(void);

void register_chan_benchmarks(void) {
    BENCH_RUN(chan_uncontended);
    BENCH_RUN(chan_nonblocking);
    BENCH_RUN(chan_pingpong_unbuffered);
    BENCH_RUN(chan_pingpong_buffered);
    BENCH_RUN(chan_prodcons);
}
