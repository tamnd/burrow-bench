/* What a timer costs, from the calls a program actually makes.
 *
 * The rows here are about one thing: a server that puts a deadline on every
 * read. That program arms a timer, does the read, and cancels the timer, a few
 * hundred thousand times a second, and it never lets a single one of them fire.
 * If that round trip is expensive then deadlines are expensive, and deadlines
 * being expensive is how a network library ends up with an option to turn them
 * off.
 *
 * So the first three rows are arm and cancel, from three different starting
 * points: a fresh timer including its memory, an existing timer, and an existing
 * timer on a P that is already holding a thousand others. The fourth is what a
 * sleep actually costs over what it asked for, and the fifth is the other half
 * of the design, which is what happens when timers really do fire.
 *
 * One P everywhere, the same as the scheduler benchmarks next door and for the
 * same reason. Timers live in a heap per P, so with more than one P the work
 * spreads out and the row measures the machine rather than the timer. What
 * contention across Ps costs is worth a row and it is a different benchmark: on
 * this design it should be nothing at all, which is a claim worth checking once
 * there is something to check it against.
 *
 * Everything runs inside runtime_main, because arming a timer puts it in the
 * heap of the P the caller is on and there is no P outside the runtime.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/atomic.h"
#include "burrow/clock.h"
#include "burrow/func.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/proc.h"
#include "burrow/time.h"

#include <stdint.h>

/* The Bench the goroutine body is running for, and the arena the timers come
 * from. File static for the reason sched_bench.c gives: one runtime_main runs at
 * a time, so a body that reached the wrong one could not exist. */
static Bench *cur;
static Arena timer_arena;

/* An hour, which is the deadline every row below uses when it does not want the
 * timer to fire. Long enough that nothing in a benchmark run can reach it, and a
 * plain number rather than something the compiler can fold away. */
#define NEVER (TIME_HOUR)

/* Runs body as the main goroutine with one P and an arena to allocate from.
 *
 * The arena rather than the heap allocator because a timer is 80 bytes and the
 * row being measured is the timer, not malloc. mem_bench.c has the rows for
 * malloc and they are three times this whole measurement.
 */
static void run_with_one_p(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    cur = b;

    arena_init(&timer_arena, NULL, 0);

    int old = runtime_gomaxprocs(1);
    runtime_main(BURROW_FN(Func, body, NULL));
    (void)runtime_gomaxprocs(old);

    arena_free(&timer_arena);
}

/* Never runs in three of the five rows, and the compiler is not allowed to know
 * that. */
static uint32_t fired;

static void on_fire(void *env) {
    (void)env;
    (void)burrow__atomic_add_u32(&fired, 1);
}

/* ------------------------------------------------------------ arm and cancel
 *
 * A whole timer, from nothing to stopped: allocate it, arm it for an hour away,
 * stop it before it can fire, hand the memory back.
 *
 * This is the honest version of the deadline round trip for a program that does
 * not keep its timers, and it is the one that pairs with Go's AfterFunc and
 * Stop, because Go allocates there too. The row below it is the same work for a
 * program that does keep them, and the distance between the two rows is the
 * allocation and nothing else. */

static void arm_stop_body(void *env) {
    (void)env;
    Bench *b = cur;
    Alloc *a = arena_allocator(&timer_arena);

    bench_resume(b);

    BENCH_LOOP(b) {
        TimeTimer *t = time_after_func(a, NEVER, BURROW_FN(Func, on_fire, NULL));
        if (t == NULL)
            break;

        (void)time_timer_stop(t);
        time_timer_free(t);
    }

    bench_pause(b);
}

BENCH(timer_arm_stop) {
    run_with_one_p(b, arm_stop_body);
}

/* ------------------------------------------------------- reset and cancel
 *
 * One timer, armed and stopped over and over. This is the row that matters.
 *
 * A connection with a read deadline on it does exactly this: the timer is made
 * once when the connection is accepted and then moved on every read for the life
 * of the connection. So this is the per read cost of having deadlines at all,
 * and it is the number to quote.
 *
 * The stop does not take the timer out of the heap, it marks it, and the reset
 * finds it still sitting there and takes the mark off. So a timer that is armed
 * and cancelled a million times touches the heap once and the rest is two lock
 * acquisitions and a pair of published minimums.
 *
 * It should come out under the row above it, and the difference between the two
 * is the allocation and nothing else. The row below is the one that settles
 * whether the design works, since it is this same work with a real heap under
 * it, and this row is only interesting next to that one. */

static void reset_stop_body(void *env) {
    (void)env;
    Bench *b = cur;
    Alloc *a = arena_allocator(&timer_arena);

    TimeTimer *t = time_after_func(a, NEVER, BURROW_FN(Func, on_fire, NULL));
    if (t == NULL)
        return;
    (void)time_timer_stop(t);

    bench_resume(b);

    BENCH_LOOP(b) {
        (void)time_timer_reset(t, NEVER, NULL);
        (void)time_timer_stop(t);
    }

    bench_pause(b);

    time_timer_free(t);
}

BENCH(timer_reset_stop) {
    run_with_one_p(b, reset_stop_body);
}

