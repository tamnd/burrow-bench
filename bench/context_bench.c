/* Context against Go's context.
 *
 * Both sides are the same tree with the same three operations on it: make a
 * node under a parent, ask a node a question, and run a cancellation down from
 * a node. burrow's is a port of Go's, so these rows compare two
 * implementations of one design.
 *
 * Two things differ and both show up in the numbers below, so they are worth
 * knowing before reading them.
 *
 * burrow makes the done channel when the context is made, where Go makes it the
 * first time somebody asks. Go can allocate anywhere and context_done cannot:
 * it has no allocator and no way to report a failure. So the WithCancel rows
 * here carry a channel Go's do not, and the Done rows are a plain load where
 * Go's is an atomic load and a branch. The trade is deliberate and these are
 * the two rows that show both sides of it.
 *
 * burrow keeps the children in an intrusive doubly linked list where Go keeps a
 * map[canceler]struct{}. Attaching a child costs four pointer writes here and a
 * map insert there, so the WithCancel rows should favour burrow on that count
 * and the cancel rows should favour it more, since walking a list is not
 * walking a map. Go cannot make that choice, because it has nowhere to put the
 * links.
 *
 * Everything is freed inside the timed loop, because there is no collector and
 * a row that leaves the freeing out is a row about half of an operation. Go's
 * side hands the node to the collector and the allocation shows up there
 * instead, as the allocs per operation the Go harness reports. Neither side is
 * being flattered.
 *
 * Everything runs on a goroutine rather than on the thread that starts the
 * process, because cancelling closes a channel and closing one wants a P.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/context.h"
#include "burrow/core.h"
#include "burrow/error.h"
#include "burrow/func.h"
#include "burrow/iface.h"
#include "burrow/mem.h"
#include "burrow/mem/heap.h"
#include "burrow/proc.h"
#include "burrow/time.h"
#include "burrow/type.h"

#include <stdint.h>

/* A key type private to this file, which is the pattern the header describes.
 * Written out by hand because the macro that generates a descriptor is on the
 * reflect milestone and is not here yet. */
static const Type key_desc = {
    {(const Byte *)"benchKey", 8},
    {(const Byte *)"bench", 5},
    KIND_INT,
    (uint32_t)sizeof(Int),
    (uint16_t)_Alignof(Int),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x626b6579U, /* "bkey" */
    NULL,
};

/* Two keys of that type, told apart by their value, which is Go's `type ctxKey
 * int` with two constants of it. The second one is never put into a context and
 * is what the miss row looks for. */
static Int key_one = 1;
static Int key_two = 2;

#define KEY_ONE BURROW_ANY(&key_desc, &key_one)
#define KEY_TWO BURROW_ANY(&key_desc, &key_two)

static Int payload = 42;

#define VALUE BURROW_ANY(TYPE_INT, &payload)

/* How deep the deep lookup goes. Eight, because Go's own advice is to keep a
 * chain short and eight is already longer than a well built program has. */
#define DEPTH 8

/* How many children the cancel row puts under one parent. */
#define FANOUT 64

static void run(Bench *b, void (*body)(void *)) {
    bench_pause(b);
    runtime_main(BURROW_FN(Func, body, b));
}

/* --------------------------------------------------------------- the questions
 *
 * What it costs to ask a context something, which is what the code holding one
 * does over and over.
 *
 * Done is the row that matters most, because it is the one every select on a
 * cancellation goes through. Here it is a load of a field written once before
 * the node was visible to anybody. Go's is an atomic load of the channel
 * followed by a branch into the slow path that makes one, since Go makes it
 * lazily.
 *
 * Err takes the node's mutex on both sides, because the error is written under
 * it at the moment the channel closes, so this row is an uncontended lock and
 * unlock plus two loads. */

static void done_body(void *arg) {
    Bench *b = (Bench *)arg;
    ContextCancelFunc cancel;
    Context c = context_with_cancel(heap_allocator(), context_background(), &cancel);

    bench_resume(b);
    BENCH_LOOP(b) {
        bench_keep(context_done(c));
    }
    bench_pause(b);

    BURROW_CALLF0(cancel);
    context_release(c);
}

BENCH(context_done) {
    run(b, done_body);
}

