/* panic and recover, which is a setjmp, a walk of the open scopes, and a jump.
 *
 * The row to read first is panic_try_empty. That is a block that never panics,
 * which is what a server's request handler looks like on every request that
 * goes fine, and it is the number that decides whether wrapping something in a
 * catch block is affordable. Nothing in it walks anything: it is the setjmp and
 * four stores.
 *
 * panic_caught is the same block with a panic in it, so the gap between the two
 * is the cost of actually going wrong. panic_caught_scopes asks the same
 * question with three scopes and three deferred calls in the way, which is the
 * shape a real panic has, since the reason to unwind rather than exit is that
 * there are files to close on the way.
 *
 * Go's counterpart is a deferred closure that calls recover, which is the only
 * way Go spells this. That is not quite the same shape and cannot be: Go pays
 * its cost in the defer and burrow pays it in the block, which is the whole
 * trade described in burrow's docs/guides/panic.md. The rows are each
 * language's idiom for the same job rather than two spellings of one
 * implementation, so read them as a comparison of features and not of
 * instruction counts.
 *
 * No runtime here, the same as the defer benchmarks and for the same reason. A
 * block on a thread that is not a goroutine keeps its state in a thread local
 * instead of on the G, which is one load either way.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/defer.h"
#include "burrow/panic.h"

#include <stdint.h>

/* gcc's -Wclobbered fires on locals held across the setjmp inside BURROW_TRY
 * and is wrong about these, for the reason burrow's tests/fatal.h gives at more
 * length: nothing here is written between the setjmp and the jump back. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

/* What the deferred calls in the scope rows do, and what the catch blocks count
 * so that neither compiler can decide the block had no effect. */
static uint64_t ticks;

static void tick(void *env) {
    (void)env;
    ticks++;
}

/* A block that does not panic, which is every request that goes fine. */
BENCH(panic_try_empty) {
    BENCH_LOOP(b) {
        BURROW_TRY {
            bench_keep_u64((uint64_t)bench_i_);
        }
        BURROW_CATCH(p) {
            (void)p;
            ticks++;
        }
        BURROW_TRY_END;
    }

    bench_keep_u64(ticks);
}

/* The same block with a panic in it and nothing in the way, so this row against
 * the one above is what going wrong costs when there is no cleanup to do. */
BENCH(panic_caught) {
    BENCH_LOOP(b) {
        BURROW_TRY {
            panic_str(BURROW_S("bench"));
        }
        BURROW_CATCH(p) {
            (void)p;
            ticks++;
        }
        BURROW_TRY_END;
    }

    bench_keep_u64(ticks);
}

/* Three frames deep with a scope and a deferred call in each, which is what a
 * panic in a program that opens things looks like. The innermost one panics and
 * the three deferred calls run on the way out. */
static void level_three(void) {
    BURROW_SCOPE {
        BURROW_DEFER(tick, NULL);
        panic_str(BURROW_S("bench"));
    }
    BURROW_SCOPE_END;
}

static void level_two(void) {
    BURROW_SCOPE {
        BURROW_DEFER(tick, NULL);
        level_three();
    }
    BURROW_SCOPE_END;
}

static void level_one(void) {
    BURROW_SCOPE {
        BURROW_DEFER(tick, NULL);
        level_two();
    }
    BURROW_SCOPE_END;
}

BENCH(panic_caught_scopes) {
    BENCH_LOOP(b) {
        BURROW_TRY {
            level_one();
        }
        BURROW_CATCH(p) {
            (void)p;
            ticks++;
        }
        BURROW_TRY_END;
    }

    bench_keep_u64(ticks);
}

void register_panic_benchmarks(void);

void register_panic_benchmarks(void) {
    BENCH_RUN(panic_try_empty);
    BENCH_RUN(panic_caught);
    BENCH_RUN(panic_caught_scopes);
}
