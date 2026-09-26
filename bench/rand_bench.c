/* math/rand and math/rand/v2, on Go's own benchmarks from both packages.
 *
 * The v2 rows are Go's BenchmarkPCG_DXSM, BenchmarkChaCha8, BenchmarkChaCha8Read
 * and the Rand rows from rand_test.go, which draw from New(NewPCG(1, 2)). The v1
 * rows are the ones from math/rand's rand_test.go on New(NewSource(1)). Each row
 * adds its draws into a total the way Go's do and hands the total to bench_keep
 * once at the end. The bounds go through bench_hide, which is what Go's keep
 * helper is for. Go's v1 rows throw the result away, and so do the C ones,
 * which is safe because every draw changes the source and cannot be dropped. Perm takes its slice from an arena that is reset every time
 * round, where Go allocates.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/func.h"
#include "burrow/math/rand.h"
#include "burrow/math/rand/v2.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"

#include <string.h>

static int64_t hidden_i64(int64_t v) {
    static int64_t slot;
    slot = v;
    return *(const int64_t *)bench_hide(&slot);
}

static Mathrand2Rand *test_rand(void) {
    Alloc *heap = heap_allocator();
    return mathrand2_new(heap, mathrand2_pcg_as_source(mathrand2_new_pcg(heap, 1, 2)));
}

static MathRandRand *test_rand1(void) {
    Alloc *heap = heap_allocator();
    return math_rand_new(heap, math_rand_new_source(heap, 1));
}

/* ---------------------------------------------------------------------- v2 */

BENCH(rand2_pcg_dxsm) {
    Mathrand2PCG *p = mathrand2_new_pcg(heap_allocator(), 0, 0);
    uint64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_pcg_uint64(p);
    }
    bench_keep_u64(t);
}

BENCH(rand2_chacha8) {
    Byte seed[32] = {1, 2, 3, 4, 5};
    Mathrand2ChaCha8 *c = mathrand2_new_cha_cha8(heap_allocator(), seed);
    uint64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_cha_cha8_uint64(c);
    }
    bench_keep_u64(t);
}

BENCH(rand2_chacha8_read) {
    Byte seed[32] = {1, 2, 3, 4, 5};
    Mathrand2ChaCha8 *c = mathrand2_new_cha_cha8(heap_allocator(), seed);
    Byte buf[32];
    Slice s = {buf, 32, 32, NULL};
    uint8_t t = 0;
    BENCH_LOOP(b) {
        mathrand2_cha_cha8_read(c, s, NULL);
        t += buf[0];
    }
    bench_keep_u64(t);
}

BENCH(rand2_source_uint64) {
    Mathrand2Source s =
        mathrand2_pcg_as_source(mathrand2_new_pcg(heap_allocator(), 1, 2));
    uint64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_source_uint64(s);
    }
    bench_keep_u64(t);
}

BENCH(rand2_global_int64) {
    int64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_int64();
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_global_uint64) {
    uint64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_uint64();
    }
    bench_keep_u64(t);
}

BENCH(rand2_int64) {
    Mathrand2Rand *r = test_rand();
    int64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_int64(r);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_uint64) {
    Mathrand2Rand *r = test_rand();
    uint64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_uint64(r);
    }
    bench_keep_u64(t);
}

BENCH(rand2_global_intn1000) {
    Int arg = (Int)hidden_i64(1000);
    Int t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_int_n(arg);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_intn1000) {
    Mathrand2Rand *r = test_rand();
    Int arg = (Int)hidden_i64(1000);
    Int t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_int_n(r, arg);
    }
    bench_keep_u64((uint64_t)t);
}

static void int64n(Bench *b, int64_t n) {
    Mathrand2Rand *r = test_rand();
    int64_t arg = hidden_i64(n);
    int64_t t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_int64_n(r, arg);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_int64n1000) {
    int64n(b, 1000);
}

BENCH(rand2_int64n1e9) {
    int64n(b, 1000000000);
}

BENCH(rand2_int64n1e18) {
    int64n(b, 1000000000000000000);
}

BENCH(rand2_int64n4e18) {
    int64n(b, 4000000000000000000);
}

static void int32n(Bench *b, int32_t n) {
    Mathrand2Rand *r = test_rand();
    int32_t arg = (int32_t)hidden_i64(n);
    int32_t t = 0;
    BENCH_LOOP(b) {
        t = (int32_t)((uint32_t)t + (uint32_t)mathrand2_rand_int32_n(r, arg));
    }
    bench_keep_u64((uint64_t)(int64_t)t);
}

BENCH(rand2_int32n1000) {
    int32n(b, 1000);
}

BENCH(rand2_int32n2e9) {
    int32n(b, 2000000000);
}

