/* The public sync/atomic layer.
 *
 * sync_bench.c measures burrow/atomic.h, which is the internal layer with the
 * memory order in every name. These rows measure burrow/sync/atomic.h, which is
 * the Go package sitting on top of it, and the two files are separate on
 * purpose: the question here is not what the instruction costs, it is what the
 * layer costs, and the only useful answer is nothing.
 *
 * Every row except the Value ones should be level with Go. Both sides compile
 * to one instruction, the C side is static inline over a compiler builtin and
 * the Go side is an intrinsic the compiler knows by name, and anything above
 * about a nanosecond apart is a wrapper that did not inline rather than
 * anything about the machine.
 *
 * One thread throughout, which is the case worth measuring. A contended counter
 * measures the cache coherence protocol and the core count of whatever box you
 * ran it on, and both languages get the same answer because it is the same
 * protocol.
 *
 * The Value rows are the ones with something to say. A load is a load of the
 * type word, a branch and a load of the data word, and a store after the first
 * one is a single store, so both should be a couple of nanoseconds and Go's
 * should be slightly ahead where its store of an interface avoids boxing.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/iface.h"
#include "burrow/sync/atomic.h"
#include "burrow/type.h"

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------- plain functions */

static int64_t counter;

BENCH(sync_atomic_add_int64) {
    int64_t *p = (int64_t *)bench_hide(&counter);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)sync_atomic_add_int64(p, 1));
    }
}

BENCH(sync_atomic_load_int64) {
    int64_t *p = (int64_t *)bench_hide(&counter);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)sync_atomic_load_int64(p));
    }
}

/* ---------------------------------------------------------------- the types
 *
 * The same operations through the struct, which should cost exactly what the
 * plain ones cost since the method is the function with the address of the
 * field. A gap on these rows and not on the ones above means the struct got in
 * the way, which it has no business doing. */

static SyncAtomicInt64 typed;
static SyncAtomicBool flag;
static SyncAtomicPointer ptr;

BENCH(sync_atomic_int64_add) {
    SyncAtomicInt64 *a = (SyncAtomicInt64 *)bench_hide(&typed);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)sync_atomic_int64_add(a, 1));
    }
}

BENCH(sync_atomic_int64_load) {
    SyncAtomicInt64 *a = (SyncAtomicInt64 *)bench_hide(&typed);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)sync_atomic_int64_load(a));
    }
}

/* Always succeeds, because a failing compare and swap measures the retry rather
 * than the instruction. The Go row is written the same way. */
BENCH(sync_atomic_int64_cas) {
    SyncAtomicInt64 *a = (SyncAtomicInt64 *)bench_hide(&typed);

    sync_atomic_int64_store(a, 0);
    BENCH_LOOP(b) {
        if (sync_atomic_int64_compare_and_swap(a, bench_i_, bench_i_ + 1))
            bench_keep_u64(1);
    }
}

BENCH(sync_atomic_bool_load) {
    SyncAtomicBool *f = (SyncAtomicBool *)bench_hide(&flag);

    sync_atomic_bool_store(f, true);
    BENCH_LOOP(b) {
        bench_keep_u64(sync_atomic_bool_load(f) ? 1u : 0u);
    }
}

BENCH(sync_atomic_pointer_load) {
    SyncAtomicPointer *p = (SyncAtomicPointer *)bench_hide(&ptr);

    sync_atomic_pointer_store(p, &counter);
    BENCH_LOOP(b) {
        bench_keep(sync_atomic_pointer_load(p));
    }
}

BENCH(sync_atomic_pointer_store) {
    SyncAtomicPointer *p = (SyncAtomicPointer *)bench_hide(&ptr);
    void *v = (void *)bench_hide(&counter);

    BENCH_LOOP(b) {
        sync_atomic_pointer_store(p, v);
    }
}

/* ------------------------------------------------------------------- Value
 *
 * The first store is the expensive one and it happens once, so these measure
 * the path everything after it takes: a store is one store, and a load is a
 * load, a branch and a load. */

static SyncAtomicValue boxed;

BENCH(sync_atomic_value_load) {
    static Int n = 1234;
    SyncAtomicValue *v = (SyncAtomicValue *)bench_hide(&boxed);

    sync_atomic_value_store(v, BURROW_ANY(TYPE_INT, &n));
    BENCH_LOOP(b) {
        bench_keep(sync_atomic_value_load(v).data);
    }
}

BENCH(sync_atomic_value_store) {
    static Int n = 1234;
    SyncAtomicValue *v = (SyncAtomicValue *)bench_hide(&boxed);
    Any val = BURROW_ANY(TYPE_INT, &n);

    sync_atomic_value_store(v, val);
    BENCH_LOOP(b) {
        sync_atomic_value_store(v, val);
    }
}

void register_sync_atomic_benchmarks(void);

void register_sync_atomic_benchmarks(void) {
    BENCH_RUN(sync_atomic_add_int64);
    BENCH_RUN(sync_atomic_load_int64);
    BENCH_RUN(sync_atomic_int64_add);
    BENCH_RUN(sync_atomic_int64_load);
    BENCH_RUN(sync_atomic_int64_cas);
    BENCH_RUN(sync_atomic_bool_load);
    BENCH_RUN(sync_atomic_pointer_load);
    BENCH_RUN(sync_atomic_pointer_store);
    BENCH_RUN(sync_atomic_value_load);
    BENCH_RUN(sync_atomic_value_store);
}
