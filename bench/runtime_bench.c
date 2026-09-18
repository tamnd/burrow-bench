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
#include "burrow/stack.h"

#include <stdint.h>

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

void register_runtime_benchmarks(void);

void register_runtime_benchmarks(void) {
    BENCH_RUN(context_switch);
    BENCH_RUN(context_make);
    BENCH_RUN(stack_alloc_free_min);
    BENCH_RUN(stack_alloc_free_large);
    BENCH_RUN(stack_set_current);
    BENCH_RUN(stack_page_size);
}