static void err_body(void *arg) {
    Bench *b = (Bench *)arg;
    ContextCancelFunc cancel;
    Context c = context_with_cancel(heap_allocator(), context_background(), &cancel);

    bench_resume(b);
    BENCH_LOOP(b) {
        bench_keep(context_err(c).vt);
    }
    bench_pause(b);

    BURROW_CALLF0(cancel);
    context_release(c);
}

BENCH(context_err_live) {
    run(b, err_body);
}

/* ------------------------------------------------------------- the value walk
 *
 * A lookup is a pointer chase per context between where it starts and where the
 * value is, with one interface comparison at each step. It is not a map and it
 * is not meant to be, on either side.
 *
 * The shallow row is the value sitting on the context being asked, so it is one
 * comparison. The deep row is eight contexts deep, so it is eight, and the two
 * together give the per level cost. The miss row walks all eight and then the
 * root and finds nothing, which is what a lookup for a key somebody else's
 * package owns costs.
 *
 * burrow walks this as a loop over the nodes it knows rather than as a call per
 * level, the same way Go's free standing value function does, so a deep chain is
 * a loop and not a hundred stack frames. */

static void value_shallow_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context c = context_with_value(a, context_background(), KEY_ONE, VALUE);

    bench_resume(b);
    BENCH_LOOP(b) {
        bench_keep(context_value(c, KEY_ONE).data);
    }
    bench_pause(b);

    context_release(c);
}

BENCH(context_value_shallow) {
    run(b, value_shallow_body);
}

/* Builds the chain the two deep rows walk. The value goes in at the bottom, so
 * the lookup has to cross every level above it. */
static Int build(Alloc *a, Context *chain) {
    chain[0] = context_with_value(a, context_background(), KEY_ONE, VALUE);
    if (BURROW_CONTEXT_IS_NIL(chain[0]))
        return 0;

    for (Int i = 1; i < DEPTH; i++) {
        chain[i] = context_with_value(a, chain[i - 1], KEY_TWO, VALUE);
        if (BURROW_CONTEXT_IS_NIL(chain[i]))
            return i;
    }
    return DEPTH;
}

static void unbuild(Context *chain, Int n) {
    while (n > 0)
        context_release(chain[--n]);
}

static void value_deep_body(void *arg) {
    Bench *b = (Bench *)arg;
    Context chain[DEPTH];
    Int n = build(heap_allocator(), chain);

    if (n != DEPTH) {
        unbuild(chain, n);
        return;
    }

    bench_resume(b);
    BENCH_LOOP(b) {
        bench_keep(context_value(chain[DEPTH - 1], KEY_ONE).data);
    }
    bench_pause(b);

    unbuild(chain, n);
}

BENCH(context_value_deep) {
    run(b, value_deep_body);
}

/* The same chain, and a key of the right type that nobody put in. Every level
 * compares and fails, then the root answers nothing. */
static Int key_three = 3;

static void value_miss_body(void *arg) {
    Bench *b = (Bench *)arg;
    Context chain[DEPTH];
    Int n = build(heap_allocator(), chain);

    if (n != DEPTH) {
        unbuild(chain, n);
        return;
    }

    bench_resume(b);
    BENCH_LOOP(b) {
        bench_keep(
            context_value(chain[DEPTH - 1], BURROW_ANY(&key_desc, &key_three)).t);
    }
    bench_pause(b);

    unbuild(chain, n);
}

BENCH(context_value_miss) {
    run(b, value_miss_body);
}

/* --------------------------------------------------------------- making one
 *
 * The whole life of a context in one iteration: make it, cancel it, free it.
 * That is what a request handler does, so it is the row a server cares about.
 *
 * The WithCancel row is a node, a channel, the four pointer writes that attach
 * it to the parent, the close and the two frees. Go's is a node, a map insert,
 * a map delete and a close, with the channel made by the Done call the cancel
 * does not need to make and the collector taking the rest.
 *
 * The WithValue row is a four word node and no lock at all on either side. */

static void with_cancel_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context root = context_background();

    bench_resume(b);
    BENCH_LOOP(b) {
        ContextCancelFunc cancel;
        Context c = context_with_cancel(a, root, &cancel);

        bench_keep(c.data);
        BURROW_CALLF0(cancel);
        context_release(c);
    }
    bench_pause(b);
}

BENCH(context_with_cancel) {
    run(b, with_cancel_body);
}

