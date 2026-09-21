/* The two things a goroutine is made of: a stack, and a way to leave it.
 *
 * Neither of these is something a user calls. They are the floor the scheduler
 * will be built on, and the reason to measure them now rather than later is
 * that the scheduler's own numbers are only meaningful next to the cost of the
 * parts it is made of. If switching a context costs 15 nanoseconds and
 * switching a goroutine costs 400, the 385 in the middle is the scheduler, and
 * that is a number worth being able to see.
 *
 * The context row has a Go pair and the stack rows do not. Go allocates a
 * goroutine stack out of a per P cache of 2, 4, 8 and 16 kilobyte spans and
 * only goes to the operating system when that cache is empty, so a Go
 * benchmark of the same thing would be measuring the cache and this one
 * measures mmap. Those are different questions and pairing them would be
 * inventing a comparison rather than making one. burrow will have the same
 * cache in front of these calls and the rows for it go in when it does, right
 * next to these, because the point of having both is being able to see what the
 * cache is worth.
 *
 * These use the internal headers, which is why the names carry the burrow
 * double underscore. There is no public API over either of them yet and there
 * may never be one, since a stack and a context are things a runtime hands out
 * rather than things a program asks for.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/context.h"
#include "burrow/runtime.h"
#include "burrow/sched.h"
#include "burrow/slice.h"
#include "burrow/stack.h"
#include "burrow/type.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ context
 *
 * A switch there and back per iteration, because a switch on its own has
 * nowhere to return to and cannot be measured in a loop. So the number this
 * prints is two switches, and halving it is the cost of one.
 *
 * The Go pair is two goroutines passing a turn over an unbuffered channel with
 * GOMAXPROCS at 1, which is also two switches per iteration. It is a fair
 * comparison of the same shape of program and it is not a fair comparison of
 * the same work: Go's number has a channel, a scheduler decision and a
 * goroutine status change in it, and this one has a register save and a stack
 * pointer swap. The burrow row is a floor rather than a competitor, and the
 * distance to Go's row is the budget the scheduler has to fit inside. */

static burrow__Context rt_main;
static burrow__Context rt_worker;
static burrow__Stack rt_stack;
static volatile uint32_t rt_stop;

static void rt_bounce(void *arg) {
    (void)arg;

    /* Hands the turn straight back, forever, until the benchmark is over. A
     * loop rather than a chain of calls because the point is the switch, and
     * anything else in here would end up in the measurement. */
    while (rt_stop == 0)
        burrow__context_switch(&rt_worker, &rt_main);
}

BENCH(context_switch) {
    bench_pause(b);

    if (!burrow__stack_alloc(&rt_stack, 64u * 1024u))
        return;

    if (!burrow__context_attach(&rt_main)) {
        burrow__stack_free(&rt_stack);
        return;
    }

    size_t size = (size_t)((unsigned char *)rt_stack.hi - (unsigned char *)rt_stack.lo);
    rt_stop = 0;

    if (!burrow__context_make(&rt_worker, rt_stack.lo, size, rt_bounce, NULL,
                              &rt_main)) {
        burrow__context_detach(&rt_main);
        burrow__stack_free(&rt_stack);
        return;
    }

    bench_resume(b);

    BENCH_LOOP(b) {
        burrow__context_switch(&rt_main, &rt_worker);
    }

    bench_pause(b);

    /* One more turn so the worker sees the flag and returns, which sends it to
     * its link, which is this context. Freeing it while it is still sitting
     * inside a switch would leave a stack that nothing is ever coming back
     * from. */
    rt_stop = 1;
    burrow__context_switch(&rt_main, &rt_worker);

    burrow__context_free(&rt_worker);
    burrow__context_detach(&rt_main);
    burrow__stack_free(&rt_stack);

    bench_resume(b);
}

