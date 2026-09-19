/* What the scheduler costs, measured through the public goroutine calls.
 *
 * The runtime benchmarks next door measure the parts a goroutine is assembled
 * from: a stack, a context, a run queue. These measure the thing built on top,
 * so every row here is a row there plus the scheduler's own work, and the
 * difference between the two files is what the scheduler costs to have.
 *
 * Every one of these pairs with a Go benchmark, which the runtime rows mostly
 * could not do. That is the point of the scheduler landing: `go f()` and
 * `runtime.Gosched()` and a blocking handoff are things both languages have, so
 * the comparison is finally between two programs doing the same work rather
 * than between a floor and a ceiling.
 *
 * One P everywhere, set with runtime_gomaxprocs before the runtime starts, and
 * the Go side does the same with runtime.GOMAXPROCS(1). Not because one P is
 * the interesting case for a real program, but because it is the only case
 * where a handoff is a handoff. With more than one P the two goroutines end up
 * on two cores and the benchmark measures cache line ping pong between them,
 * which is a real thing to measure and is a different benchmark. That one goes
 * in when there is a contended row to put next to it.
 *
 * Each benchmark runs inside runtime_main, since go and gosched and park are
 * only callable while the scheduler is up. The clock is stopped across the
 * start and the stop of the runtime, so what is timed is the loop and not the
 * thread creation around it.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/atomic.h"
#include "burrow/func.h"
#include "burrow/proc.h"
#include "burrow/sched.h"

#include <stdint.h>
#include <string.h>

/* The Bench the goroutine body is running for.
 *
 * runtime_main takes a Func, which carries an env pointer, so this could be
 * passed through properly. It is a file static instead because every benchmark
 * here needs the same thing and a benchmark that reached the wrong one would
 * fail loudly rather than quietly: only one runtime_main runs at a time. */
static Bench *cur;

/* Runs body as the main goroutine with one P, timing only what body times.
 *
 * The pattern in every benchmark below is the same three lines, so it is one
 * function. The clock is already running when a benchmark is entered, so this
 * stops it, and body starts it again once its own setup is done. */
static void run_with_one_p(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    cur = b;

    int old = runtime_gomaxprocs(1);
    runtime_main(BURROW_FN(Func, body, NULL));
    (void)runtime_gomaxprocs(old);
}

/* ------------------------------------------------------------- starting one
 *
 * A launch and the switch into it and out of it again, which is what `go f()`
 * costs in the only sense worth quoting. A launch on its own is a stack, a
 * context and a queue put, and it is already measured in runtime_bench.c. What
 * is not measured there is the rest of the goroutine's life, and a program that
 * starts a goroutine it never runs has not started a goroutine.
 *
 * It also keeps the memory bounded. A loop that launches without running would
 * have a million live goroutines in it by the end and would be measuring the
 * machine running out of address space. */

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

BENCH(goroutine_start) {
    start_ran = 0;
    run_with_one_p(b, start_body);
    bench_keep_u64(start_ran);
}

/* --------------------------------------------------------- starting a batch
 *
 * Sixty four at a time, then wait for all of them. Divide by 64 for the cost of
 * one.
 *
 * This is the same work as the row above and a different shape of it, and the
 * two numbers are worth having next to each other. One at a time is the best
 * case: the child is in the runnext slot, it runs on a hot cache, and its stack
 * comes straight back off the free list the moment it exits. A batch is the
 * case a server actually produces, where the ring fills, the free list runs dry
 * and the launcher is not the next thing to run. If the two rows are close the
 * queue and the stack cache are doing their job, and if they are far apart the
 * gap is where to look.
 *
 * Sixty four and not two hundred and fifty six because the local run queue
 * holds 256 and a batch that overflows it is measuring the global queue and the
 * lock in front of it. That is worth a row of its own later, once there is a
 * reason to believe the answer. */

#define SCHED_BATCH 64

static uint32_t batch_done;

static void batch_child(void *env) {
    (void)env;
    (void)burrow__atomic_add_u32(&batch_done, 1);
}

static void batch_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        burrow__atomic_store_u32(&batch_done, 0);

        for (int i = 0; i < SCHED_BATCH; i++) {
            if (!go(BURROW_FN(Func, batch_child, NULL)))
                break;
        }

        /* Yielding rather than parking, because parking needs something to wake
         * it and a WaitGroup is two milestones away. The Go side spins on the
         * same counter with the same Gosched for the same reason, so the two
         * are measuring the same program. */
        while (burrow__atomic_load_acquire_u32(&batch_done) < SCHED_BATCH)
            runtime_gosched();
    }

    bench_pause(b);
}

BENCH(goroutine_start_batch) {
    batch_done = 0;
    run_with_one_p(b, batch_body);
    bench_keep_u64(batch_done);
}

/* ------------------------------------------------------------------ yielding
 *
 * runtime_gosched with an empty run queue, which is the scheduler deciding
 * there is nothing else to do and coming straight back. No context switch
 * happens at all on this path, so what it costs is the cost of asking.
 *
 * It is worth a row because it is the floor under every other row in this file,
 * and because a compute loop that calls gosched to be polite pays this on every
 * call and gets nothing for it. When preemption lands, those loops stop needing
 * the call and this row stops mattering. */

static void yield_alone_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        runtime_gosched();
    }

    bench_pause(b);
}

BENCH(goroutine_yield_alone) {
    run_with_one_p(b, yield_alone_body);
}

/* Two goroutines yielding to each other, so every call is a real switch. One
 * iteration is two of them, there and back, and halving it is one.
 *
 * This is the handoff with nothing in the way: no lock, no park, no wakeup,
 * just the run queue handing the turn to the only other thing on it. The row
 * below does the same volley through park and ready, and the distance between
 * the two is what blocking costs over yielding. */

