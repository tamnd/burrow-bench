/* The harness.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* ------------------------------------------------------------------ clock */

int64_t bench_now_ns(void) {
#if defined(_WIN32)
    /* The frequency is fixed for the life of the process, so ask once. */
    static LARGE_INTEGER freq;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    /* Split the division so that the multiply cannot overflow on a machine
     * that has been up for a while. */
    int64_t whole = t.QuadPart / freq.QuadPart;
    int64_t part = t.QuadPart % freq.QuadPart;
    return whole * 1000000000 + (part * 1000000000) / freq.QuadPart;
#else
    struct timespec ts;
    /* CLOCK_MONOTONIC and not CLOCK_REALTIME, because a benchmark that runs
     * across an NTP step should not report a negative duration. */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
#endif
}

/* ------------------------------------------------------------------- sink */

/* Deliberately not static and deliberately in this file rather than a header,
 * so that a compiler without link time optimisation cannot see what happens to
 * the value and has to keep the work that produced it. */
volatile const void *bench_sink_ptr;
volatile uint64_t bench_sink_u64;

void bench_keep(const void *p) {
    bench_sink_ptr = p;
}

void bench_keep_u64(uint64_t v) {
    bench_sink_u64 = v;
}

/* ------------------------------------------------------------- the timer */

void bench_pause(Bench *b) {
    if (!b->running)
        return;
    b->accum_ns += bench_now_ns() - b->started_ns;
    b->running = false;
}

void bench_resume(Bench *b) {
    if (b->running)
        return;
    b->started_ns = bench_now_ns();
    b->running = true;
}

void bench_report_allocs(Bench *b, uint64_t bytes, uint64_t allocs) {
    b->bytes = bytes;
    b->allocs = allocs;
}

/* ---------------------------------------------------------------- registry */

#define BENCH_MAX 256

typedef struct Entry {
    const char *name;
    BenchFunc fn;
} Entry;

static Entry entries[BENCH_MAX];
static int nentries;

void bench_register(const char *name, BenchFunc fn) {
    if (nentries >= BENCH_MAX) {
        fprintf(stderr, "bench: more than %d benchmarks, raise BENCH_MAX\n", BENCH_MAX);
        exit(1);
    }
    entries[nentries].name = name;
    entries[nentries].fn = fn;
    nentries++;
}

/* ----------------------------------------------------------------- running */

/* Go stops at a billion iterations no matter what, and so do we. Past that
 * point the benchmark is not too fast to measure, it is broken. */
#define N_MAX 1000000000

static Bench run_measured(const Entry *e, int64_t n, int64_t *wall_ns) {
    Bench b;
    memset(&b, 0, sizeof b);
    b.name = e->name;
    b.n = n;

    int64_t start = bench_now_ns();
    bench_resume(&b);
    e->fn(&b);
    bench_pause(&b);
    if (wall_ns != NULL)
        *wall_ns = bench_now_ns() - start;

    b.ns = b.accum_ns;
    return b;
}

/* Work out an iteration count that makes the measurement mean something, the
 * way Go does: run a small number, see how long it took, scale up, repeat.
 *
 * The scale up is capped at a hundred times per round because a first run that
 * was almost too fast to time can otherwise produce an enormous jump, and the
 * whole point is to land near the target rather than overshoot it by a minute.
 *
 * Two clocks are watched, and the second one is not an optimisation, it is what
 * keeps the runner from hanging. A benchmark that pauses the clock around setup
 * inside the loop can spend a thousand times longer setting up than it spends
 * being measured, so scaling until the measured time reaches the target means
 * scaling until the machine gives up. Whichever clock reaches the target first
 * ends the search. */
static int64_t pick_n(const Entry *e, int64_t target_ns) {
    int64_t n = 1;
    for (int round = 0; round < 30; round++) {
        int64_t wall = 0;
        Bench b = run_measured(e, n, &wall);
        int64_t ns = b.ns;

        if (ns >= target_ns || wall >= target_ns || n >= N_MAX)
            return n;

        int64_t next;
        if (ns <= 0) {
            /* Too fast to measure at all at this count. Jump hard. */
            next = n * 100;
        } else {
            next = n * target_ns / ns;
            /* A little over, because landing just short means another round. */
            next += next / 5;
            if (next > n * 100)
                next = n * 100;
            if (next <= n)
                next = n + 1;
        }

        /* If the untimed part dominates, the estimate above is about the timed
         * part only and will overshoot the wall clock by that ratio. Scale it
         * back by what the wall clock actually did. */
        if (wall > 0 && wall > ns) {
            int64_t by_wall = n * target_ns / wall;
            by_wall += by_wall / 5;
            if (by_wall < next)
                next = by_wall;
            if (next <= n)
                next = n + 1;
        }

        n = next;
        if (n > N_MAX)
            return N_MAX;
    }
    return n;
}

/* -------------------------------------------------------------------- main */

static void usage(void) {
    fprintf(stderr, "usage: bench [-run substring] [-time seconds] [-n count] [-tsv] "
                    "[-list]\n");
}

int bench_main(int argc, char **argv) {
    const char *filter = NULL;
    double seconds = 1.0;
    int64_t fixed_n = 0;
    bool tsv = false;
    bool list = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-run") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (strcmp(a, "-time") == 0 && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else if (strcmp(a, "-n") == 0 && i + 1 < argc) {
            fixed_n = strtoll(argv[++i], NULL, 10);
        } else if (strcmp(a, "-tsv") == 0) {
            tsv = true;
        } else if (strcmp(a, "-list") == 0) {
            list = true;
        } else {
            usage();
            return 2;
        }
    }

    if (list) {
        for (int i = 0; i < nentries; i++)
            printf("%s\n", entries[i].name);
        return 0;
    }

    int64_t target_ns = (int64_t)(seconds * 1e9);
    if (target_ns < 1000000)
        target_ns = 1000000;

    if (tsv)
        printf("name\titers\tns_per_op\tbytes_per_op\tallocs_per_op\n");

    int ran = 0;
    for (int i = 0; i < nentries; i++) {
        const Entry *e = &entries[i];
        if (filter != NULL && strstr(e->name, filter) == NULL)
            continue;
        ran++;

        int64_t n = fixed_n > 0 ? fixed_n : pick_n(e, target_ns);
        Bench b = run_measured(e, n, NULL);

        double ns_per_op = n > 0 ? (double)b.ns / (double)n : 0.0;
        double bytes_per_op = n > 0 ? (double)b.bytes / (double)n : 0.0;
        double allocs_per_op = n > 0 ? (double)b.allocs / (double)n : 0.0;

        if (tsv) {
            printf("%s\t%lld\t%.4f\t%.2f\t%.3f\n", e->name, (long long)n, ns_per_op,
                   bytes_per_op, allocs_per_op);
        } else {
            printf("%-34s %12lld %12.2f ns/op", e->name, (long long)n, ns_per_op);
            if (b.allocs > 0 || b.bytes > 0)
                printf(" %10.0f B/op %8.2f allocs/op", bytes_per_op, allocs_per_op);
            printf("\n");
        }
        fflush(stdout);
    }

    if (ran == 0) {
        fprintf(stderr, "bench: nothing matched %s\n", filter ? filter : "(no filter)");
        return 1;
    }
    return 0;
}