/* Laying a trampoline over a stack that already exists. No mapping and no
 * system call on the assembly paths, which is the point: making a context is
 * meant to be cheap enough that the scheduler can do it on every goroutine
 * launch without thinking about it. On Windows it is a fiber and therefore a
 * kernel object, and the row will say so loudly. */
static burrow__Stack rt_make_stack;
static burrow__Context rt_made;
static burrow__Context rt_link;

static void rt_nothing(void *arg) {
    (void)arg;
}

BENCH(context_make) {
    bench_pause(b);
    if (!burrow__stack_alloc(&rt_make_stack, 64u * 1024u))
        return;
    size_t size =
        (size_t)((unsigned char *)rt_make_stack.hi - (unsigned char *)rt_make_stack.lo);
    bench_resume(b);

    BENCH_LOOP(b) {
        if (!burrow__context_make(&rt_made, rt_make_stack.lo, size, rt_nothing, NULL,
                                  &rt_link))
            break;
        burrow__context_free(&rt_made);
    }

    bench_pause(b);
    burrow__stack_free(&rt_make_stack);
    bench_resume(b);
}

/* ------------------------------------------------------------------- stacks
 *
 * One mmap, one mprotect and one munmap per iteration, or the Windows
 * equivalent. This is the cost a goroutine launch pays when nothing is cached,
 * and it is the number the free list has to beat to be worth having.
 *
 * Two sizes, because the kernel's work is not the same at both ends. The
 * minimum is what a goroutine will actually be given, and the megabyte is there
 * because munmap has to walk the page tables for everything it takes back and a
 * row that only measured small mappings would hide that. */

static burrow__Stack rt_scratch;

BENCH(stack_alloc_free_min) {
    BENCH_LOOP(b) {
        if (!burrow__stack_alloc(&rt_scratch, BURROW_STACK_MIN))
            break;
        bench_keep(rt_scratch.lo);
        burrow__stack_free(&rt_scratch);
    }
}

BENCH(stack_alloc_free_large) {
    BENCH_LOOP(b) {
        if (!burrow__stack_alloc(&rt_scratch, 1024u * 1024u))
            break;
        bench_keep(rt_scratch.lo);
        burrow__stack_free(&rt_scratch);
    }
}

/* What the scheduler calls on every single context switch to tell the overflow
 * handler which stack the thread is on. It is a thread local store and it has
 * to stay one, because anything more expensive than this gets paid twice per
 * switch and the switch itself is only a few nanoseconds. This row exists so
 * that a day when it stops being a store shows up as a number. */
static burrow__Stack rt_named;

BENCH(stack_set_current) {
    burrow__Stack *p = (burrow__Stack *)bench_hide(&rt_named);

    BENCH_LOOP(b) {
        bench_keep(burrow__stack_set_current(p));
    }

    (void)burrow__stack_set_current(NULL);
}

/* Every size in the stack file is rounded to this, so it is asked for on every
 * allocation. The header claims it is a memory read rather than a system call
 * on the libcs burrow targets. This is the row that makes that claim checkable,
 * and a machine where it is a syscall will show it immediately. */
BENCH(stack_page_size) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)burrow__stack_page_size());
    }
}

/* ------------------------------------------------------------- the run queues
 *
 * The three operations the scheduler does on every single goroutine, so the
 * numbers here are a floor under every number the scheduler will ever print.
 * A put and a get together are what it costs to park a goroutine and pick it up
 * again on the same P with nothing else going on, and that is the path almost
 * every goroutine in almost every program takes.
 *
 * No Go pair, for the reason the top of this file gives about stacks. Go has no
 * way to reach its own run queues and inventing something that looks like one
 * would be measuring the thing built on top rather than the thing itself.
 *
 * One P and one thread throughout, which is the case worth having a number for.
 * The ring is lock free for the owner by design, so an uncontended put is meant
 * to be a load and a store, and a row that stops looking like a load and a
 * store is the row that says somebody added an atomic read modify write to the
 * hot path. What contention costs is a different question and it belongs in a
 * benchmark with threads in it, which goes in when the scheduler does. */

