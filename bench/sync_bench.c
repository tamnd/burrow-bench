/* The concurrency primitives the scheduler is going to be built on.
 *
 * Three groups. The atomics, which should be level with Go because both sides
 * compile to the same instruction and any gap is a wrapper that failed to
 * inline. Notes used without blocking, which is the fast path a scheduler
 * spends most of its time on. And then the rows where a thread really does go
 * to sleep and really does get woken up.
 *
 * That last group is going to look bad, and it is supposed to. A note ping pong
 * is two trips into the kernel per volley, and a Go channel ping pong is two
 * goroutine switches in user space that never leave the process. The row is not
 * a claim that burrow is slow at handing work between threads, it is the
 * measurement of what goroutines are for. The whole scheduler exists to make
 * sure that almost nothing in a burrow program ever reaches these rows, and
 * once channels are written there will be a pair of rows next to these two that
 * measure the same handoff the way Go does it. Until then this is the honest
 * number and the size of the gap is the size of the prize.
 *
 * The same goes for thread start and join against starting a goroutine. One is
 * a clone syscall and a stack from the kernel, the other is a few hundred bytes
 * from a free list.
 *
 * These use the internal headers, which is why the names carry the burrow
 * double underscore. The public locks are in burrow/sync.h and their rows are
 * in bench/mutex_bench.c, kept separate because the cost of the layer on top is
 * worth knowing on its own. A note blocks a thread and a sync.Mutex parks a
 * goroutine, so the two files are not two measurements of the same thing.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/atomic.h"
#include "burrow/note.h"
#include "burrow/thread.h"

#include <stdint.h>

/* ------------------------------------------------------------------ atomics
 *
 * One thread, no contention, which is the case that matters. A contended
 * counter measures the cache coherence protocol and the machine's core count,
 * and the answer changes on every box, so it belongs in a different file with
 * a different story around it. What these rows check is the wrapper: burrow's
 * atomics are static inline over a compiler builtin and Go's are intrinsics, so
 * a gap here is a call that did not get inlined rather than anything about the
 * hardware. */

static uint64_t sync_counter;
static uint32_t sync_counter32;

BENCH(atomic_add_u64) {
    uint64_t *p = (uint64_t *)bench_hide(&sync_counter);

    BENCH_LOOP(b) {
        bench_keep_u64(burrow__atomic_add_u64(p, 1));
    }
}

BENCH(atomic_add_u32) {
    uint32_t *p = (uint32_t *)bench_hide(&sync_counter32);

    BENCH_LOOP(b) {
        bench_keep_u64(burrow__atomic_add_u32(p, 1));
    }
}

BENCH(atomic_load_u64) {
    uint64_t *p = (uint64_t *)bench_hide(&sync_counter);

    BENCH_LOOP(b) {
        bench_keep_u64(burrow__atomic_load_u64(p));
    }
}

BENCH(atomic_store_u64) {
    uint64_t *p = (uint64_t *)bench_hide(&sync_counter);

    BENCH_LOOP(b) {
        burrow__atomic_store_u64(p, (uint64_t)bench_i_);
    }
}

/* A compare and swap that always succeeds, because a failing one measures the
 * retry loop rather than the instruction. The expected value is refreshed from
 * the counter each time round for the same reason. */
BENCH(atomic_cas_u64) {
    uint64_t *p = (uint64_t *)bench_hide(&sync_counter);
    *p = 0;

    BENCH_LOOP(b) {
        uint64_t want = (uint64_t)bench_i_;
        bench_keep_u64(burrow__atomic_cas_u64(p, &want, want + 1) ? 1u : 0u);
    }
}

/* ------------------------------------------------------------------- notes
 *
 * The uncontended cycle first: close the gate, open it, walk through it, all on
 * one thread with nobody waiting. It is the cheapest thing a note can do and it
 * is the shape a scheduler hits constantly, since most of the time the thread
 * being signalled has not got round to sleeping yet.
 *
 * This row is the reason burrow's notes count their sleepers. It used to make a
 * futex call every time round, because a wake with nobody queued looks the same
 * from inside the kernel as a wake with a crowd, and that cost 352 nanoseconds
 * against 17 for the Go side. Now the wake reads the count, finds nobody, and
 * stays in user space, and the row is a handful of nanoseconds. If it ever
 * climbs back into the hundreds then a system call has crept back into a path
 * that has no business making one. */

