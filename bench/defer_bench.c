/* defer, which is a scope, a list of calls, and the calls.
 *
 * The row to read first is defer_one against defer_direct. defer_direct writes
 * the cleanup at the bottom of the block by hand, which is what the code would
 * say without this feature, so the gap between the two is the whole price of
 * writing it as a defer. Everything else here is that question asked with more
 * calls in the scope.
 *
 * defer_four fills the four calls a scope holds in the caller's frame and
 * defer_eight goes past them, so the gap between those two rows is the one
 * allocation a scope makes when it runs out of room, spread over eight calls.
 *
 * One asymmetry to know about before reading the ratios, and it is not a
 * measurement error. A Go defer of a named function is open-coded into the
 * frame by the compiler, which knows at compile time which function runs and
 * calls it directly. A burrow defer is a function value, because everything in
 * burrow that takes a callback is, so the call is indirect and the compiler
 * cannot see through it. That is the design and not an accident, and these rows
 * are what it costs against a language whose compiler is in on the deal.
 *
 * The deferred function increments a counter that the benchmark reads
 * afterwards, on both sides, since a cleanup whose effect nobody looks at is
 * one either compiler is allowed to delete.
 *
 * No runtime here. A scope on a thread that is not a goroutine uses a thread
 * local chain, which is one load rather than the goroutine lookup, and the
 * numbers are a nanosecond apart either way. Starting a scheduler to measure a
 * frame's worth of pointer writes would only add noise.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/defer.h"
#include "burrow/func.h"

#include <stdint.h>

/* What every deferred call does. A file static rather than something through
 * the environment pointer, so that the Go side can say the same thing with a
 * package level variable and neither row is paying for an extra indirection the
 * other one is not. */
static uint64_t ticks;

static void tick(void *env) {
    (void)env;
    ticks++;
}

/* Puts a function value somewhere the optimiser cannot follow and hands it
 * back. The address of a slot goes through bench_hide rather than the function
 * pointer itself, because C does not promise that a function pointer survives a
 * trip through a void *, which is the same dance bench/func_bench.c does. */
static Func hide_func(Func f) {
    static Func slot;

    slot = f;
    return *(const Func *)bench_hide(&slot);
}

/* One scope, one deferred call, per iteration. */
BENCH(defer_one) {
    BENCH_LOOP(b) {
        BURROW_SCOPE {
            BURROW_DEFER(tick, NULL);
        }
        BURROW_SCOPE_END;
    }

    bench_keep_u64(ticks);
}

/* The same work with the call written out at the bottom of the block, which is
 * what somebody writes when there is no defer. The gap is the feature.
 *
 * The function value goes through bench_hide first, so this is the same
 * indirect call the deferred row makes rather than an inlined increment the
 * compiler can fold into an addition. Without it this row reports zero, which
 * is the compiler telling you it worked out the answer without running the
 * loop. */
BENCH(defer_direct) {
    Func f = hide_func(BURROW_FN(Func, tick, NULL));

    BENCH_LOOP(b) {
        BURROW_CALLF0(f);
    }

    bench_keep_u64(ticks);
}

/* Four is what a scope holds without asking anybody for memory. */
BENCH(defer_four) {
    BENCH_LOOP(b) {
        BURROW_SCOPE {
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
        }
        BURROW_SCOPE_END;
    }

    bench_keep_u64(ticks);
}

/* Eight is four in the frame and four in one allocation, which is freed before
 * the scope returns. Against defer_four this is what running out of room costs,
 * and against Go it is a row where Go is doing the same thing: a function with
 * more defers in it than the compiler will open-code puts them on the heap. */
BENCH(defer_eight) {
    BENCH_LOOP(b) {
        BURROW_SCOPE {
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
            BURROW_DEFER(tick, NULL);
        }
        BURROW_SCOPE_END;
    }

    bench_keep_u64(ticks);
}

/* An empty scope, which is the cost of the block itself with nothing in it.
 * Worth a row because a function that opens a scope and then takes an early
 * error return pays exactly this and nothing else. Go has no counterpart: a Go
 * function with no defer in it does not have a defer. */
BENCH(defer_scope_empty) {
    BENCH_LOOP(b) {
        BURROW_SCOPE {
            bench_keep_u64((uint64_t)bench_i_);
        }
        BURROW_SCOPE_END;
    }
}

/* A scope inside a loop, which is the shape the header asks for and the one a
 * file opened per item has. Ten turns, one call each, so the scope is opened
 * and closed ten times per iteration and nothing accumulates. Go's counterpart
 * is the same loop with the defer in it, which does accumulate, because a Go
 * defer in a loop body does not run until the function returns. That is the
 * difference the two languages have here and it is the point of the row. */
BENCH(defer_loop_ten) {
    BENCH_LOOP(b) {
        for (int i = 0; i < 10; i++) {
            BURROW_SCOPE {
                BURROW_DEFER(tick, NULL);
            }
            BURROW_SCOPE_END;
        }
    }

    bench_keep_u64(ticks);
}

void register_defer_benchmarks(void);

void register_defer_benchmarks(void) {
    BENCH_RUN(defer_one);
    BENCH_RUN(defer_direct);
    BENCH_RUN(defer_four);
    BENCH_RUN(defer_eight);
    BENCH_RUN(defer_scope_empty);
    BENCH_RUN(defer_loop_ten);
}