static burrow__P rq_p;
static burrow__P rq_victim;
static burrow__G rq_gs[BURROW_RUNQ_SIZE + 1];

static void rq_reset(burrow__P *p) {
    memset(p, 0, sizeof(*p));
    p->status = BURROW_PRUNNING;
}

/* A put and a get back to back on an empty queue, which is one goroutine
 * through the ring and the pair the scheduler runs most often. */
BENCH(runq_put_get) {
    bench_pause(b);
    rq_reset(&rq_p);
    burrow__G *g = &rq_gs[0];
    bench_resume(b);

    BENCH_LOOP(b) {
        burrow__G *overflow = NULL;
        (void)burrow__runq_put(&rq_p, g, false, &overflow);
        bench_keep(burrow__runq_get(&rq_p));
    }
}

/* The same pair through the runnext slot, which is a compare and swap in each
 * direction rather than a store and a compare and swap. This is the path a
 * channel handoff takes, so the difference between this row and the one above
 * is what the slot costs to have. */
BENCH(runq_put_get_next) {
    bench_pause(b);
    rq_reset(&rq_p);
    burrow__G *g = &rq_gs[0];
    bench_resume(b);

    BENCH_LOOP(b) {
        burrow__G *overflow = NULL;
        (void)burrow__runq_put(&rq_p, g, true, &overflow);
        bench_keep(burrow__runq_get(&rq_p));
    }
}

/* Filling the ring and emptying it again, 256 goroutines at a time, so the
 * number is dominated by the loads and stores rather than by the loop around
 * them. Divide by 512 for the cost of moving one goroutine through a queue that
 * is not empty, which is the case the row above cannot show because an empty
 * ring keeps the same cache line hot forever. */
BENCH(runq_fill_drain) {
    bench_pause(b);
    rq_reset(&rq_p);
    bench_resume(b);

    BENCH_LOOP(b) {
        for (int i = 0; i < BURROW_RUNQ_SIZE; i++) {
            burrow__G *overflow = NULL;
            (void)burrow__runq_put(&rq_p, &rq_gs[i], false, &overflow);
        }
        for (int i = 0; i < BURROW_RUNQ_SIZE; i++)
            bench_keep(burrow__runq_get(&rq_p));
    }
}

/* One steal of half a full ring, which is 128 goroutines moved and one handed
 * back to run. Refilling the victim is the expensive part and is paused out, so
 * what is left is the grab: two acquire loads, 128 relaxed loads and stores, and
 * one compare and swap.
 *
 * This is the row that says whether stealing is worth doing in a batch. Divide
 * by 128 and compare against the put and get row: if a stolen goroutine is not
 * a good deal cheaper to move than a locally queued one, the batch is not
 * earning its complexity. */
BENCH(runq_steal_half) {
    bench_pause(b);
    rq_reset(&rq_p);
    rq_reset(&rq_victim);
    bench_resume(b);

    BENCH_LOOP(b) {
        bench_pause(b);
        rq_reset(&rq_p);
        rq_reset(&rq_victim);
        for (int i = 0; i < BURROW_RUNQ_SIZE; i++) {
            burrow__G *overflow = NULL;
            (void)burrow__runq_put(&rq_victim, &rq_gs[i], false, &overflow);
        }
        bench_resume(b);

        bench_keep(burrow__runq_steal(&rq_p, &rq_victim, false));
    }
}

/* The global queue, which is a linked list under a lock everywhere it is used
 * for real. The lock is not here because it is the scheduler's and not the
 * queue's, so this is the list on its own: two pointer writes to push and two
 * to pop.
 *
 * It is on the overflow path rather than the hot one, so this row exists to
 * confirm it stays out of the way rather than to be made faster. */
BENCH(gqueue_push_pop) {
    burrow__GQueue q;
    memset(&q, 0, sizeof(q));

    BENCH_LOOP(b) {
        burrow__gqueue_push(&q, &rq_gs[0]);
        bench_keep(burrow__gqueue_pop(&q));
    }
}

