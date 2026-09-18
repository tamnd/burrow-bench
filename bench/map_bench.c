/* Map against Go's map.
 *
 * Both sides are Swiss tables now, Go's since 1.24 and burrow's since it was
 * written, so this is a comparison of two implementations of the same idea
 * rather than a comparison of two data structures. That makes the ratios worth
 * reading and it makes the places they diverge worth explaining.
 *
 * The hash is the first divergence and it is the big one. Go's is AES backed
 * where the chip has the instruction, which is most machines this runs on.
 * burrow's is FNV-1a a byte at a time, which is fine for an eight byte int key
 * and is not fine for a string key, so the string lookups here are expected to
 * lose and the size of the loss is the argument for replacing the hash. That is
 * the number this file exists to produce.
 *
 * The second is growth. Go's table is a directory of smaller tables and grows
 * by splitting one of them. burrow's is a single table that doubles and
 * reinserts. Amortised that is the same work, and map_set_grow is where it
 * shows up as one pause instead of many small ones.
 *
 * Construction is measured against an arena that resets once per iteration, so
 * those time columns are two different experiments: burrow hands the memory
 * back at the reset and Go hands it to a collector that runs later on another
 * thread. The allocation counts are the same experiment and are the column to
 * read.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/map.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/type.h"

#include <stdio.h>

/* A thousand entries, which is a table of 128 groups and eleven doublings to
 * get there from nothing. Small enough to stay in L2 so that the measurement
 * is the table and not the memory system, large enough that the probe
 * sequence is doing real work.
 *
 * A power of two so that the index into the key arrays is a mask instead of a
 * division. The Go side masks with the same constant. */
#define N 1024

static Int int_keys[N];   /* the keys that are in the table */
static Int int_misses[N]; /* the same count of keys that are not */

/* String keys need their bytes to outlive the map, so they live here rather
 * than in an arena that gets reset. Sixteen bytes each, which keeps a key and
 * its length word inside one cache line. */
static char key_bytes[N][16];
static char miss_bytes[N][16];
static Str str_keys[N];
static Str str_misses[N];

static void build_keys(void) {
    Int i;
    for (i = 0; i < N; i++) {
        int_keys[i] = i * 7 + 1; /* not 0..N, so the low bits are not the index */
        int_misses[i] = -(i * 7 + 1);

        snprintf(key_bytes[i], sizeof key_bytes[i], "key%08lld", (long long)i);
        snprintf(miss_bytes[i], sizeof miss_bytes[i], "nyk%08lld", (long long)i);
        str_keys[i] = (Str){(const Byte *)key_bytes[i], 11};
        str_misses[i] = (Str){(const Byte *)miss_bytes[i], 11};
    }
}

static void report_arena(Bench *b, Arena *ar) {
    AllocStats st = mem_stats(arena_allocator(ar));
    bench_report_allocs(b, st.bytes_total, st.allocs);
}

/* Prefills a map with the N int keys, value equal to the key. */
static Map *filled_int_map(Alloc *a) {
    Map *m = map_make(a, TYPE_INT, TYPE_INT, N);
    Int i;
    build_keys();
    for (i = 0; i < N; i++)
        map_set(m, &int_keys[i], &int_keys[i]);
    return m;
}

static Map *filled_str_map(Alloc *a) {
    Map *m = map_make(a, TYPE_STRING, TYPE_INT, N);
    Int i;
    build_keys();
    for (i = 0; i < N; i++)
        map_set(m, &str_keys[i], &i);
    return m;
}

/* ------------------------------------------------------------------ lookups */

/* The one that happens most. A key that is there, so the probe stops on the
 * first group nearly every time and this is the hash plus one group match plus
 * one key comparison. */
BENCH(map_get_hit_int) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        bench_keep(map_get(m, &int_keys[bench_i_ & (N - 1)]));
    }

    arena_free(&ar);
}

/* The miss, which is the answer a cache gets on the request that matters. It
 * has to prove absence, so it probes until it finds an empty slot, and at seven
 * eighths full that is more than one group more often than people expect. */
BENCH(map_get_miss_int) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        bench_keep(map_get(m, &int_misses[bench_i_ & (N - 1)]));
    }

    arena_free(&ar);
}

/* The two return form, v, ok := m[k], which copies the value out instead of
 * handing back a pointer into the table. The gap against map_get_hit_int is
 * what the copy costs, and for an eight byte value it should be nothing. */
BENCH(map_get2_hit_int) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));
    Int out = 0;

    BENCH_LOOP(b) {
        bench_keep_u64(map_get2(m, &int_keys[bench_i_ & (N - 1)], &out) ? 1u : 0u);
        bench_keep(&out);
    }

    arena_free(&ar);
}

/* String keys, which is what half the maps in a real program are keyed by.
 * Eleven bytes, so FNV-1a does eleven rounds of multiply and xor while Go does
 * one AES round, and this is the benchmark that says how much that is worth. */
BENCH(map_get_hit_str) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_str_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        bench_keep(map_get(m, &str_keys[bench_i_ & (N - 1)]));
    }

    arena_free(&ar);
}

/* The string miss. Same hash cost, and the keys differ in their first three
 * bytes, so any comparison that does happen ends immediately. */
BENCH(map_get_miss_str) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_str_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        bench_keep(map_get(m, &str_misses[bench_i_ & (N - 1)]));
    }

    arena_free(&ar);
}

/* ------------------------------------------------------------------- writes */

/* Writing to a key that is already there, which never grows the table and is
 * therefore a lookup plus a store. Counters and caches spend their lives here. */
