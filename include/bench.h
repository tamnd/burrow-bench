/* A benchmark harness shaped like Go's testing.B.
 *
 * It is shaped that way on purpose. burrow ports testing eventually, and when
 * it does these benchmarks should survive with a search and replace instead of
 * a rewrite. Until then this is a few hundred lines with no dependencies beyond
 * the C standard library and one clock call per platform.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#ifndef BURROW_BENCH_H
#define BURROW_BENCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Bench {
    const char *name;

    /* How many times the body runs. Set by the harness, read by your loop, and
     * the same field Go calls b.N. */
    int64_t n;

    /* Filled in by the harness. Nanoseconds for the whole timed run, then the
     * two allocation counters if the benchmark reported them. */
    int64_t ns;
    uint64_t bytes;
    uint64_t allocs;

    /* Internal. The accumulated timed nanoseconds and the point the clock was
     * last started, so that pause and resume can bracket untimed setup. */
    int64_t accum_ns;
    int64_t started_ns;
    bool running;
} Bench;

typedef void (*BenchFunc)(Bench *b);

/* Declare one. The body runs many times, so anything it allocates it has to
 * release, or the benchmark measures the machine running out of memory.
 *
 *     BENCH(arena_alloc) {
 *         Arena ar;
 *         arena_init(&ar, NULL, 0);
 *         BENCH_LOOP(b) { ... }
 *         arena_free(&ar);
 *     }
 */
#define BENCH(name)                                                                    \
    static void bench_##name(Bench *b);                                                \
    static const char bench_name_##name[] = #name;                                     \
    static void bench_##name(Bench *b)

/* The timed part. Everything outside it is setup and is not counted. */
#define BENCH_LOOP(b) for (int64_t bench_i_ = 0; bench_i_ < (b)->n; bench_i_++)

/* Register one with the runner. */
#define BENCH_RUN(name) bench_register(bench_name_##name, bench_##name)

void bench_register(const char *name, BenchFunc fn);

/* Everything a benchmark computed has to go somewhere the compiler cannot
 * prove is dead, or the loop it was in gets deleted and the benchmark reports
 * a number about nothing. This is the somewhere.
 *
 * It is a real call into another translation unit rather than a volatile store,
 * because a volatile store is a store and shows up in the measurement, and
 * because link time optimisation can see through the cheaper tricks. */
void bench_keep(const void *p);
void bench_keep_u64(uint64_t v);

/* Stop and start the clock around setup that has to happen inside the loop.
 * Same idea as Go's b.StopTimer and b.StartTimer, and the same warning: if you
 * are pausing more often than you are measuring, the pause is the benchmark. */
void bench_pause(Bench *b);
void bench_resume(Bench *b);

/* Report bytes and allocations per operation next to the time, which is Go's
 * three column output and the part of an arena worth knowing about. Call it
 * once, after the loop, with the totals for the whole run. */
void bench_report_allocs(Bench *b, uint64_t bytes, uint64_t allocs);

/* Runs everything registered and prints the results. Hand it argc and argv.
 *
 *   -run <substring>   only benchmarks whose name contains this
 *   -time <seconds>    how long each one should take, default 1
 *   -n <count>         fix the iteration count instead of working it out
 *   -count <runs>      repeat each one this many times and report the spread
 *   -tsv               machine readable output, one row per benchmark
 *   -list              print the names and exit
 *
 * Use -count. A single run gives one number and says nothing about how much to
 * trust it, and on a machine that is doing anything else at all the answer is
 * usually not much. Three runs of the same benchmark on a lightly loaded server
 * came back 9.3, 15.1 and 22.8 nanoseconds, which is not a measurement, it is
 * three measurements of the scheduler. With -count the spread is printed next
 * to the number and that stops happening quietly.
 */
int bench_main(int argc, char **argv);

/* Monotonic nanoseconds. Exposed because a few benchmarks need to time
 * something the loop cannot express. */
int64_t bench_now_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* BURROW_BENCH_H */
