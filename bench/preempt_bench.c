/* What a safe point costs.
 *
 * burrow preempts a goroutine by setting a flag on it and waiting for it to
 * pass somewhere it is safe to stop. Channel operations and `go` are such
 * places, and so is runtime_preempt_point, which is the one a compute loop with
 * none of those in it calls by hand.
 *
 * The whole design rests on that call being cheap enough to sit inside a loop,
 * so this file is two rows and a comparison. The comparison is the point: read
 * `preempt_point_idle` against `goroutine_yield_alone` in bench/sched_bench.c,
 * which is runtime_gosched finding an empty run queue and coming straight back.
 * Both of them do nothing, and the distance between them is the reason the
 * cheap one exists. Gosched does nothing by asking the scheduler, which is a
 * stack switch there and another one back. This does nothing by reading a word.
 *
 * There is no Go column, and there cannot be one. Go's compiler writes the safe
 * points into the loop itself, so the cost is spread across the generated code
 * and there is nothing to call and nothing to time. That is also why neither
 * row is in tools/pairs.txt.
 *
 * Neither row measures a preemption. A preemption happens at most once every
 * ten milliseconds, so a benchmark that took one would be measuring how often
 * sysmon runs rather than what the call costs, and every iteration that did not
 * take one would be measuring this anyway. What is being timed here is the
 * ninety nine point nine per cent case, which is the answer no.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/func.h"
#include "burrow/proc.h"

#include <stddef.h>

/* The Bench the goroutine body is running for. A file static for the reason
 * bench/sched_bench.c gives: only one runtime_main runs at a time, so a
 * benchmark that reached the wrong one would fail loudly rather than quietly. */
static Bench *cur;

static void run_with_one_p(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    cur = b;

    int old = runtime_gomaxprocs(1);
    runtime_main(BURROW_FN(Func, body, NULL));
    (void)runtime_gomaxprocs(old);
}

/* --------------------------------------------------------- on a goroutine
 *
 * The real one. A goroutine nobody has asked to stop, calling the safe point
 * over and over, which is what the inside of a preemptible compute loop looks
 * like on every iteration but the rare one.
 *
 * What is being timed is a load of the M out of thread local storage, a load of
 * the current goroutine out of the M, a relaxed load of the flag and a branch.
 * Nothing is written and no lock is taken, so this number should not move when
 * other goroutines are busy, and it is the number quoted in the preemption
 * section of docs/design/06-runtime.md in the burrow tree. */

static void idle_body(void *env) {
    (void)env;
    Bench *b = cur;

    bench_resume(b);

    BENCH_LOOP(b) {
        runtime_preempt_point();
    }

    bench_pause(b);
}

BENCH(preempt_point_idle) {
    run_with_one_p(b, idle_body);
}

/* ------------------------------------------------------- off a goroutine
 *
 * The same call on a thread that is not running a goroutine at all, where it
 * answers immediately because there is no M to ask.
 *
 * This is a row rather than a footnote because it is a path real code takes. A
 * function that is sometimes called from a goroutine and sometimes from the
 * program's own thread is supposed to be able to put a safe point in its loop
 * without first working out which it is today, and that is only true if the
 * wrong answer is free.
 *
 * It stops one load earlier than the row above, so the expectation was that it
 * would come out cheaper. It does not. The two rows land on top of each other,
 * and the reason is that neither is measuring the loads: both pay a lookup in
 * thread local storage to find the M, and next to that the two loads that
 * follow it are hits in the nearest cache. Read them as one number rather than
 * as a pair to compare.
 *
 * No runtime here on purpose. Starting one would give the calling thread an M
 * and turn this into the row above. */

BENCH(preempt_point_off_goroutine) {
    BENCH_LOOP(b) {
        runtime_preempt_point();
    }
}

void register_preempt_benchmarks(void);

void register_preempt_benchmarks(void) {
    BENCH_RUN(preempt_point_idle);
    BENCH_RUN(preempt_point_off_goroutine);
}
