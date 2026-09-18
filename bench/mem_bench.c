/* The allocators.
 *
 * Every arena benchmark here resets every RESET_EVERY iterations, with the
 * clock still running. That is deliberate and it is the only honest way to do
 * it. An arena that never resets grows until the machine dies, so something has
 * to give the memory back, and hiding that something behind bench_pause would
 * report a bump pointer cost that no real program ever pays on its own. A
 * request handler pays for its allocations and for the one reset at the end,
 * so that is what gets measured.
 *
 * The malloc side frees each block immediately for the same reason. Both sides
 * get the memory and both sides give it back.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/mem/track.h"

#include <stdlib.h>
#include <string.h>

/* Roughly how many allocations a small request makes before it is done. Low
 * enough that the arena stays in cache, high enough that the reset is properly
 * amortised rather than being the whole measurement. */
#define RESET_EVERY 1024

/* 64 bytes is the size that matters. It is a cache line, it is about what a
 * small struct or a short string copy costs, and it is the size the allocator
 * sees far more often than any other. */
#define SMALL 64

/* Big enough to leave the bump pointer path and take the large allocation path
 * instead, which is a different branch and worth its own number. */
#define LARGE (1 << 20)

static void report_arena(Bench *b, Arena *ar) {
    AllocStats st = mem_stats(arena_allocator(ar));
    bench_report_allocs(b, st.bytes_total, st.allocs);
}

/* ------------------------------------------------------------------- arena */

BENCH(arena_alloc) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        void *p = mem_alloc_nozero(a, SMALL, 8);
        bench_keep(p);
        if ((bench_i_ & (RESET_EVERY - 1)) == 0)
            arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

BENCH(arena_alloc_zeroed) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        void *p = mem_alloc(a, SMALL, 8);
        bench_keep(p);
        if ((bench_i_ & (RESET_EVERY - 1)) == 0)
            arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

BENCH(arena_alloc_large) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        void *p = mem_alloc_nozero(a, LARGE, 8);
        bench_keep(p);
        /* One megabyte at a time fills a chunk immediately, so this resets
         * every iteration. What is being measured is the large path plus the
         * chunk coming straight back off the spare list. */
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* The reset on its own, after the arena has already been through a round and
 * has chunks to keep. This should not touch the memory it is keeping, so the
 * number should be flat no matter how much was allocated. */
BENCH(arena_reset) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        bench_pause(b);
        for (int i = 0; i < RESET_EVERY; i++)
            bench_keep(mem_alloc_nozero(a, SMALL, 8));
        bench_resume(b);

        arena_reset(&ar);
    }

    arena_free(&ar);
}

/* Growing the most recent allocation, which the arena is meant to do in place
 * by unwinding the bump pointer rather than copying. If this is anywhere near
 * realloc's number, the unwind is not happening. */
BENCH(arena_append_grow) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        size_t n = 16;
        void *p = mem_alloc_nozero(a, n, 8);
        for (int i = 0; i < 8; i++) {
            size_t next = n * 2;
            p = mem_realloc(a, p, n, next, 8);
            n = next;
        }
        bench_keep(p);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* ------------------------------------------------------------------- heap */

/* The same work through burrow's heap backend, which is malloc with the
 * allocator interface in front of it. The gap between this and malloc_free
 * below is what the indirection costs, and it should be close to nothing. */
BENCH(heap_alloc) {
    Alloc *a = heap_allocator();

    BENCH_LOOP(b) {
        void *p = mem_alloc_nozero(a, SMALL, 8);
        bench_keep(p);
        mem_free(a, p, SMALL, 8);
    }
}

BENCH(heap_alloc_zeroed) {
    Alloc *a = heap_allocator();

    BENCH_LOOP(b) {
        void *p = mem_alloc(a, SMALL, 8);
        bench_keep(p);
        mem_free(a, p, SMALL, 8);
    }
}

/* ------------------------------------------------------------------- track
 *
 * What it costs to run under the checking allocator, which is the question
 * anybody asks before turning it on for a whole test suite. Each row here has
 * the bare allocator directly above it in the table, so the overhead is a
 * division rather than a claim.
 *
 * The first two turn the quarantine off, so they measure the bookkeeping on its
 * own: a hash of the pointer, a probe, a record allocated from the heap, and the
 * same three again on the way back. The third leaves the quarantine at its
 * default, which is what a test run actually uses, and the difference between it
 * and the second is what the poison memset and the deferred free cost.
 *
 * None of these have a Go row next to them, and they are not meant to. Go has
 * no equivalent because Go has no equivalent problem. */

BENCH(track_arena_alloc) {
    Arena ar;
    Track tr;
    arena_init(&ar, NULL, 0);
    track_init(&tr, arena_allocator(&ar));
    track_set_quarantine(&tr, 0);
    Alloc *a = track_allocator(&tr);

    BENCH_LOOP(b) {
        void *p = mem_alloc_nozero(a, SMALL, 8);
        bench_keep(p);
        /* Through the wrapper rather than through the arena, since a reset is
         * what tells the wrapper those blocks were given back and not leaked,
         * and the cost of clearing the records belongs in this number. */
        if ((bench_i_ & (RESET_EVERY - 1)) == 0)
            mem_reset(a);
    }

    track_free(&tr);
    arena_free(&ar);
}

BENCH(track_heap_alloc) {
    Track tr;
    track_init(&tr, heap_allocator());
    track_set_quarantine(&tr, 0);
    Alloc *a = track_allocator(&tr);

    BENCH_LOOP(b) {
        void *p = mem_alloc_nozero(a, SMALL, 8);
        bench_keep(p);
        mem_free(a, p, SMALL, 8);
    }

    track_free(&tr);
}

BENCH(track_heap_quarantine) {
    Track tr;
    track_init(&tr, heap_allocator());
    Alloc *a = track_allocator(&tr);

    BENCH_LOOP(b) {
        void *p = mem_alloc_nozero(a, SMALL, 8);
        bench_keep(p);
        mem_free(a, p, SMALL, 8);
    }

    track_free(&tr);
}

/* ---------------------------------------------------------------- baseline */

BENCH(malloc_free) {
    BENCH_LOOP(b) {
        void *p = malloc(SMALL);
        bench_keep(p);
        free(p);
    }
}

BENCH(calloc_free) {
    BENCH_LOOP(b) {
        void *p = calloc(1, SMALL);
        bench_keep(p);
        free(p);
    }
}

BENCH(realloc_grow) {
    BENCH_LOOP(b) {
        size_t n = 16;
        void *p = malloc(n);
        for (int i = 0; i < 8; i++) {
            n *= 2;
            p = realloc(p, n);
        }
        bench_keep(p);
        free(p);
    }
}

void register_mem_benchmarks(void);

void register_mem_benchmarks(void) {
    BENCH_RUN(arena_alloc);
    BENCH_RUN(arena_alloc_zeroed);
    BENCH_RUN(arena_alloc_large);
    BENCH_RUN(arena_reset);
    BENCH_RUN(arena_append_grow);
    BENCH_RUN(heap_alloc);
    BENCH_RUN(heap_alloc_zeroed);
    BENCH_RUN(track_arena_alloc);
    BENCH_RUN(track_heap_alloc);
    BENCH_RUN(track_heap_quarantine);
    BENCH_RUN(malloc_free);
    BENCH_RUN(calloc_free);
    BENCH_RUN(realloc_grow);
}
