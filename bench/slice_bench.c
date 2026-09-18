/* Slice against Go's slice, and against the raw C loop underneath it.
 *
 * append is the hottest thing in this library. Almost every port that builds a
 * result builds it by appending, so a cost here is a cost everywhere, and it
 * should have a number attached before anything gets built on top of it.
 *
 * The arena benchmarks reset once per iteration rather than once per run. Each
 * of these iterations builds a whole slice and throws it away, which is what a
 * request handler does, and a request handler pays for its allocations and for
 * the one reset at the end. Letting the arena grow across a hundred thousand
 * iterations would measure the machine running out of memory instead.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/slice.h"
#include "burrow/type.h"

#include <stdlib.h>
#include <string.h>

/* A thousand elements is a page and a half of ints. Small enough that it stays
 * near cache, large enough that the growth happens eleven times and the
 * reallocation is a real part of the cost rather than a rounding error. */
#define N 1024

/* Sixty four at a time, which is what appending a decoded record or a line of
 * fields looks like. */
#define CHUNK 64

static const char medium_c[] =
    "the quick brown fox jumps over the lazy dog, and then does it again";

static Int chunk[CHUNK];

static void fill_chunk(void) {
    for (Int i = 0; i < CHUNK; i++)
        chunk[i] = i;
}

static void report_arena(Bench *b, Arena *ar) {
    AllocStats st = mem_stats(arena_allocator(ar));
    bench_report_allocs(b, st.bytes_total, st.allocs);
}

/* ------------------------------------------------------------------ append */

/* The one that matters. Build a slice of a thousand ints from nothing, one
 * element at a time, letting append work out the capacity. This is the shape of
 * every parse loop in the library. */
