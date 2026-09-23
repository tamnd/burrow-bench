/* The harness.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

/* clock_gettime and CLOCK_MONOTONIC are POSIX, not C11, and glibc hides
 * everything that is not C11 when the compiler is asked for strict C11, which
 * -std=c11 is. macOS declares them anyway so this never showed up locally. It
 * has to come before any header, including bench.h, because the first system
 * header pulled in is the one that decides what the rest of them expose.
 *
 * 199309L is the revision that added clock_gettime, and asking for exactly that
 * rather than a later one keeps the request honest about what is being used. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 199309L
#endif

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

const void *bench_hide(const void *p) {
    return p;
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

#define BENCH_MAX 512

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

/* -------------------------------------------------------- repeating a run */

/* The most repetitions that can be asked for. Past this the interesting number
 * is not the spread, it is why the machine is so noisy that more than this many
 * runs were needed. */
#define COUNT_MAX 64

static int compare_double(const void *x, const void *y) {
    double a = *(const double *)x;
    double b = *(const double *)y;
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

/* The median and not the mean, because a single scheduler hiccup in one run out
 * of five moves a mean and does not move a median. The mean of a set with one
 * enormous outlier is a number describing the outlier. */
static double median_of(double *v, int n) {
    qsort(v, (size_t)n, sizeof v[0], compare_double);
    if (n % 2 == 1)
        return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

/* -------------------------------------------------------------------- main */

/* A substring, or the whole name when the filter ends in $, since
 * goroutine_start is a substring of goroutine_start_batch and a profile of one
 * row should not have the other in it. */
static bool name_matches(const char *name, const char *filter) {
    size_t n = strlen(filter);
    if (n > 0 && filter[n - 1] == '$')
        return strlen(name) == n - 1 && strncmp(name, filter, n - 1) == 0;
    return strstr(name, filter) != NULL;
}

static void usage(void) {
    fprintf(stderr, "usage: bench [-run substring or name$] [-time seconds] [-n count] "
                    "[-count runs] [-tsv] [-list]\n");
}

int bench_main(int argc, char **argv) {
    const char *filter = NULL;
    double seconds = 1.0;
    int64_t fixed_n = 0;
    int count = 1;
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
        } else if (strcmp(a, "-count") == 0 && i + 1 < argc) {
            count = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(a, "-tsv") == 0) {
            tsv = true;
        } else if (strcmp(a, "-list") == 0) {
            list = true;
        } else {
            usage();
            return 2;
        }
    }

    if (count < 1)
        count = 1;
    if (count > COUNT_MAX)
        count = COUNT_MAX;

    if (list) {
        for (int i = 0; i < nentries; i++)
            printf("%s\n", entries[i].name);
        return 0;
    }

    int64_t target_ns = (int64_t)(seconds * 1e9);
    if (target_ns < 1000000)
        target_ns = 1000000;

    /* The third column stays ns_per_op so that anything already reading this
     * keeps working. The repetition columns are appended rather than inserted. */
    if (tsv)
        printf("name\titers\tns_per_op\tbytes_per_op\tallocs_per_op\truns\tmin_ns\t"
               "max_ns\tspread_pct\n");

    int ran = 0;
    for (int i = 0; i < nentries; i++) {
        const Entry *e = &entries[i];
        if (filter != NULL && !name_matches(e->name, filter))
            continue;
        ran++;

        /* Worked out once and reused for every repetition, so the repetitions
         * are measuring the same amount of work and are comparable. */
        int64_t n = fixed_n > 0 ? fixed_n : pick_n(e, target_ns);

        double samples[COUNT_MAX];
        Bench last;
        memset(&last, 0, sizeof last);

        for (int r = 0; r < count; r++) {
            last = run_measured(e, n, NULL);
            samples[r] = n > 0 ? (double)last.ns / (double)n : 0.0;
        }

        double bytes_per_op = n > 0 ? (double)last.bytes / (double)n : 0.0;
        double allocs_per_op = n > 0 ? (double)last.allocs / (double)n : 0.0;

        double lo = samples[0];
        double hi = samples[0];
        for (int r = 1; r < count; r++) {
            if (samples[r] < lo)
                lo = samples[r];
            if (samples[r] > hi)
                hi = samples[r];
        }
        double ns_per_op = median_of(samples, count);
        double spread = lo > 0 ? (hi - lo) / lo * 100.0 : 0.0;

        if (tsv) {
            printf("%s\t%lld\t%.4f\t%.2f\t%.3f\t%d\t%.4f\t%.4f\t%.1f\n", e->name,
                   (long long)n, ns_per_op, bytes_per_op, allocs_per_op, count, lo, hi,
                   spread);
        } else {
            printf("%-34s %12lld %12.2f ns/op", e->name, (long long)n, ns_per_op);
            if (last.allocs > 0 || last.bytes > 0)
                printf(" %10.0f B/op %8.2f allocs/op", bytes_per_op, allocs_per_op);
            /* The spread is only printed when it was actually measured. One run
             * has a spread of zero, and printing that would claim a precision
             * nothing supports. */
            if (count > 1)
                printf("  spread %5.1f%% over %d", spread, count);
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
