/* SyncMap against Go's sync.Map.
 *
 * Both sides are the same data structure. Go replaced the read map and dirty
 * map pair with a hash trie in 1.24, and burrow's is a port of that trie, so
 * these rows compare two implementations of one design rather than two designs.
 * Where they diverge is underneath: Go reclaims an unlinked node with the
 * collector and burrow reclaims it with the epoch scheme in reclaim_bench.c, so
 * the write rows here are the rows where that difference shows up.
 *
 * One thing to know before reading the columns. Go's sync.Map is keyed by any,
 * so a Go caller with an int key pays an interface conversion on every call,
 * and for a value that does not fit in the small integer cache that conversion
 * is an allocation. The Go side boxes its keys and values once during setup and
 * reuses them, which takes that cost out of the loop. Without it the Go column
 * would mostly be the allocator. So these ratios are the two maps, and a real
 * Go program using an int key pays more than the Go column says.
 *
 * Everything here runs on a goroutine rather than on the thread that starts the
 * process. A read takes a reclamation pin, a pin from a goroutine is an array
 * index off its M, and a pin from a foreign thread takes a slot out of a pool
 * with a compare and swap. The goroutine path is the one a burrow program is
 * on. reclaim_pin_foreign is what the other one costs.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/func.h"
#include "burrow/mem.h"
#include "burrow/mem/heap.h"
#include "burrow/proc.h"
#include "burrow/sync.h"
#include "burrow/type.h"

#include <stdbool.h>
#include <stddef.h>

/* The same thousand and twenty four entries as map_bench.c, so a SyncMap row
 * can be read against the Map row next to it. A power of two so the index into
 * the key arrays is a mask. */
#define N 1024

static Int keys[N];   /* the keys that are in the map */
static Int misses[N]; /* the same count of keys that are not */

static void build_keys(void) {
    Int i;
    for (i = 0; i < N; i++) {
        keys[i] = i * 7 + 1; /* not 0..N, so the low bits are not the index */
        misses[i] = -(i * 7 + 1);
    }
}

/* Carries the map and the benchmark across into the goroutine, since a
 * goroutine body takes one pointer. */
typedef struct Ctx {
    Bench *b;
    SyncMap m;
} Ctx;

static void fill(SyncMap *m) {
    Int i;
    for (i = 0; i < N; i++)
        sync_map_store(m, &keys[i], &keys[i]);
}

/* Runs body on a goroutine with a filled map, then frees the map there too,
 * because freeing it walks the retired nodes and that wants an M. */
static void run_filled(Bench *b, void (*body)(void *)) {
    Ctx c;

    build_keys();
    c.b = b;
    c.m = SYNC_MAP(heap_allocator(), TYPE_INT, TYPE_INT);

    bench_pause(b);
    runtime_main(BURROW_FN(Func, body, &c));
}

/* ------------------------------------------------------------- the read side
 *
 * A hit walks the trie to an entry and compares the key, and pays one pin and
 * one unpin around the walk. A thousand entries is between two and three levels
 * of a sixteen way trie, so the walk is a couple of dependent loads, and this
 * row is those loads plus the pin.
 *
 * Nothing is locked and nothing is written, which is the whole point of the
 * structure, so this row should not move when other threads are reading. There
 * is no benchmark here that shows that, because the runner pins to one CPU. */