BENCH(slice_append_grow) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        Slice s = slice_nil(TYPE_INT);
        for (Int i = 0; i < N; i++)
            s = BURROW_APPEND(Int, a, s, i);
        bench_keep(s.p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* The same thing with the capacity known up front, which is what you get after
 * somebody has profiled the loop above and added a hint. The difference between
 * the two numbers is what the growth actually costs. */
BENCH(slice_append_prealloc) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        Slice s = slice_make(a, TYPE_INT, 0, N);
        for (Int i = 0; i < N; i++)
            s = BURROW_APPEND(Int, a, s, i);
        bench_keep(s.p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* Sixteen bulk appends of sixty four instead of a thousand and twenty four
 * single ones. Same elements, same growth, one sixteenth of the calls, so this
 * is how much of the number above is the call itself. */
BENCH(slice_append_bulk) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    fill_chunk();

    BENCH_LOOP(b) {
        Slice s = slice_nil(TYPE_INT);
        for (Int i = 0; i < N / CHUNK; i++)
            s = slice_append(a, s, chunk, CHUNK);
        bench_keep(s.p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* The same loop again, calling slice_append rather than going through
 * BURROW_APPEND.
 *
 * BURROW_APPEND expands to an inline fast path that knows the element size at
 * the call site, so slice_append_prealloc no longer crosses a call boundary
 * once per element and no longer copies through memcpy with a length nothing
 * can see. This one still does both. The gap between the two is what the
 * inlining buys, and keeping it here means the gap stays measured rather than
 * remembered. */
BENCH(slice_append_call) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        Slice s = slice_make(a, TYPE_INT, 0, N);
        for (Int i = 0; i < N; i++)
            s = slice_append(a, s, &i, 1);
        bench_keep(s.p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* The hand written version of slice_append_prealloc, with no descriptor, no
 * function call and no capacity check. This is the floor. Anything append costs
 * above this is what the generality is charging. */
BENCH(raw_append_prealloc) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        Int *p = (Int *)mem_alloc_nozero(a, N * sizeof(Int), _Alignof(Int));
        Int len = 0;
        if (p != NULL) {
            for (Int i = 0; i < N; i++)
                p[len++] = i;
        }
        bench_keep(p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* -------------------------------------------------------------------- make */

BENCH(slice_make) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        Slice s = slice_make(a, TYPE_INT, N, N);
        bench_keep(s.p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

BENCH(calloc_1k) {
    BENCH_LOOP(b) {
        void *p = calloc(N, sizeof(Int));
        bench_keep(p);
        free(p);
    }
}

/* ------------------------------------------------------------------- index */

/* Sixty four bounds checked reads through BURROW_AT, which inlines the check
 * and the stride because it knows the element type at the call site. Go's
 * compiler inlines its equivalent and can often prove the check away entirely,
 * which burrow cannot, so a gap is expected here. */
BENCH(slice_index) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Slice s = slice_make(a, TYPE_INT, N, N);
    for (Int i = 0; i < N; i++)
        BURROW_AT(Int, s, i) = i;

    BENCH_LOOP(b) {
        uint64_t sum = 0;
        for (Int i = 0; i < CHUNK; i++)
            sum += (uint64_t)BURROW_AT(Int, s, i);
        bench_keep_u64(sum);
    }

    arena_free(&ar);
}

/* The same sixty four reads through slice_at, which is a real call into the
 * library taking the stride from the descriptor. The pair with slice_index is
 * what the inline fast path is worth on the read side. */
BENCH(slice_index_call) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Slice s = slice_make(a, TYPE_INT, N, N);
    for (Int i = 0; i < N; i++)
        BURROW_AT(Int, s, i) = i;

    BENCH_LOOP(b) {
        uint64_t sum = 0;
        for (Int i = 0; i < CHUNK; i++)
            sum += (uint64_t)*(Int *)slice_at(s, i);
        bench_keep_u64(sum);
    }

    arena_free(&ar);
}

BENCH(raw_index_int) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Int *p = (Int *)mem_alloc(a, N * sizeof(Int), _Alignof(Int));
    for (Int i = 0; i < N; i++)
        p[i] = i;

    BENCH_LOOP(b) {
        uint64_t sum = 0;
        for (Int i = 0; i < CHUNK; i++)
            sum += (uint64_t)p[i];
        bench_keep_u64(sum);
    }

    arena_free(&ar);
}

/* ------------------------------------------------------------------- slice */

/* Reslicing should be four words of arithmetic and nothing else. If this is not
 * within noise of free, something is copying that should not be. */
BENCH(slice_sub) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Slice s = slice_make(a, TYPE_INT, N, N);

    BENCH_LOOP(b) {
        Slice m = slice_sub(s, 1, N - 1);
        bench_keep(m.p);
        bench_keep_u64((uint64_t)m.len);
    }

    arena_free(&ar);
}

/* -------------------------------------------------------------------- copy */

BENCH(slice_copy) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Slice src = slice_make(a, TYPE_INT, N, N);
    Slice dst = slice_make(a, TYPE_INT, N, N);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)slice_copy(dst, src));
    }

    arena_free(&ar);
}

BENCH(memcpy_same_size) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    void *src = mem_alloc(a, N * sizeof(Int), _Alignof(Int));
    void *dst = mem_alloc(a, N * sizeof(Int), _Alignof(Int));

    BENCH_LOOP(b) {
        memcpy(dst, src, N * sizeof(Int));
        bench_keep(dst);
    }

    arena_free(&ar);
}

/* ------------------------------------------------------------- conversions */

BENCH(slice_from_str_medium) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = str_from_cstr(medium_c);

    BENCH_LOOP(b) {
        Slice by = slice_from_str(a, s);
        bench_keep(by.p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

void register_slice_benchmarks(void);

void register_slice_benchmarks(void) {
    BENCH_RUN(slice_append_grow);
    BENCH_RUN(slice_append_prealloc);
    BENCH_RUN(slice_append_bulk);
    BENCH_RUN(slice_append_call);
    BENCH_RUN(raw_append_prealloc);
    BENCH_RUN(slice_make);
    BENCH_RUN(calloc_1k);
    BENCH_RUN(slice_index);
    BENCH_RUN(slice_index_call);
    BENCH_RUN(raw_index_int);
    BENCH_RUN(slice_sub);
    BENCH_RUN(slice_copy);
    BENCH_RUN(memcpy_same_size);
    BENCH_RUN(slice_from_str_medium);
}
