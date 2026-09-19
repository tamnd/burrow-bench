/* What it costs to ask what time it is, and what a timed sleep costs when it
 * does not sleep.
 *
 * These two rows matter more than they look. Every timer, every timeout and
 * every deadline in the runtime starts with a call to burrow__nanotime, and the
 * scheduler is about to grow a loop that reads it on the way round. A clock that
 * costs twenty nanoseconds instead of two is a tax on all of that, and the
 * difference between those two numbers is entirely whether the platform reads
 * its clock through the vdso or goes into the kernel.
 *
 * The Go pair for the clock is time.Since, which is one runtime.nanotime call
 * and a subtraction. That is as close as Go lets an outsider get: nanotime is
 * not exported, and time.Now reads the wall clock and the monotonic clock
 * together and is therefore a different amount of work. So the paired row here
 * is written to be the same shape as time.Since rather than the shape a caller
 * would write, and the bare reading sits next to it unpaired.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/clock.h"
#include "burrow/note.h"

#include <stdint.h>

/* ------------------------------------------------------------------- the clock
 *
 * One reading per iteration. On Linux and macOS this never enters the kernel,
 * because both map a page the kernel keeps up to date and read the counter out
 * of it, and on Windows QueryPerformanceCounter does the same thing. So what is
 * being measured is a handful of instructions plus, on macOS and Windows, the
 * multiply and divide that turns ticks into nanoseconds. */

BENCH(nanotime) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)burrow__nanotime());
    }
}

/* One reading subtracted from a start taken before the loop, which is exactly
 * what time.Since is and is why this is the row with the Go pair on it. Both
 * sides read the clock once and subtract once. The gap between this row and the
 * one above is the subtraction, and it should be nothing. */

BENCH(nanotime_elapsed) {
    int64_t start = burrow__nanotime();

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(burrow__nanotime() - start));
    }
}

/* Two readings and the subtraction between them, which is what a caller timing
 * a piece of work actually pays. No Go pair, because the Go program that does
 * this is two time.Now calls and time.Now reads the wall clock as well, so the
 * pairing would be a claim that is not true. It is here because it is the shape
 * the runtime uses and it should come to twice the row above. */

BENCH(nanotime_interval) {
    BENCH_LOOP(b) {
        int64_t start = burrow__nanotime();
        bench_keep_u64((uint64_t)(burrow__nanotime() - start));
    }
}

/* ------------------------------------------------------------- the timed sleep
 *
 * The timed sleep on a gate that is already open, which is the path the
 * scheduler will take almost every time it uses one. A thread about to park
 * checks for work, finds none, and goes to sleep with a deadline, and by then
 * the wake it was racing has usually already landed. When it has, this call has
 * to notice and come straight back without going near the kernel.
 *
 * There is no Go pair. The nearest thing is a select with a time.After in it,
 * which allocates a timer and a channel and is a different measurement, and the
 * WaitGroup row in sync_bench.c already covers the untimed version. This row
 * exists to be watched rather than compared: it should stay level with
 * note_cycle, and if it ever drifts above it then the timeout path has picked
 * up a clock reading or a system call it does not need. */

static burrow__Note clock_note;

BENCH(note_timeout_hit) {
    if (!burrow__note_init(&clock_note))
        return;

    BENCH_LOOP(b) {
        burrow__note_clear(&clock_note);
        burrow__note_wake(&clock_note);
        bench_keep_u64(burrow__note_sleep_timeout(&clock_note, 1000000000) ? 1u : 0u);
    }

    burrow__note_free(&clock_note);
}

/* And the poll, which is the same call with no time in it at all. A caller that
 * wants to look without waiting uses this rather than a second function, so it
 * is worth knowing that it costs what a look costs. */

BENCH(note_timeout_poll) {
    if (!burrow__note_init(&clock_note))
        return;

    BENCH_LOOP(b) {
        bench_keep_u64(burrow__note_sleep_timeout(&clock_note, 0) ? 1u : 0u);
    }

    burrow__note_free(&clock_note);
}

void register_clock_benchmarks(void);

void register_clock_benchmarks(void) {
    BENCH_RUN(nanotime);
    BENCH_RUN(nanotime_elapsed);
    BENCH_RUN(nanotime_interval);
    BENCH_RUN(note_timeout_hit);
    BENCH_RUN(note_timeout_poll);
}
