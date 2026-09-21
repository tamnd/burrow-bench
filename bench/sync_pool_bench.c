/* SyncPool against Go's sync.Pool.
 *
 * Both sides are the same design. A private slot and a ring buffer chain per P,
 * a steal from the far end of somebody else's ring when your own is empty, and
 * a victim generation behind the live one so that what you put in survives one
 * drain and not two. burrow's is a port of Go's, so these rows compare two
 * implementations of one structure.
 *
 * What differs is underneath. Go drops an object and the collector takes it,
 * where burrow hands it to a free function the caller supplied, and Go empties
 * a pool at a garbage collection where burrow does it on the system monitor's
 * timer. Neither of those is on any path measured here, which is the point: the
 * rows below are the Get and the Put, and those are the same two loads and two
 * stores on both sides.
 *
 * Nothing here allocates in the loop. The new function hands back a pointer to
 * a static object rather than making one, on both sides, because a row whose
 * new function allocates is a row about the allocator. What the miss row is
 * about is the walk that happens before the new function is reached, which is
 * an empty private slot, an empty ring of your own, and a scan of everybody
 * else's that finds nothing.
 *
 * There is no steal row. A steal needs a second P with something in its ring,
 * and the runner pins the process to one CPU so that the numbers are stable.
 * See the README.
 *
 * Everything runs on a goroutine rather than on the thread that starts the
 * process. A pool sends a goroutine to its own P's slot and sends anything else
 * to one extra slot shared under a mutex, so the thread this runs on is the
 * difference between the fast path and a lock.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/func.h"
#include "burrow/iface.h"
#include "burrow/mem.h"
#include "burrow/mem/heap.h"
#include "burrow/proc.h"
#include "burrow/sync.h"
#include "burrow/type.h"

#include <stddef.h>
#include <stdint.h>

/* What the pool holds. Big enough to be a real object and small enough that
 * nothing here is about memory bandwidth. */
typedef struct Buf {
    char data[64];
} Buf;

/* Written out by hand, because the macro that generates one of these is on the
 * reflect milestone and is not here yet. A pool never looks past the pointer,
 * so a descriptor with no operations on it is enough. */
static const Type buf_desc = {
    {(const Byte *)"Buf", 3},
    {(const Byte *)"bench", 5},
    KIND_STRUCT,
    (uint32_t)sizeof(Buf),
    (uint16_t)_Alignof(Buf),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x66756263U, /* "cbuf" */
    NULL,
};
static const Type *const TYPE_BUF = &buf_desc;

/* The objects the new function hands out, and where it is up to. A ring of them
 * rather than one, so that a row which holds several at once holds several
 * different ones and the cache behaves the way it would in a real pool. */
#define POOL_OBJS 64

static Buf objs[POOL_OBJS];
static size_t next_obj;

static Any make_buf(void *env) {
    (void)env;

    Buf *b = &objs[next_obj];
    next_obj = (next_obj + 1) & (POOL_OBJS - 1);
    return BURROW_ANY(TYPE_BUF, b);
}

/* Nil would do, since nothing here allocates, but a real pool has one and the
 * drain paths should be measured with one in place. */
static void drop_buf(void *env, Any v) {
    (void)env;
    bench_keep(v.data);
}

/* Carries the pool and the benchmark into the goroutine, since a goroutine body
 * takes one pointer. */
typedef struct Ctx {
    Bench *b;
    SyncPool p;
} Ctx;

static void run_pool(Bench *b, void (*body)(void *)) {
    Ctx c;

    next_obj = 0;
    c.b = b;
    c.p = SYNC_POOL(heap_allocator(), BURROW_FN(SyncPoolNewFunc, make_buf, NULL),
                    BURROW_FN(SyncPoolFreeFunc, drop_buf, NULL));

    bench_pause(b);
    runtime_main(BURROW_FN(Func, body, &c));
}