/* -------------------------------------------------- the same on a busy P
 *
 * The row above with a thousand other timers already in the heap, which is what
 * a server holding a thousand connections looks like.
 *
 * The point of the row is the shape rather than the number. A four way heap of a
 * thousand entries is five levels deep against one level for an empty one, so if
 * the cost of a deadline grew with the number of connections it would show here
 * as a multiple. It should not show at all, because the reset finds the timer
 * still in the heap and the only thing that moves it is a sift, and a sift on a
 * timer whose new deadline is an hour away and whose neighbours are all an hour
 * away is a comparison and no swap.
 *
 * A thousand and not ten thousand because the arena has to hold them and because
 * one more level of heap is not a different answer. */

#define DEEP 1000

static TimeTimer *deep[DEEP];

static void reset_stop_deep_body(void *env) {
    (void)env;
    Bench *b = cur;
    Alloc *a = arena_allocator(&timer_arena);

    for (int i = 0; i < DEEP; i++) {
        deep[i] = time_after_func(a, NEVER, BURROW_FN(Func, on_fire, NULL));
        if (deep[i] == NULL)
            return;
    }

    TimeTimer *t = time_after_func(a, NEVER, BURROW_FN(Func, on_fire, NULL));
    if (t == NULL)
        return;
    (void)time_timer_stop(t);

    bench_resume(b);

    BENCH_LOOP(b) {
        (void)time_timer_reset(t, NEVER, NULL);
        (void)time_timer_stop(t);
    }

    bench_pause(b);

    time_timer_free(t);
    for (int i = 0; i < DEEP; i++)
        time_timer_free(deep[i]);
}

BENCH(timer_reset_stop_deep) {
    run_with_one_p(b, reset_stop_deep_body);
}

/* ---------------------------------------------------------------- sleeping
 *
 * A one millisecond sleep, and what comes out is not one millisecond.
 *
 * Read this row as latency rather than as throughput. The interesting quantity
 * is the difference between the number and 1000000 nanoseconds, because that
 * difference is everything between the timer coming due and the goroutine
 * running again: a thread noticing, the goroutine going on a run queue, and a
 * thread picking it up. Most of it is the platform's timer granularity rather
 * than anything here, which is why the row to read it against is Go's, on the
 * same machine, rather than against 1000000 on its own.
 *
 * It is also the only row in this file that can be made worse by the scheduler
 * rather than by the timers, and worse than that by the platform. A parked
 * thread waits on a condition variable here, and macOS gives a condition
 * variable timeout a leeway of half the interval so that it can coalesce the
 * wakeup with something else, so a one millisecond wait on an otherwise idle
 * machine comes back at one and a half. That is the platform and not this code,
 * and the way out of it is the netpoller, which is what Go blocks in instead.
 * The results README has the measurement.
 *
 * One millisecond and not one microsecond because a microsecond is under the
 * granularity of every platform timer burrow sits on, so that row would measure
 * the floor and call it a sleep. */

static void sleep_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        time_sleep(TIME_MILLISECOND);
    }

    bench_pause(b);
}

BENCH(timer_sleep_1ms) {
    run_with_one_p(b, sleep_body);
}

/* ------------------------------------------------------------------- firing
 *
 * A thousand timers all due at once, and the wait until every callback has run.
 * Divide by a thousand for one.
 *
 * Every row above is a timer that never fires, which is the common case and not
 * the whole case. This is the other half: the heap actually pops, and each pop
 * starts a goroutine, because AfterFunc runs its callback on one. So this row is
 * a heap removal plus a goroutine launch plus the scheduler getting round to it,
 * a thousand times over, and the launch is most of it. Divided by a thousand it
 * should land near the goroutine launch rows in sched_bench.c, and if it lands
 * well above them then firing a timer costs something beyond the launch and it
 * is worth finding out what.
 *
 * All due at once rather than spread out, because spreading them out measures
 * the sleep in between and there is a row for that already. This one is the
 * burst, which is what a thousand connections timing out during an outage looks
 * like, and the question it answers is whether the runtime gets through them or
 * falls over. */

#define BURST 1000

static TimeTimer *burst[BURST];

static void burst_body(void *env) {
    (void)env;
    Bench *b = cur;
    Alloc *a = arena_allocator(&timer_arena);

    for (int i = 0; i < BURST; i++) {
        burst[i] = time_after_func(a, NEVER, BURROW_FN(Func, on_fire, NULL));
        if (burst[i] == NULL)
            return;
        (void)time_timer_stop(burst[i]);
    }

    BENCH_LOOP(b) {
        burrow__atomic_store_u32(&fired, 0);

        bench_resume(b);

        for (int i = 0; i < BURST; i++)
            (void)time_timer_reset(burst[i], 0, NULL);

        /* Yielding rather than parking, for the reason the batch row in
         * sched_bench.c gives: parking needs something to wake it and a wait
         * group is two milestones away. The Go side spins on the same counter
         * the same way, so the two are measuring the same program. */
        while (burrow__atomic_load_acquire_u32(&fired) < BURST)
            runtime_gosched();

        bench_pause(b);
    }

    for (int i = 0; i < BURST; i++)
        time_timer_free(burst[i]);
}

BENCH(timer_fire_burst) {
    run_with_one_p(b, burst_body);
    bench_keep_u64(fired);
}

void register_timer_benchmarks(void);

void register_timer_benchmarks(void) {
    BENCH_RUN(timer_arm_stop);
    BENCH_RUN(timer_reset_stop);
    BENCH_RUN(timer_reset_stop_deep);
    BENCH_RUN(timer_sleep_1ms);
    BENCH_RUN(timer_fire_burst);
}