static uint32_t pair_stop;
static uint32_t pair_gone;

static void pair_child(void *env) {
    (void)env;

    while (burrow__atomic_load_acquire_u32(&pair_stop) == 0)
        runtime_gosched();

    (void)burrow__atomic_add_u32(&pair_gone, 1);
}

static void pair_body(void *env) {
    (void)env;
    Bench *b = cur;

    if (!go(BURROW_FN(Func, pair_child, NULL)))
        return;

    /* One turn so the child is running and on the queue before the clock
     * starts, rather than paying for its launch on the first iteration. */
    runtime_gosched();

    bench_resume(b);

    BENCH_LOOP(b) {
        runtime_gosched();
    }

    bench_pause(b);

    burrow__atomic_store_u32(&pair_stop, 1);
    while (burrow__atomic_load_acquire_u32(&pair_gone) == 0)
        runtime_gosched();
}

BENCH(goroutine_yield_pair) {
    pair_stop = 0;
    pair_gone = 0;
    run_with_one_p(b, pair_body);
}

/* ------------------------------------------------------------ blocking handoff
 *
 * Two goroutines passing a turn through park and ready, which is what a channel
 * send and receive will be built out of when channels land. One iteration is a
 * volley: this goroutine wakes the other and blocks, the other wakes it back
 * and blocks again. Two goroutine switches, two parks and two readies.
 *
 * The gate below is the smallest thing that uses the pair correctly, and it is
 * a copy of the one in burrow's own scheduler test rather than something from
 * the library, because the library does not export one and should not. A
 * channel is not a one waiter gate and building the general thing here would be
 * benchmarking a guess at the implementation instead of the primitives.
 *
 * The Go pair is two goroutines volleying on an unbuffered channel, which is
 * the same program with the real thing in the middle. Go's number therefore has
 * a channel in it that this one does not, so this row being faster is expected
 * and is not yet a win. It becomes a comparison the day channels land, and
 * until then it is the budget the channel has to fit inside. */

typedef struct Gate {
    burrow__Lock lock;
    Goroutine *waiter;
    uint32_t signalled;
} Gate;

static bool gate_unlock(Goroutine *g, void *arg) {
    (void)g;
    burrow__unlock(&((Gate *)arg)->lock);
    return true;
}

static void gate_wait(Gate *gt) {
    burrow__lock(&gt->lock);

    if (gt->signalled != 0) {
        gt->signalled = 0;
        burrow__unlock(&gt->lock);
        return;
    }

    gt->waiter = sched_current();
    sched_park(gate_unlock, gt);
}

static void gate_signal(Gate *gt) {
    burrow__lock(&gt->lock);

    Goroutine *w = gt->waiter;
    if (w == NULL) {
        gt->signalled = 1;
        burrow__unlock(&gt->lock);
        return;
    }

    gt->waiter = NULL;
    burrow__unlock(&gt->lock);
    sched_ready(w);
}

static Gate hand_there;
static Gate hand_back;
static uint32_t hand_stop;
static uint32_t hand_gone;

static void hand_child(void *env) {
    (void)env;

    for (;;) {
        gate_wait(&hand_there);

        /* Read before the signal, because after it this goroutine is racing the
         * other one to finish and the flag may have been reused. */
        bool stop = burrow__atomic_load_acquire_u32(&hand_stop) != 0;
        gate_signal(&hand_back);

        if (stop)
            break;
    }

    (void)burrow__atomic_add_u32(&hand_gone, 1);
}

static void hand_body(void *env) {
    (void)env;
    Bench *b = cur;

    if (!go(BURROW_FN(Func, hand_child, NULL)))
        return;

    /* One volley outside the clock, which starts the child and leaves it parked
     * on the gate, so the first timed iteration is a wakeup and not a launch. */
    gate_signal(&hand_there);
    gate_wait(&hand_back);

    bench_resume(b);

    BENCH_LOOP(b) {
        gate_signal(&hand_there);
        gate_wait(&hand_back);
    }

    bench_pause(b);

    burrow__atomic_store_u32(&hand_stop, 1);
    gate_signal(&hand_there);
    gate_wait(&hand_back);

    while (burrow__atomic_load_acquire_u32(&hand_gone) == 0)
        runtime_gosched();
}

BENCH(goroutine_handoff) {
    memset(&hand_there, 0, sizeof hand_there);
    memset(&hand_back, 0, sizeof hand_back);
    hand_stop = 0;
    hand_gone = 0;

    run_with_one_p(b, hand_body);
}

/* ------------------------------------------------------- starting the runtime
 *
 * runtime_main and back out again with a main goroutine that does nothing. Ps
 * created, threads started, one goroutine run, then every thread stopped and
 * joined.
 *
 * Go has no pair for this and cannot have one, since starting the Go runtime
 * means starting a process. It is here because burrow can be started and
 * stopped many times in one program, which is a thing Go cannot do and which
 * anybody embedding burrow in a larger C program is going to do, and because a
 * number nobody is watching is a number that grows. */

static void empty_body(void *env) {
    (void)env;
}

BENCH(runtime_start_stop) {
    BENCH_LOOP(b) {
        runtime_main(BURROW_FN(Func, empty_body, NULL));
    }
}

void register_sched_benchmarks(void);

void register_sched_benchmarks(void) {
    BENCH_RUN(goroutine_start);
    BENCH_RUN(goroutine_start_batch);
    BENCH_RUN(goroutine_yield_alone);
    BENCH_RUN(goroutine_yield_pair);
    BENCH_RUN(goroutine_handoff);
    BENCH_RUN(runtime_start_stop);
}