/* The same life with a deadline on it, which is what a request handler that
 * talks to anything over a network actually writes.
 *
 * An hour out, so the timer never fires and what is timed is arming it and
 * stopping it again. That is a heap insert and a heap removal on both sides,
 * plus everything the WithCancel row above already does, so the difference
 * between the two rows is what a deadline costs.
 *
 * Go arms a runtime timer directly. burrow goes through time_after_func, which
 * is a TimeTimer holding a burrow__Timer, so there is an allocation here that
 * Go does not have. Both put the timer on the P the caller is running on. */

#define AN_HOUR (3600 * TIME_SECOND)

static void with_timeout_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context root = context_background();

    bench_resume(b);
    BENCH_LOOP(b) {
        ContextCancelFunc cancel;
        Context c = context_with_timeout(a, root, AN_HOUR, &cancel);

        bench_keep(c.data);
        BURROW_CALLF0(cancel);
        context_release(c);
    }
    bench_pause(b);
}

BENCH(context_with_timeout) {
    run(b, with_timeout_body);
}

/* A deadline that has already gone by, which is what a request arriving with no
 * time left on its budget looks like.
 *
 * No timer is armed on either side. The context comes back with its done
 * channel already closed, so this row is the constructor plus a cancellation
 * that reaches one node, and it should come out near the WithCancel row rather
 * than near the one above. It is here because the early path is easy to get
 * wrong in a way that costs a timer nobody needed. */

static void with_timeout_expired_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context root = context_background();

    bench_resume(b);
    BENCH_LOOP(b) {
        ContextCancelFunc cancel;
        Context c = context_with_timeout(a, root, 0, &cancel);

        bench_keep(c.data);
        BURROW_CALLF0(cancel);
        context_release(c);
    }
    bench_pause(b);
}

BENCH(context_with_timeout_expired) {
    run(b, with_timeout_expired_body);
}

static void with_value_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context root = context_background();

    bench_resume(b);
    BENCH_LOOP(b) {
        Context c = context_with_value(a, root, KEY_ONE, VALUE);

        bench_keep(c.data);
        context_release(c);
    }
    bench_pause(b);
}

BENCH(context_with_value) {
    run(b, with_value_body);
}

/* A child under a cancellable parent rather than under the root, which is the
 * case that has a list to join and a cancel to detach from. The row above has
 * neither, because Background is never cancelled, so the difference between the
 * two is what the attaching and the detaching cost. */
static void with_cancel_nested_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    ContextCancelFunc parent_cancel;
    Context parent = context_with_cancel(a, context_background(), &parent_cancel);

    if (BURROW_CONTEXT_IS_NIL(parent))
        return;

    bench_resume(b);
    BENCH_LOOP(b) {
        ContextCancelFunc cancel;
        Context c = context_with_cancel(a, parent, &cancel);

        bench_keep(c.data);
        BURROW_CALLF0(cancel);
        context_release(c);
    }
    bench_pause(b);

    BURROW_CALLF0(parent_cancel);
    context_release(parent);
}

BENCH(context_with_cancel_nested) {
    run(b, with_cancel_nested_body);
}

/* ---------------------------------------------------------------- the causes
 *
 * The four entry points Go added in 1.20 and 1.21, which are the ones a program
 * written today reaches for and the ones that were missing here until now.
 *
 * WithCancelCause is WithCancel with a second word written under the same lock
 * at the same moment, so the row should sit on top of the WithCancel row and
 * the difference between them is what carrying a reason costs. Cause is a walk
 * up to the nearest cancellable node followed by that node's lock, which is the
 * Err row plus the value walk rather than a third thing.
 *
 * WithoutCancel is a node that answers nothing to the cancel key, so it is one
 * allocation on both sides and no channel and no list. It should be the
 * cheapest constructor here.
 *
 * AfterFunc is measured on the path where the stop wins, because that is what a
 * handler that finishes its work does and because the other path ends in a
 * goroutine and would be measuring the scheduler. So the row is a node, a
 * channel, the attach, the once, the detach and the frees. */

BURROW_SENTINEL_ERROR(bench_reason, "bench: the reason");

static void nothing(void *env) {
    (void)env;
}

static void cause_body(void *arg) {
    Bench *b = (Bench *)arg;
    ContextCancelCauseFunc cancel;
    Context c =
        context_with_cancel_cause(heap_allocator(), context_background(), &cancel);

    if (BURROW_CONTEXT_IS_NIL(c))
        return;

    BURROW_CALLF(cancel, bench_reason);

    bench_resume(b);
    BENCH_LOOP(b) {
        bench_keep(context_cause(c).vt);
    }
    bench_pause(b);

    context_release(c);
}