static void load_hit_body(void *arg) {
    Ctx *c = (Ctx *)arg;
    Int i = 0;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Int got = 0;
        sync_map_load(&c->m, &keys[i & (N - 1)], &got);
        bench_keep_u64((uint64_t)got);
        i++;
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

BENCH(sync_map_load_hit) {
    run_filled(b, load_hit_body);
}

static void load_miss_body(void *arg) {
    Ctx *c = (Ctx *)arg;
    Int i = 0;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Int got = 0;
        bench_keep_u64(sync_map_load(&c->m, &misses[i & (N - 1)], &got) ? 1u : 0u);
        i++;
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

/* A miss stops at the first empty slot or at an entry whose key does not match,
 * so it is usually shorter than a hit and never longer. */
BENCH(sync_map_load_miss) {
    run_filled(b, load_miss_body);
}

static void has_hit_body(void *arg) {
    Ctx *c = (Ctx *)arg;
    Int i = 0;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        bench_keep_u64(sync_map_load(&c->m, &keys[i & (N - 1)], NULL) ? 1u : 0u);
        i++;
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

/* The same lookup asking only whether the key is there, which is what
 * SYNC_MAP_HAS does. No Go column, because Go's Load always hands the value
 * back and there is nothing there to compare this against. It is here to say
 * what the copy costs, which is this row against the hit row above, and to say
 * that a presence check is worth writing as one. */
BENCH(sync_map_has_hit) {
    run_filled(b, has_hit_body);
}

static void load_or_store_hit_body(void *arg) {
    Ctx *c = (Ctx *)arg;
    Int i = 0;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Int k = keys[i & (N - 1)];
        Int got = 0;
        bool loaded = false;
        sync_map_load_or_store(&c->m, &k, &k, &got, &loaded);
        bench_keep_u64((uint64_t)got);
        i++;
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

/* LoadOrStore on a key that is already there. It looks the key up without
 * taking the lock first and returns, so this should land near the load row and
 * not near the store row. The version that has to store is the interesting one
 * to get wrong, and it is covered by the churn row below. */
BENCH(sync_map_load_or_store_hit) {
    run_filled(b, load_or_store_hit_body);
}

/* ------------------------------------------------------------ the write side
 *
 * These are the rows where the two sides stop being the same program.
 *
 * An entry in this map is immutable once it is published, so replacing a value
 * allocates a new entry, swaps it in, and hands the old one to the reclaimer.
 * Go does the same and lets the collector take the old one. So the C column is
 * an allocation plus a retire and the Go column is an allocation plus whatever
 * share of a future collection that node represents, which is not in the
 * column. Read the allocation counts alongside the times. */

static void store_update_body(void *arg) {
    Ctx *c = (Ctx *)arg;
    Int i = 0;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Int k = keys[i & (N - 1)];
        sync_map_store(&c->m, &k, &k);
        i++;
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

/* Storing over a key that is already there, which is the update path: lock the
 * node that owns the slot, build the replacement, swap, retire the old one. */
BENCH(sync_map_store_update) {
    run_filled(b, store_update_body);
}

static void store_delete_pair_body(void *arg) {
    Ctx *c = (Ctx *)arg;
    Int i = 0;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Int k = misses[i & (N - 1)];
        sync_map_store(&c->m, &k, &k);
        sync_map_delete(&c->m, &k);
        i++;
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

/* One insert and one delete of a key that was not there, which is the churn a
 * cache keyed by something short lived does all day. The insert can split a
 * node and the delete can prune one back, so this row carries the structural
 * work that the update row above does not. */
BENCH(sync_map_store_delete_pair) {
    run_filled(b, store_delete_pair_body);
}

/* ----------------------------------------------------------------- the walk */

static bool count_one(const void *key, const void *val, void *arg) {
    Int *n = (Int *)arg;

    bench_keep(key);
    bench_keep(val);
    (*n)++;
    return true;
}

static void range_body(void *arg) {
    Ctx *c = (Ctx *)arg;

    fill(&c->m);
    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Int n = 0;
        sync_map_range(&c->m, count_one, &n);
        bench_keep_u64((uint64_t)n);
    }
    bench_pause(c->b);
    sync_map_free(&c->m);
}

/* A whole walk of a thousand entries per iteration, so divide by a thousand for
 * the per entry cost. Go's walk takes nothing, no lock and no bookkeeping,
 * because the collector keeps a node alive for as long as the walk is holding
 * it. This one has to say so itself, and holds one pin across the whole walk
 * rather than one per entry, which is why the header says the callback must not
 * block. This is the row where that decision is visible. */
BENCH(sync_map_range) {
    run_filled(b, range_body);
}

void register_sync_map_benchmarks(void);

void register_sync_map_benchmarks(void) {
    BENCH_RUN(sync_map_load_hit);
    BENCH_RUN(sync_map_load_miss);
    BENCH_RUN(sync_map_has_hit);
    BENCH_RUN(sync_map_load_or_store_hit);
    BENCH_RUN(sync_map_store_update);
    BENCH_RUN(sync_map_store_delete_pair);
    BENCH_RUN(sync_map_range);
}