BENCH(rand2_float32) {
    Mathrand2Rand *r = test_rand();
    float t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_float32(r);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_float64) {
    Mathrand2Rand *r = test_rand();
    double t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_float64(r);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_exp_float64) {
    Mathrand2Rand *r = test_rand();
    double t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_exp_float64(r);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_norm_float64) {
    Mathrand2Rand *r = test_rand();
    double t = 0;
    BENCH_LOOP(b) {
        t += mathrand2_rand_norm_float64(r);
    }
    bench_keep_u64((uint64_t)t);
}

BENCH(rand2_perm3) {
    Mathrand2Rand *r = test_rand();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Int t = 0;
    BENCH_LOOP(b) {
        Slice p = mathrand2_rand_perm(r, a, 3);
        t += ((Int *)p.p)[0];
        arena_reset(&ar);
    }
    arena_free(&ar);
    bench_keep_u64((uint64_t)t);
}

/* ---------------------------------------------------------------------- v1 */

BENCH(rand_int63_threadsafe) {
    BENCH_LOOP(b) {
        math_rand_int63();
    }
}

BENCH(rand_int63_unthreadsafe) {
    MathRandRand *r = test_rand1();
    BENCH_LOOP(b) {
        math_rand_rand_int63(r);
    }
}

BENCH(rand_intn1000) {
    MathRandRand *r = test_rand1();
    BENCH_LOOP(b) {
        math_rand_rand_intn(r, 1000);
    }
}

BENCH(rand_int63n1000) {
    MathRandRand *r = test_rand1();
    BENCH_LOOP(b) {
        math_rand_rand_int63n(r, 1000);
    }
}

BENCH(rand_int31n1000) {
    MathRandRand *r = test_rand1();
    BENCH_LOOP(b) {
        math_rand_rand_int31n(r, 1000);
    }
}

BENCH(rand_float64) {
    MathRandRand *r = test_rand1();
    BENCH_LOOP(b) {
        math_rand_rand_float64(r);
    }
}

BENCH(rand_perm30) {
    MathRandRand *r = test_rand1();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Slice p = math_rand_rand_perm(r, a, 30);
        bench_keep(p.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

static void swap_check(void *env, Int i, Int j) {
    if (i < 0 || i >= 52 || j < 0 || j >= 52) {
        bench_keep(env);
    }
}

BENCH(rand_shuffle_overhead) {
    MathRandRand *r = test_rand1();
    BENCH_LOOP(b) {
        math_rand_rand_shuffle(r, 52, BURROW_FN(SwapFunc, swap_check, b));
    }
}

static void read_n(Bench *b, Int n) {
    MathRandRand *r = test_rand1();
    Byte buf[1000];
    Slice s = {buf, n, n, NULL};
    BENCH_LOOP(b) {
        math_rand_rand_read(r, s, NULL);
    }
    bench_keep(buf);
}

BENCH(rand_read3) {
    read_n(b, 3);
}

BENCH(rand_read64) {
    read_n(b, 64);
}

BENCH(rand_read1000) {
    read_n(b, 1000);
}

void register_rand_benchmarks(void);

void register_rand_benchmarks(void) {
    BENCH_RUN(rand2_pcg_dxsm);
    BENCH_RUN(rand2_chacha8);
    BENCH_RUN(rand2_chacha8_read);
    BENCH_RUN(rand2_source_uint64);
    BENCH_RUN(rand2_global_int64);
    BENCH_RUN(rand2_global_uint64);
    BENCH_RUN(rand2_int64);
    BENCH_RUN(rand2_uint64);
    BENCH_RUN(rand2_global_intn1000);
    BENCH_RUN(rand2_intn1000);
    BENCH_RUN(rand2_int64n1000);
    BENCH_RUN(rand2_int64n1e9);
    BENCH_RUN(rand2_int64n1e18);
    BENCH_RUN(rand2_int64n4e18);
    BENCH_RUN(rand2_int32n1000);
    BENCH_RUN(rand2_int32n2e9);
    BENCH_RUN(rand2_float32);
    BENCH_RUN(rand2_float64);
    BENCH_RUN(rand2_exp_float64);
    BENCH_RUN(rand2_norm_float64);
    BENCH_RUN(rand2_perm3);
    BENCH_RUN(rand_int63_threadsafe);
    BENCH_RUN(rand_int63_unthreadsafe);
    BENCH_RUN(rand_intn1000);
    BENCH_RUN(rand_int63n1000);
    BENCH_RUN(rand_int31n1000);
    BENCH_RUN(rand_float64);
    BENCH_RUN(rand_perm30);
    BENCH_RUN(rand_shuffle_overhead);
    BENCH_RUN(rand_read3);
    BENCH_RUN(rand_read64);
    BENCH_RUN(rand_read1000);
}
