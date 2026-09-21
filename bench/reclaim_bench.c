/* Epoch based reclamation, which is the thing burrow has instead of a
 * collector.
 *
 * No Go column here, and there cannot be one. Go's answer to this problem is
 * the garbage collector, so the fair comparison is not "pin against something"
 * but the whole of sync.Map against the whole of Go's, which is what the map
 * rows are for. These rows exist so that the floor under that comparison is a
 * number somebody is watching rather than an assumption.
 *
 * The pin row is the one that matters. It is a sequentially consistent store,
 * which is an xchg on x86 and a store release plus a fence on arm64, and every
 * lock free read in burrow pays it once. If it ever stops looking like one
 * atomic read modify write, something has been added to the fast path that
 * should not be there.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/func.h"
#include "burrow/mem.h"
#include "burrow/mem/heap.h"
#include "burrow/proc.h"
#include "burrow/reclaim.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------- the read side
 *
 * What a reader pays to be allowed to follow a pointer somebody else might be
 * unlinking. Nothing is being read here on purpose: the row is the permission
 * and not the work, because the work is whatever structure sits on top.
 *
 * On a goroutine, because a goroutine's slot is an array index off its M and
 * that is the path every lock free read inside burrow takes. The foreign row
 * below is the other one. */

static void pin_body(void *arg) {
    Bench *b = (Bench *)arg;

    bench_resume(b);
    BENCH_LOOP(b) {
        burrow__pin();
        burrow__unpin();
    }
    bench_pause(b);
}

BENCH(reclaim_pin_unpin) {
    bench_pause(b);
    runtime_main(BURROW_FN(Func, pin_body, b));
}

static void nested_body(void *arg) {
    Bench *b = (Bench *)arg;

    bench_resume(b);
    BENCH_LOOP(b) {
        burrow__pin();
        burrow__pin();
        burrow__unpin();
        burrow__unpin();
    }
    bench_pause(b);
}

/* A pin inside a pin, which is what a function that pins calling another
 * function that pins costs. The inner pair is a counter and nothing else, so
 * this should be the row above plus a few cycles and not twice it. */
BENCH(reclaim_pin_nested) {
    bench_pause(b);
    runtime_main(BURROW_FN(Func, nested_body, b));
}

/* The same pin from a thread the scheduler did not start, which is what a host
 * program calling into burrow gets. There is no M to hang a slot off, so each
 * pin takes one out of a pool with a compare and swap and gives it back on the
 * way out. It is the slow path on purpose and this row is how slow. */
BENCH(reclaim_pin_foreign) {
    BENCH_LOOP(b) {
        burrow__pin();
        burrow__unpin();
    }
}

/* ------------------------------------------------------------ the write side
 *
 * What it costs to hand one object over, averaged over a batch.
 *
 * Most of these are three stores and a counter. One in sixty four also walks
 * the participant slots and frees whatever has come far enough, and that is the
 * cost this row is really measuring, because it is the one that grows with the
 * number of threads on the machine.
 *
 * The objects go round a free list rather than through the allocator, because
 * an allocation is an order of magnitude more expensive than the thing being
 * measured and would be the whole row. Everything in flight fits in a few
 * hundred boxes, so the list settles immediately and the loop never allocates
 * again. */

typedef struct Box {
    burrow__Retired retired;
    struct Box *reuse;
} Box;

/* Only ever touched by the one goroutine running this benchmark, including from
 * box_free, since a collection happens on the thread that triggered it. */
static Box *pool;

static void box_free(void *p) {
    Box *x = (Box *)p;
    x->reuse = pool;
    pool = x;
}

static Box *box_get(void) {
    Box *x = pool;
    if (x != NULL) {
        pool = x->reuse;
        return x;
    }
    return BURROW_NEW(heap_allocator(), Box);
}

static void retire_body(void *arg) {
    Bench *b = (Bench *)arg;

    bench_resume(b);
    BENCH_LOOP(b) {
        Box *x = box_get();
        if (x == NULL)
            break;
        burrow__retire(&x->retired, box_free, x);
    }
    bench_pause(b);
}

BENCH(reclaim_retire) {
    /* On a goroutine, because a retire from a thread the scheduler did not
     * start has no pocket to put the object in and takes the shared lock every
     * time. That path is real and is what a host program calling in gets, and
     * it is not the one a lock free map spends its life on. */
    bench_pause(b);
    runtime_main(BURROW_FN(Func, retire_body, b));
    burrow__reclaim_drain();

    while (pool != NULL) {
        Box *x = pool;
        pool = x->reuse;
        mem_free(heap_allocator(), x, sizeof(Box), _Alignof(Box));
    }
}

void register_reclaim_benchmarks(void);

void register_reclaim_benchmarks(void) {
    BENCH_RUN(reclaim_pin_unpin);
    BENCH_RUN(reclaim_pin_nested);
    BENCH_RUN(reclaim_pin_foreign);
    BENCH_RUN(reclaim_retire);
}