/* ------------------------------------------------------------ the fast path
 *
 * A Get and a Put back to back on one P. The Put fills the private slot and the
 * Get takes it out again, so this is a load, a compare and two stores, with no
 * atomic anywhere and no memory another core has ever seen.
 *
 * It is the row a pool exists for, and it is the number to hold against
 * whatever making the object would have cost. A pool is worth having when this
 * is smaller than that and is not worth having when it is not. */

static void get_put_body(void *arg) {
    Ctx *c = (Ctx *)arg;

    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Any v = sync_pool_get(&c->p);
        bench_keep(v.data);
        sync_pool_put(&c->p, v);
    }
    bench_pause(c->b);
    sync_pool_free(&c->p);
}

BENCH(sync_pool_get_put) {
    run_pool(b, get_put_body);
}

/* ------------------------------------------------------------- the ring path
 *
 * Two out and two back per iteration. The private slot holds one, so the second
 * of each pair goes to the head of this P's ring, which is a packed load, a
 * store and a release store to publish the slot.
 *
 * Read it against the row above and divide the difference by one, since one of
 * the two pairs here is the fast path and the other is the ring. That gap is
 * what the ring costs over the private slot. */

static void get_put_pair_body(void *arg) {
    Ctx *c = (Ctx *)arg;

    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Any v1 = sync_pool_get(&c->p);
        Any v2 = sync_pool_get(&c->p);
        bench_keep(v1.data);
        bench_keep(v2.data);
        sync_pool_put(&c->p, v1);
        sync_pool_put(&c->p, v2);
    }
    bench_pause(c->b);
    sync_pool_free(&c->p);
}

BENCH(sync_pool_get_put_pair) {
    run_pool(b, get_put_pair_body);
}

/* ------------------------------------------------------------- the miss path
 *
 * A Get on a pool that has nothing in it, so every iteration walks the whole
 * way: an empty private slot, an empty ring of this P's own, a scan of every
 * other P's ring that finds nothing, the victim generation behind all of that,
 * and then the new function.
 *
 * Nothing is put back, which is what keeps the pool empty. The new function
 * hands out a pointer to a static object, so what is left in the number is the
 * walk and not an allocation. It is the worst case and it is also the first
 * call a program makes, so it is worth knowing what it costs to find out that
 * there is nothing there. */

static void get_new_body(void *arg) {
    Ctx *c = (Ctx *)arg;

    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        Any v = sync_pool_get(&c->p);
        bench_keep(v.data);
    }
    bench_pause(c->b);
    sync_pool_free(&c->p);
}

BENCH(sync_pool_get_new) {
    run_pool(b, get_new_body);
}

/* ---------------------------------------------------------------- the sweep
 *
 * What the system monitor's tick costs when there is nothing to do, which is
 * what it costs almost every time it happens. It takes the registry lock, walks
 * the pools, and for each one walks the victim generation's rings and finds
 * them empty, then swaps the two generations over.
 *
 * No Go column. Go has no such call, because in Go this happens inside a
 * garbage collection and there is nothing to time on its own. The row is here
 * to answer the question the design raises, which is whether a timer that fires
 * once a second in every burrow program costs anything worth caring about.
 *
 * One pool in the process, so multiply by however many a real program has. */

static void sweep_body(void *arg) {
    Ctx *c = (Ctx *)arg;

    /* Used once, so it is on the registry and the sweep has something to walk.
     * Put back, so the generations have something to swap. */
    Any v = sync_pool_get(&c->p);
    sync_pool_put(&c->p, v);

    bench_resume(c->b);
    BENCH_LOOP(c->b) {
        burrow__pool_sweep();
    }
    bench_pause(c->b);
    sync_pool_free(&c->p);
}

BENCH(sync_pool_sweep_idle) {
    run_pool(b, sweep_body);
}

void register_sync_pool_benchmarks(void);

void register_sync_pool_benchmarks(void) {
    BENCH_RUN(sync_pool_get_put);
    BENCH_RUN(sync_pool_get_put_pair);
    BENCH_RUN(sync_pool_get_new);
    BENCH_RUN(sync_pool_sweep_idle);
}