BENCH(map_set_update) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        Int i = bench_i_ & (N - 1);
        map_set(m, &int_keys[i], &int_keys[i]);
    }

    bench_keep_u64((uint64_t)map_len(m));
    arena_free(&ar);
}

/* Building a thousand entry map from nothing with no hint, so the table
 * doubles eleven times on the way and every doubling rehashes everything that
 * is already in it.
 *
 * This is the benchmark where the two designs genuinely differ. burrow pays for
 * the whole rehash on one insert and Go pays for a split on several, so the
 * totals should be close and the worst single insert should not be. */
BENCH(map_set_grow) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    build_keys();

    BENCH_LOOP(b) {
        Map *m = map_make(a, TYPE_INT, TYPE_INT, 0);
        Int i;
        for (i = 0; i < N; i++)
            map_set(m, &int_keys[i], &int_keys[i]);
        bench_keep_u64((uint64_t)map_len(m));
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* The same thousand inserts with the size given up front, which is
 * make(map[int]int, n) and is what code that knows its own size should be
 * doing. The gap against map_set_grow is what the eleven rehashes cost. */
BENCH(map_set_prealloc) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    build_keys();

    BENCH_LOOP(b) {
        Map *m = map_make(a, TYPE_INT, TYPE_INT, N);
        Int i;
        for (i = 0; i < N; i++)
            map_set(m, &int_keys[i], &int_keys[i]);
        bench_keep_u64((uint64_t)map_len(m));
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* String keys, built the same way, because the hash shows up in construction
 * as well as in lookup and the two costs are not the same shape. */
BENCH(map_set_prealloc_str) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    build_keys();

    BENCH_LOOP(b) {
        Map *m = map_make(a, TYPE_STRING, TYPE_INT, N);
        Int i;
        for (i = 0; i < N; i++)
            map_set(m, &str_keys[i], &i);
        bench_keep_u64((uint64_t)map_len(m));
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* ---------------------------------------------------------------- deleting */

/* Deleting a key that is not there, which is the probe and nothing else. Worth
 * its own number because it is the cheapest possible delete and it sets the
 * floor for the pair below. */
BENCH(map_del_miss) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        map_del(m, &int_misses[bench_i_ & (N - 1)]);
    }

    bench_keep_u64((uint64_t)map_len(m));
    arena_free(&ar);
}

/* Delete a key that is there and put it straight back, because measuring the
 * delete on its own would empty the table after the first pass through the
 * keys and then measure an empty one. The pair is also the shape of a real
 * workload: an LRU moves an entry by removing it and reinserting it.
 *
 * Both sides do the same two operations in the same order. */
BENCH(map_set_del_pair) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        Int i = bench_i_ & (N - 1);
        map_del(m, &int_keys[i]);
        map_set(m, &int_keys[i], &int_keys[i]);
    }

    bench_keep_u64((uint64_t)map_len(m));
    arena_free(&ar);
}

/* ---------------------------------------------------------------- the rest */

/* Walking the whole table, which is for k, v := range m. Both sides walk the
 * slots in storage order from a random starting point, so this is a linear scan
 * over the control bytes with a skip for every slot that is not full. */
BENCH(map_iter) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        const void *k;
        void *v;
        Int sum = 0;
        MapIter it = map_iter(m);
        while (map_next(&it, &k, &v))
            sum += *(Int *)v;
        bench_keep_u64((uint64_t)sum);
    }

    arena_free(&ar);
}

/* Fill a thousand, delete a thousand, forever, in one map that is never
 * remade. This is what a cache does and it is the benchmark that proves the
 * tombstones get reclaimed: the table has to reach a steady state instead of
 * growing without bound while holding nothing.
 *
 * The reported bytes are for the whole run rather than per iteration, so the
 * number to look at is whether it stops moving as the iteration count goes up. */
BENCH(map_churn) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = map_make(arena_allocator(&ar), TYPE_INT, TYPE_INT, 0);
    build_keys();

    BENCH_LOOP(b) {
        Int i;
        for (i = 0; i < N; i++)
            map_set(m, &int_keys[i], &int_keys[i]);
        for (i = 0; i < N; i++)
            map_del(m, &int_keys[i]);
        bench_keep_u64((uint64_t)map_len(m));
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* clear then refill, which is the other way to reuse a map and is the one that
 * keeps the memory. Go's clear is a builtin that walks the table, and so is
 * this, so the comparison is direct. */
BENCH(map_clear_refill) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Map *m = filled_int_map(arena_allocator(&ar));

    BENCH_LOOP(b) {
        Int i;
        map_clear(m);
        for (i = 0; i < N; i++)
            map_set(m, &int_keys[i], &int_keys[i]);
        bench_keep_u64((uint64_t)map_len(m));
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

void register_map_benchmarks(void);

void register_map_benchmarks(void) {
    BENCH_RUN(map_get_hit_int);
    BENCH_RUN(map_get_miss_int);
    BENCH_RUN(map_get2_hit_int);
    BENCH_RUN(map_get_hit_str);
    BENCH_RUN(map_get_miss_str);
    BENCH_RUN(map_set_update);
    BENCH_RUN(map_set_grow);
    BENCH_RUN(map_set_prealloc);
    BENCH_RUN(map_set_prealloc_str);
    BENCH_RUN(map_del_miss);
    BENCH_RUN(map_set_del_pair);
    BENCH_RUN(map_iter);
    BENCH_RUN(map_churn);
    BENCH_RUN(map_clear_refill);
}
