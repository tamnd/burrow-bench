/* math/big Int, on the operations that decide how fast a big number program
 * is: add, multiply, square, divide, square root, GCD, modular inverse and
 * exponentiation, conversion to and from decimal, and primality.
 *
 * The suffix is the size of the operands in 64 bit words, so big_mul_1000 is
 * two numbers of 1000 words each, about 19000 decimal digits. The operands are
 * the same on both sides, filled from splitmix64 with a fixed seed, and the
 * receiver is one Int that every iteration writes into, as a Go program would
 * reuse one. Each size is picked to land on one side or the other of a place
 * where the code changes strategy: 10 words is schoolbook multiplication, 100
 * is Karatsuba and 1000 is Karatsuba several levels deep, and the same sizes
 * for division are below and above the recursive division threshold.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/burrow.h"
#include "burrow/math/big.h"
#include "burrow/mem/arena.h"

#include <stdlib.h>

static uint64_t splitmix(uint64_t *s) {
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

/* n words from seed, with the top word's high bit set so the length is n. */
static void rnd(BigInt *z, Int n, uint64_t seed) {
    BigWord *w = malloc((size_t)n * sizeof(BigWord));
    for (Int i = 0; i < n; i++)
        w[i] = (BigWord)splitmix(&seed);
    w[n - 1] |= (BigWord)1 << 63;
    big_int_set_bits(z, slice_from(w, n, n, TYPE_UINT));
    free(w);
}

typedef BigInt *(*BinOp)(BigInt *z, const BigInt *x, const BigInt *y);

static void binop(Bench *b, BinOp op, Int nx, Int ny) {
    BigInt x = {0}, y = {0}, z = {0};
    rnd(&x, nx, 1);
    rnd(&y, ny, 2);
    BENCH_LOOP(b) {
        op(&z, &x, &y);
    }
    bench_keep_u64(big_int_bit_len(&z));
    big_int_free(&x);
    big_int_free(&y);
    big_int_free(&z);
}

BENCH(big_add_10) {
    binop(b, big_int_add, 10, 10);
}
BENCH(big_add_1000) {
    binop(b, big_int_add, 1000, 1000);
}
BENCH(big_mul_10) {
    binop(b, big_int_mul, 10, 10);
}
BENCH(big_mul_100) {
    binop(b, big_int_mul, 100, 100);
}
BENCH(big_mul_1000) {
    binop(b, big_int_mul, 1000, 1000);
}
BENCH(big_quo_10) {
    binop(b, big_int_quo, 20, 10);
}
BENCH(big_quo_100) {
    binop(b, big_int_quo, 200, 100);
}
BENCH(big_quo_1000) {
    binop(b, big_int_quo, 2000, 1000);
}

static BigInt *sqr(BigInt *z, const BigInt *x, const BigInt *y) {
    (void)y;
    return big_int_mul(z, x, x);
}

BENCH(big_sqr_100) {
    binop(b, sqr, 100, 1);
}
BENCH(big_sqr_1000) {
    binop(b, sqr, 1000, 1);
}

static BigInt *sqrt_op(BigInt *z, const BigInt *x, const BigInt *y) {
    (void)y;
    return big_int_sqrt(z, x);
}

BENCH(big_sqrt_100) {
    binop(b, sqrt_op, 100, 1);
}

static BigInt *gcd(BigInt *z, const BigInt *x, const BigInt *y) {
    return big_int_gcd(z, NULL, NULL, x, y);
}

BENCH(big_gcd_10) {
    binop(b, gcd, 10, 10);
}
BENCH(big_gcd_100) {
    binop(b, gcd, 100, 100);
}

static BigInt *mod_inverse(BigInt *z, const BigInt *x, const BigInt *y) {
    return big_int_mod_inverse(z, x, y);
}

/* 2048 bit numbers, the size of an RSA key. */
BENCH(big_mod_inverse_32) {
    binop(b, mod_inverse, 32, 32);
}

BENCH(big_exp_mod_32) {
    BigInt x = {0}, y = {0}, m = {0}, z = {0};
    rnd(&x, 32, 1);
    rnd(&y, 32, 2);
    rnd(&m, 32, 3);
    BENCH_LOOP(b) {
        big_int_exp(&z, &x, &y, &m);
    }
    bench_keep_u64(big_int_bit_len(&z));
    big_int_free(&x);
    big_int_free(&y);
    big_int_free(&m);
    big_int_free(&z);
}

static void to_string(Bench *b, Int n) {
    BigInt x = {0};
    rnd(&x, n, 1);
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Str s = big_int_string(&x, a);
        bench_keep(s.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
    big_int_free(&x);
}

BENCH(big_string_10) {
    to_string(b, 10);
}
BENCH(big_string_100) {
    to_string(b, 100);
}
BENCH(big_string_1000) {
    to_string(b, 1000);
}

static void set_string(Bench *b, Int n) {
    BigInt x = {0}, z = {0};
    rnd(&x, n, 1);
    Arena ar;
    arena_init(&ar, NULL, 0);
    Str s = big_int_string(&x, arena_allocator(&ar));
    BENCH_LOOP(b) {
        bool ok;
        big_int_set_string(&z, s, 10, &ok);
        if (!ok)
            abort();
    }
    arena_free(&ar);
    big_int_free(&x);
    big_int_free(&z);
}

BENCH(big_set_string_10) {
    set_string(b, 10);
}
BENCH(big_set_string_1000) {
    set_string(b, 1000);
}

/* 2⁵²¹ - 1, a Mersenne prime, so every round runs to the end. */
BENCH(big_probably_prime_521) {
    BigInt p = {0};
    big_int_lsh(&p, big_int_set_int64(&p, 1), 521);
    big_int_sub(&p, &p, big_new_int(heap_allocator(), 1));
    BENCH_LOOP(b) {
        if (!big_int_probably_prime(&p, 20))
            abort();
    }
    big_int_free(&p);
}

void register_big_benchmarks(void);
void register_big_benchmarks(void) {
    BENCH_RUN(big_add_10);
    BENCH_RUN(big_add_1000);
    BENCH_RUN(big_mul_10);
    BENCH_RUN(big_mul_100);
    BENCH_RUN(big_mul_1000);
    BENCH_RUN(big_quo_10);
    BENCH_RUN(big_quo_100);
    BENCH_RUN(big_quo_1000);
    BENCH_RUN(big_sqr_100);
    BENCH_RUN(big_sqr_1000);
    BENCH_RUN(big_sqrt_100);
    BENCH_RUN(big_gcd_10);
    BENCH_RUN(big_gcd_100);
    BENCH_RUN(big_mod_inverse_32);
    BENCH_RUN(big_exp_mod_32);
    BENCH_RUN(big_string_10);
    BENCH_RUN(big_string_100);
    BENCH_RUN(big_string_1000);
    BENCH_RUN(big_set_string_10);
    BENCH_RUN(big_set_string_1000);
    BENCH_RUN(big_probably_prime_521);
}