static burrow__Note sync_note;

BENCH(note_cycle) {
    if (!burrow__note_init(&sync_note))
        return;

    BENCH_LOOP(b) {
        burrow__note_clear(&sync_note);
        burrow__note_wake(&sync_note);
        burrow__note_sleep(&sync_note);
    }

    burrow__note_free(&sync_note);
}

/* And now the real thing. One volley per iteration, which is two context
 * switches and four system calls in the worst case, and the pair for it on the
 * Go side is an unbuffered channel between two goroutines. */

static burrow__Note sync_to_worker;
static burrow__Note sync_to_main;
static uint32_t sync_stop;
static burrow__Thread sync_ponger;

static void sync_pong(void *arg) {
    (void)arg;
    for (;;) {
        burrow__note_sleep(&sync_to_worker);
        burrow__note_clear(&sync_to_worker);

        bool done = burrow__atomic_load_acquire_u32(&sync_stop) != 0;
        burrow__note_wake(&sync_to_main);
        if (done)
            return;
    }
}

BENCH(note_pingpong) {
    if (!burrow__note_init(&sync_to_worker))
        return;
    if (!burrow__note_init(&sync_to_main)) {
        burrow__note_free(&sync_to_worker);
        return;
    }
    burrow__atomic_store_release_u32(&sync_stop, 0);

    if (!burrow__thread_start(&sync_ponger, sync_pong, NULL, 0)) {
        burrow__note_free(&sync_to_main);
        burrow__note_free(&sync_to_worker);
        return;
    }

    BENCH_LOOP(b) {
        burrow__note_clear(&sync_to_main);
        burrow__note_wake(&sync_to_worker);
        burrow__note_sleep(&sync_to_main);
    }

    /* One more volley to let the worker see the flag and leave. Outside the
     * loop, so it is not in the measurement, and the join cannot come first
     * because the worker is asleep waiting for it. */
    burrow__atomic_store_release_u32(&sync_stop, 1);
    burrow__note_clear(&sync_to_main);
    burrow__note_wake(&sync_to_worker);
    burrow__note_sleep(&sync_to_main);
    (void)burrow__thread_join(&sync_ponger);

    burrow__note_free(&sync_to_main);
    burrow__note_free(&sync_to_worker);
}

/* ----------------------------------------------------------------- threads */

static burrow__Thread sync_worker;
static uint64_t sync_worker_ran;

static void sync_touch(void *arg) {
    (void)arg;
    sync_worker_ran++;
}

/* Start a thread, wait for it, throw it away, and do it again. The Go pair
 * starts a goroutine and waits on a WaitGroup, which is the same program and a
 * completely different amount of work underneath. */
BENCH(thread_start_join) {
    BENCH_LOOP(b) {
        if (!burrow__thread_start(&sync_worker, sync_touch, NULL, 0))
            break;
        (void)burrow__thread_join(&sync_worker);
    }

    bench_keep_u64(sync_worker_ran);
}

/* Giving the processor up when there is nobody else asking for it, which is
 * what a spin loop does between attempts. Go's Gosched goes through the
 * scheduler and burrow's goes through the kernel, so this is another row where
 * the gap is the point. */
BENCH(thread_yield) {
    BENCH_LOOP(b) {
        burrow__thread_yield();
    }
}

BENCH(thread_self) {
    BENCH_LOOP(b) {
        bench_keep_u64(burrow__thread_self());
    }
}

void register_sync_benchmarks(void);

void register_sync_benchmarks(void) {
    BENCH_RUN(atomic_add_u64);
    BENCH_RUN(atomic_add_u32);
    BENCH_RUN(atomic_load_u64);
    BENCH_RUN(atomic_store_u64);
    BENCH_RUN(atomic_cas_u64);
    BENCH_RUN(note_cycle);
    BENCH_RUN(note_pingpong);
    BENCH_RUN(thread_start_join);
    BENCH_RUN(thread_yield);
    BENCH_RUN(thread_self);
}
