/* Error against Go's error.
 *
 * Errors get measured because of where they sit. Every fallible call in the
 * library returns one, which means the success path is on the hot path of
 * everything, and the failure path runs at exactly the moment the machine is
 * least happy. Two different things worth a number each.
 *
 * The comparisons are the ones that happen most. A sentinel check against
 * io.EOF runs once per read in every loop that reads until the end, and it
 * should cost two loads and a branch. That is the claim, so here is the number.
 *
 * Construction is measured against an arena that resets once per iteration.
 * Go's side allocates and hands the garbage to a collector that runs later on
 * another thread, so the time columns are not the same experiment. The
 * allocation counts are, because both sides count the same thing.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/error.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/slice.h"
#include "burrow/type.h"

/* Five deep, which is about what a real chain looks like once a request has
 * been through a handler, a store and a driver. */
#define DEPTH 5

static void report_arena(Bench *b, Arena *ar) {
    AllocStats st = mem_stats(arena_allocator(ar));
    bench_report_allocs(b, st.bytes_total, st.allocs);
}

BURROW_SENTINEL_ERROR(bench_err_closed, "file already closed");
BURROW_SENTINEL_ERROR(bench_err_not_exist, "file does not exist");

/* ------------------------------------------------------------ the wrapper
 *
 * There is no public wrapping constructor yet, because Go's is fmt.Errorf with
 * %w and fmt is not written. So the chains below are built out of this, which
 * is the same shape fmt.Errorf will produce: a message and the error it wraps.
 * Building one is not in any timed loop. */

typedef struct Wrapped {
    Str text;
    Error cause;
} Wrapped;

static Str wrapped_message(const void *self) {
    return ((const Wrapped *)self)->text;
}

static Error wrapped_unwrap(const void *self) {
    return ((const Wrapped *)self)->cause;
}

static const Type wrapped_type = {
    {(const Byte *)"Wrapped", 7},
    {NULL, 0},
    KIND_STRUCT,
    (uint32_t)sizeof(Wrapped),
    (uint16_t)_Alignof(Wrapped),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x77726170u,
    NULL,
};

static const ErrorVT wrapped_vt = {
    &wrapped_type, wrapped_message, wrapped_unwrap, NULL, NULL, NULL,
};

static Wrapped wrap_nodes[DEPTH];

/* A chain DEPTH layers deep with bench_err_closed at the bottom, built once
 * into static storage so that nothing here depends on an allocator. */
static Error build_chain(void) {
    Error e = bench_err_closed;
    int i;
    for (i = 0; i < DEPTH; i++) {
        wrap_nodes[i].text = BURROW_S("layer");
        wrap_nodes[i].cause = e;
        e = (Error){&wrapped_vt, &wrap_nodes[i]};
    }
    return e;
}

/* --------------------------------------------------------------- the checks */

/* The one that runs once per read in every loop that reads to the end. A hit on
 * the first error, so it measures the comparison and nothing else. */
BENCH(error_is_sentinel) {
    Error err = bench_err_closed;

    BENCH_LOOP(b) {
        bench_keep_u64(errors_is(err, bench_err_closed) ? 1u : 0u);
    }
}

/* The miss, which is the more common answer in a loop that is looking for one
 * particular failure among several. Same work, other branch. */
BENCH(error_is_sentinel_miss) {
    Error err = bench_err_closed;

    BENCH_LOOP(b) {
        bench_keep_u64(errors_is(err, bench_err_not_exist) ? 1u : 0u);
    }
}

/* Through five layers of wrapping to the sentinel at the bottom, which is what
 * errors.Is costs once an error has crossed a few package boundaries. */
BENCH(error_is_chain) {
    Error err = build_chain();

    BENCH_LOOP(b) {
        bench_keep_u64(errors_is(err, bench_err_closed) ? 1u : 0u);
    }
}

/* The same depth with nothing to find, so the whole chain gets walked every
 * time. This is the worst case of a question people ask on every request. */
BENCH(error_is_chain_miss) {
    Error err = build_chain();

    BENCH_LOOP(b) {
        bench_keep_u64(errors_is(err, bench_err_not_exist) ? 1u : 0u);
    }
}

/* errors.As down the same chain. It compares type descriptor pointers rather
 * than error values, so the per layer cost is a load and a compare either way,
 * and the interesting part is whether the extra vtable slot changes anything. */
BENCH(error_as_chain) {
    Error err = build_chain();

    BENCH_LOOP(b) {
        bench_keep(errors_as(err, &wrapped_type));
    }
}

/* Reading the message, which in burrow is a load and in Go is a method call
 * that may build a string. Both sides here hold a message that is already
 * built, so this is the vtable dispatch and nothing else. */
BENCH(error_text) {
    Error err = build_chain();

    BENCH_LOOP(b) {
        Str m = error_text(err);
        bench_keep(m.p);
    }
}

/* -------------------------------------------------------------- the success */

/* Returning no error, which is what the overwhelming majority of calls in a
 * working program do.
 *
 * bench_keep is a real call, so the function cannot be inlined away and the
 * two words genuinely go through the return registers. If this is not close to
 * free then every fallible function in the library pays for it. */
static Error succeeds(Int i) {
    if (i < 0)
        return bench_err_closed;
    return BURROW_NO_ERROR;
}

BENCH(error_return_ok) {
    BENCH_LOOP(b) {
        Error err = succeeds(bench_i_);
        bench_keep_u64(BURROW_FAILED(err) ? 1u : 0u);
    }
}

/* ---------------------------------------------------------- the constructors */

/* errors.New, which is the one that allocates. One allocation here rather than
 * Go's two, because the struct and its text go in the same block. */
BENCH(error_new) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str text = BURROW_S("connection reset by peer");

    BENCH_LOOP(b) {
        Error err = errors_new(a, text);
        bench_keep(err.data);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* A sentinel, for the comparison that makes the point. It is the same error
 * from the caller's side and it costs nothing at all to have, because the
 * linker built it. Go's sentinels are package level vars, so Go pays for these
 * at init time and nobody measures it there either. */
BENCH(error_sentinel_use) {
    BENCH_LOOP(b) {
        Error err = bench_err_closed;
        bench_keep(err.vt);
    }
}

/* errors.Join of two, which is what a cleanup path that closes several things
 * produces. Two allocations: the Slice of children and the joined message with
 * the struct in front of it. */
BENCH(error_join_two) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        Error err = errors_join_v(a, 2, bench_err_closed, bench_err_not_exist);
        bench_keep(err.data);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* Searching a tree rather than a chain. The join is built outside the loop, so
 * this is the depth first walk and not the construction. Go's Is does the same
 * recursion over the same shape. */
BENCH(error_is_tree) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Error inner = errors_join_v(a, 2, bench_err_not_exist, bench_err_closed);
    Error err = errors_join_v(a, 2, bench_err_not_exist, inner);

    BENCH_LOOP(b) {
        bench_keep_u64(errors_is(err, bench_err_closed) ? 1u : 0u);
    }

    arena_free(&ar);
}

void register_error_benchmarks(void);

void register_error_benchmarks(void) {
    BENCH_RUN(error_is_sentinel);
    BENCH_RUN(error_is_sentinel_miss);
    BENCH_RUN(error_is_chain);
    BENCH_RUN(error_is_chain_miss);
    BENCH_RUN(error_as_chain);
    BENCH_RUN(error_text);
    BENCH_RUN(error_return_ok);
    BENCH_RUN(error_new);
    BENCH_RUN(error_sentinel_use);
    BENCH_RUN(error_join_two);
    BENCH_RUN(error_is_tree);
}
