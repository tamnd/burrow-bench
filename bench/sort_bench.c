/* sort, on the rows Go's own sort benchmarks have.
 *
 * The inputs are Go's: 1024 ints set to i ^ 0x2cc, the same sorted, reversed
 * and mod 8, 65536 ints set to i ^ 0xcccc, and 1024 strings that are the
 * decimal form of i ^ 0x2cc. The copy or refill that resets the input runs
 * with the clock paused, as Go's does with StopTimer.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/func.h"
#include "burrow/slice.h"
#include "burrow/sort.h"
#include "burrow/type.h"

#include <stdio.h>
#include <string.h>

enum { N1K = 1 << 10, N64K = 1 << 16 };

static Int ints[N64K];
static Int unsorted_ints[N1K];
static Str strs[N1K];
static Str unsorted_strs[N1K];
static char str_bytes[N1K][8];

typedef enum Fill { XOR, SORTED, REVERSED, MOD8 } Fill;

static void fill(Int *data, Int n, Fill f) {
    for (Int i = 0; i < n; i++) {
        switch (f) {
        case XOR:
            data[i] = i ^ (n == N1K ? 0x2cc : 0xcccc);
            break;
        case SORTED:
            data[i] = i;
            break;
        case REVERSED:
            data[i] = n - i;
            break;
        case MOD8:
            data[i] = i % 8;
            break;
        }
    }
}

static void run_ints(Bench *b, Int n, Fill f) {
    Slice s = slice_from(ints, n, n, TYPE_INT);
    bench_pause(b);
    BENCH_LOOP(b) {
        fill(ints, n, f);
        bench_resume(b);
        sort_ints(s);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(ints);
}

static bool int_less(void *env, Int i, Int j) {
    const Int *d = env;
    return d[i] < d[j];
}

static bool str_less(void *env, Int i, Int j) {
    const Str *d = env;
    return str_cmp(d[i], d[j]) < 0;
}

static void init_strs(void) {
    for (Int i = 0; i < N1K; i++) {
        int k = snprintf(str_bytes[i], sizeof str_bytes[i], "%lld", (long long)(i ^ 0x2cc));
        unsorted_strs[i] = (Str){(const Byte *)str_bytes[i], k};
    }
}

BENCH(sort_string1k) {
    init_strs();
    Slice s = slice_from(strs, N1K, N1K, TYPE_STRING);
    bench_pause(b);
    BENCH_LOOP(b) {
        memcpy(strs, unsorted_strs, sizeof strs);
        bench_resume(b);
        sort_strings(s);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(strs);
}

BENCH(sort_string1k_slice) {
    init_strs();
    Slice s = slice_from(strs, N1K, N1K, TYPE_STRING);
    SortLessFunc less = BURROW_FN(SortLessFunc, str_less, strs);
    bench_pause(b);
    BENCH_LOOP(b) {
        memcpy(strs, unsorted_strs, sizeof strs);
        bench_resume(b);
        sort_slice(s, less);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(strs);
}

BENCH(sort_stable_string1k) {
    init_strs();
    SortStringSlice s = slice_from(strs, N1K, N1K, TYPE_STRING);
    SortInterface data = sort_string_slice_as_sort_interface(&s);
    bench_pause(b);
    BENCH_LOOP(b) {
        memcpy(strs, unsorted_strs, sizeof strs);
        bench_resume(b);
        sort_stable(data);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(strs);
}

BENCH(sort_int1k) {
    run_ints(b, N1K, XOR);
}

BENCH(sort_int1k_sorted) {
    run_ints(b, N1K, SORTED);
}

BENCH(sort_int1k_reversed) {
    run_ints(b, N1K, REVERSED);
}

BENCH(sort_int1k_mod8) {
    run_ints(b, N1K, MOD8);
}

BENCH(sort_stable_int1k) {
    fill(unsorted_ints, N1K, XOR);
    SortIntSlice s = slice_from(ints, N1K, N1K, TYPE_INT);
    SortInterface data = sort_int_slice_as_sort_interface(&s);
    bench_pause(b);
    BENCH_LOOP(b) {
        memcpy(ints, unsorted_ints, sizeof unsorted_ints);
        bench_resume(b);
        sort_stable(data);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(ints);
}

BENCH(sort_stable_int1k_slice) {
    fill(unsorted_ints, N1K, XOR);
    Slice s = slice_from(ints, N1K, N1K, TYPE_INT);
    SortLessFunc less = BURROW_FN(SortLessFunc, int_less, ints);
    bench_pause(b);
    BENCH_LOOP(b) {
        memcpy(ints, unsorted_ints, sizeof unsorted_ints);
        bench_resume(b);
        sort_slice_stable(s, less);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(ints);
}

BENCH(sort_int64k) {
    run_ints(b, N64K, XOR);
}

BENCH(sort_int64k_slice) {
    Slice s = slice_from(ints, N64K, N64K, TYPE_INT);
    SortLessFunc less = BURROW_FN(SortLessFunc, int_less, ints);
    bench_pause(b);
    BENCH_LOOP(b) {
        fill(ints, N64K, XOR);
        bench_resume(b);
        sort_slice(s, less);
        bench_pause(b);
    }
    bench_resume(b);
    bench_keep(ints);
}

void register_sort_benchmarks(void);

void register_sort_benchmarks(void) {
    BENCH_RUN(sort_string1k);
    BENCH_RUN(sort_string1k_slice);
    BENCH_RUN(sort_stable_string1k);
    BENCH_RUN(sort_int1k);
    BENCH_RUN(sort_int1k_sorted);
    BENCH_RUN(sort_int1k_reversed);
    BENCH_RUN(sort_int1k_mod8);
    BENCH_RUN(sort_stable_int1k);
    BENCH_RUN(sort_stable_int1k_slice);
    BENCH_RUN(sort_int64k);
    BENCH_RUN(sort_int64k_slice);
}