/* Moving half a ring to the global queue in one go, which is what a put onto a
 * full local queue turns into. 129 goroutines land on the batch and the ring is
 * refilled between iterations, which is paused out.
 *
 * Go moves half rather than one because the global queue has a lock on it and
 * the point is to touch that lock as rarely as possible. Dividing this by 129
 * and comparing against the put and get row is how to see whether that trade
 * still pays. */
BENCH(runq_put_slow) {
    bench_pause(b);
    rq_reset(&rq_p);
    bench_resume(b);

    BENCH_LOOP(b) {
        bench_pause(b);
        rq_reset(&rq_p);
        for (int i = 0; i < BURROW_RUNQ_SIZE; i++) {
            burrow__G *overflow = NULL;
            (void)burrow__runq_put(&rq_p, &rq_gs[i], false, &overflow);
        }
        burrow__GQueue batch;
        memset(&batch, 0, sizeof(batch));
        bench_resume(b);

        (void)burrow__runq_put_slow(&rq_p, &rq_gs[BURROW_RUNQ_SIZE], &batch);
        bench_keep(batch.head);
    }
}

/* ------------------------------------------------------------------ tracing
 *
 * What a stack trace costs to collect, which matters because the places that
 * want one are places that cannot afford much: a logger on an error path, a
 * profiler sampling a running program, an allocator recording where a block
 * came from. Printing it is a different cost and is not measured here, since
 * nobody prints one in a loop.
 *
 * Both sides walk from the same depth and ask for the same number of frames, so
 * the rows are comparable straight across. Go's is a frame pointer walk with a
 * lookup per frame into the module data to work out whether the frame was
 * inlined, and burrow's is the walk without the lookup, because burrow has no
 * inline table to consult yet. So this gap is expected to close rather than
 * hold, and the row is here to show by how much when it does.
 *
 * Windows is the row to watch. It does not walk at all, it asks ntdll for the
 * whole stack and then finds the asking frame in the answer, and that is a
 * different order of cost from reading a pair of words per frame. */

#define TR_DEPTH 10
#define TR_MAX 32

static Uintptr tr_pcs[TR_MAX];

static BURROW_NOINLINE Int tr_down(Int depth, Int max) {
    volatile Int n;

    if (depth > 0)
        n = tr_down(depth - 1, max);
    else
        n = runtime_callers(0, slice_from(tr_pcs, max, max, TYPE_UINTPTR));

    /* Through a volatile so that neither the recursion nor the call at the
     * bottom of it can become a jump. A jump would take the frame it reused out
     * of the trace, and the depth is the whole point of the row. */
    return n;
}

/* Ten frames deep and room for all of them, which is the shape a crash reporter
 * or a profiler asks for. */
BENCH(callers_ten) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)tr_down(TR_DEPTH, TR_MAX));
    }
}

/* The same stack with room for two frames, which is the shape a logger asks for
 * when all it wants is the line that called it. The walk stops when the buffer
 * is full, so the difference between this row and the one above is eight
 * frames, and dividing by eight gives the cost of following one link. */
BENCH(callers_two) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)tr_down(TR_DEPTH, 2));
    }
}

void register_runtime_benchmarks(void);

void register_runtime_benchmarks(void) {
    BENCH_RUN(context_switch);
    BENCH_RUN(context_make);
    BENCH_RUN(stack_alloc_free_min);
    BENCH_RUN(stack_alloc_free_large);
    BENCH_RUN(stack_set_current);
    BENCH_RUN(stack_page_size);
    BENCH_RUN(runq_put_get);
    BENCH_RUN(runq_put_get_next);
    BENCH_RUN(runq_fill_drain);
    BENCH_RUN(runq_steal_half);
    BENCH_RUN(runq_put_slow);
    BENCH_RUN(gqueue_push_pop);
    BENCH_RUN(callers_ten);
    BENCH_RUN(callers_two);
}