BENCH(context_cause) {
    run(b, cause_body);
}

static void with_cancel_cause_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context root = context_background();

    bench_resume(b);
    BENCH_LOOP(b) {
        ContextCancelCauseFunc cancel;
        Context c = context_with_cancel_cause(a, root, &cancel);

        bench_keep(c.data);
        BURROW_CALLF(cancel, bench_reason);
        context_release(c);
    }
    bench_pause(b);
}

BENCH(context_with_cancel_cause) {
    run(b, with_cancel_cause_body);
}

static void without_cancel_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    ContextCancelFunc parent_cancel;
    Context parent = context_with_cancel(a, context_background(), &parent_cancel);

    if (BURROW_CONTEXT_IS_NIL(parent))
        return;

    bench_resume(b);
    BENCH_LOOP(b) {
        Context c = context_without_cancel(a, parent);

        bench_keep(c.data);
        context_release(c);
    }
    bench_pause(b);

    BURROW_CALLF0(parent_cancel);
    context_release(parent);
}

BENCH(context_without_cancel) {
    run(b, without_cancel_body);
}

static void after_func_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    ContextCancelFunc parent_cancel;
    Context parent = context_with_cancel(a, context_background(), &parent_cancel);

    if (BURROW_CONTEXT_IS_NIL(parent))
        return;

    bench_resume(b);
    BENCH_LOOP(b) {
        StopFunc stop;
        Context reg =
            context_after_func(a, parent, BURROW_FN(Func, nothing, NULL), &stop);

        bench_keep(reg.data);
        bench_keep_u64((uint64_t)BURROW_CALLF0(stop));
        context_release(reg);
    }
    bench_pause(b);

    BURROW_CALLF0(parent_cancel);
    context_release(parent);
}

BENCH(context_after_func) {
    run(b, after_func_body);
}

/* ------------------------------------------------------------- the cancel
 *
 * One parent, sixty four children, and a cancel that has to reach all of them.
 * That is the shape a server has when one request fans out, and it is the row
 * the children being a list rather than a map is supposed to win.
 *
 * Building the tree is inside the timed loop, because the alternative is timing
 * a cancel on a tree that was already cancelled, and a cancelled node is
 * detached and there is nothing left to walk. So this row is sixty four
 * WithCancels plus the cancel plus sixty five frees, and it should be read
 * against the WithCancel row above rather than on its own. What the comparison
 * against Go says is whether the whole shape costs more or less, which is the
 * question somebody choosing between the two actually has. */

static void cancel_tree_body(void *arg) {
    Bench *b = (Bench *)arg;
    Alloc *a = heap_allocator();
    Context kids[FANOUT];
    ContextCancelFunc kid_cancel;

    bench_resume(b);
    BENCH_LOOP(b) {
        ContextCancelFunc cancel;
        Context parent = context_with_cancel(a, context_background(), &cancel);
        Int made = 0;

        for (Int i = 0; i < FANOUT; i++) {
            kids[i] = context_with_cancel(a, parent, &kid_cancel);
            if (BURROW_CONTEXT_IS_NIL(kids[i]))
                break;
            made++;
        }

        BURROW_CALLF0(cancel);

        unbuild(kids, made);
        context_release(parent);
    }
    bench_pause(b);
}

BENCH(context_cancel_tree) {
    run(b, cancel_tree_body);
}

void register_context_benchmarks(void);

void register_context_benchmarks(void) {
    BENCH_RUN(context_done);
    BENCH_RUN(context_err_live);
    BENCH_RUN(context_value_shallow);
    BENCH_RUN(context_value_deep);
    BENCH_RUN(context_value_miss);
    BENCH_RUN(context_with_cancel);
    BENCH_RUN(context_with_cancel_nested);
    BENCH_RUN(context_with_timeout);
    BENCH_RUN(context_with_timeout_expired);
    BENCH_RUN(context_with_value);
    BENCH_RUN(context_cause);
    BENCH_RUN(context_with_cancel_cause);
    BENCH_RUN(context_without_cancel);
    BENCH_RUN(context_after_func);
    BENCH_RUN(context_cancel_tree);
}
