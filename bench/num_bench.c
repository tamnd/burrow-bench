/* What Go's arithmetic rules cost when they are written out in C.
 *
 * burrow/num.h exists because Go defines what happens on overflow, on a shift
 * past the width of a type and on a divide by zero, and C does not. Writing
 * those out costs something, and the whole point of these rows is to say how
 * much rather than to claim it is free.
 *
 * Read them in pairs. num_add against num_add_raw is a wrapping add against the
 * plain operator, and the two should be identical, because two's complement
 * wrapping is what the hardware was going to do anyway and the only thing the
 * function adds is a cast the compiler throws away. Same for the multiply.
 * num_div against num_div_raw is the one with a real number in it, because
 * int64_div checks for a zero divisor and for the MinInt over minus one case
 * that faults on x86, and that is two compares in front of the divide.
 *
 * The Go column is the same expression written in Go, where all of this is the
 * language's job. A row where burrow is level with Go is a row where the port
 * costs nothing, and that is most of them.
 *
 * Every operand goes through bench_hide first. An arithmetic benchmark is the
 * easiest thing in this repository to measure wrongly, because a compiler that
 * can see both operands will compute the answer at compile time and leave an
 * empty loop behind. The loop counter is one operand on most rows for the same
 * reason, since it changes every iteration and cannot be hoisted.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/num.h"

/* Hands back a value the optimiser has to treat as unknown. Taking the address
 * of a static and reading it back through bench_hide is the same trick the
 * interface benchmarks use, and it is needed here for every constant that would
 * otherwise get folded into the loop. */
static Int hidden_int(Int v) {
    static Int slot;
    slot = v;
    return *(const Int *)bench_hide(&slot);
}

static double hidden_double(double v) {
    static double slot;
    slot = v;
    return *(const double *)bench_hide(&slot);
}

/* ------------------------------------------------------------ the arithmetic
 *
 * Wrapping add and multiply against the plain operator. These pairs are the
 * claim that the header is free on the operations that cannot fail, and they
 * are the first thing to check after a compiler upgrade. */

BENCH(num_add) {
    Int a = hidden_int(1234567);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_add(a, (Int)bench_i_));
    }
}

BENCH(num_add_raw) {
    Int a = hidden_int(1234567);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(a + (Int)bench_i_));
    }
}

BENCH(num_mul) {
    Int a = hidden_int(2654435761);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_mul(a, (Int)bench_i_));
    }
}

BENCH(num_mul_raw) {
    Int a = hidden_int(2654435761);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(a * (Int)bench_i_));
    }
}

/* The generic macro picks a function by the type of its first argument, and the
 * selection happens at compile time, so this row exists to show that _Generic
 * costs nothing at run time rather than because anybody doubted it. */
BENCH(num_add_generic) {
    Int a = hidden_int(1234567);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)BURROW_ADD(a, (Int)bench_i_));
    }
}

/* --------------------------------------------------------------- the divides
 *
 * The divisor is hidden, because a division by a constant is not a division:
 * both compilers turn it into a multiply and a shift, and the row would be
 * measuring that instead. */

BENCH(num_div) {
    Int d = hidden_int(7);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_div((Int)bench_i_, d));
    }
}

BENCH(num_div_raw) {
    Int d = hidden_int(7);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)((Int)bench_i_ / d));
    }
}

BENCH(num_mod) {
    Int d = hidden_int(7);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_mod((Int)bench_i_, d));
    }
}

BENCH(num_mod_raw) {
    Int d = hidden_int(7);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)((Int)bench_i_ % d));
    }
}

/* ---------------------------------------------------------------- the shifts
 *
 * The count varies per iteration and goes past the width of the type, which is
 * the case the header exists for. The raw row is undefined behaviour for those
 * counts and is here as a speed comparison only: on x86 it answers with the low
 * six bits of the count, which is a different number, not a faster one. It is
 * masked so that a sanitised build of this file stays quiet. */

BENCH(num_shl) {
    Int x = hidden_int(1);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_shl(x, (Int)(bench_i_ & 127)));
    }
}

BENCH(num_shl_raw) {
    Int x = hidden_int(1);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(x << (bench_i_ & 63)));
    }
}

BENCH(num_shr) {
    Int x = hidden_int(-123456789);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_shr(x, (Int)(bench_i_ & 127)));
    }
}

/* ------------------------------------------------------------ float to int
 *
 * Four compares and a select around one convert instruction, because Go's
 * answer for a value that does not fit is not the hardware's answer everywhere.
 * The raw row is the bare cast, which is undefined in C once the value is out
 * of range, and the input here stays in range so that the two rows differ only
 * by the checks. */

BENCH(num_from_float64) {
    double f = hidden_double(1234.5);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)int_from_float64(f + (double)bench_i_));
    }
}

BENCH(num_from_float64_raw) {
    double f = hidden_double(1234.5);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(Int)(f + (double)bench_i_));
    }
}

/* ------------------------------------------------------- the realistic shape
 *
 * A hash round, which is where the wrapping functions actually get used. Every
 * ported hash in the library is a multiply and an xor per byte, and every one
 * of those multiplies overflows on almost every input, so this is the row that
 * says what the header costs in the place it is unavoidable.
 *
 * It is written with the signed functions on purpose. The real hashes use
 * unsigned arithmetic, where C already wraps and the functions are the plain
 * operator, so a signed version is the expensive case rather than the flattering
 * one. */

enum { HASH_BYTES = 64 };

BENCH(num_hash_round) {
    static uint8_t buf[HASH_BYTES];
    Int prime = hidden_int(1099511628211);
    Int i;

    for (i = 0; i < (Int)HASH_BYTES; i++)
        buf[i] = (uint8_t)(i * 7 + 1);

    BENCH_LOOP(b) {
        Int h = (Int)14695981039346656037ULL;
        Int j;
        for (j = 0; j < (Int)HASH_BYTES; j++) {
            h = int_mul(h, prime);
            h ^= (Int)buf[j];
        }
        bench_keep_u64((uint64_t)h);
    }
}

/* Summing a slice, which is the other shape: an accumulator that a long enough
 * input will overflow, in a loop the compiler would like to vectorise. Read
 * this against num_sum_raw rather than against Go, since whether either side
 * vectorises is a compiler decision and the point of the pair is that the
 * function does not stop it happening. */

enum { SUM_LEN = 256 };

BENCH(num_sum) {
    static Int xs[SUM_LEN];
    Int i;

    for (i = 0; i < (Int)SUM_LEN; i++)
        xs[i] = i * 3;

    BENCH_LOOP(b) {
        Int total = 0;
        Int j;
        for (j = 0; j < (Int)SUM_LEN; j++)
            total = int_add(total, xs[j]);
        bench_keep_u64((uint64_t)total);
    }
}

BENCH(num_sum_raw) {
    static Int xs[SUM_LEN];
    Int i;

    for (i = 0; i < (Int)SUM_LEN; i++)
        xs[i] = i * 3;

    BENCH_LOOP(b) {
        Int total = 0;
        Int j;
        for (j = 0; j < (Int)SUM_LEN; j++)
            total += xs[j];
        bench_keep_u64((uint64_t)total);
    }
}

void register_num_benchmarks(void);

void register_num_benchmarks(void) {
    BENCH_RUN(num_add);
    BENCH_RUN(num_add_raw);
    BENCH_RUN(num_add_generic);
    BENCH_RUN(num_mul);
    BENCH_RUN(num_mul_raw);
    BENCH_RUN(num_div);
    BENCH_RUN(num_div_raw);
    BENCH_RUN(num_mod);
    BENCH_RUN(num_mod_raw);
    BENCH_RUN(num_shl);
    BENCH_RUN(num_shl_raw);
    BENCH_RUN(num_shr);
    BENCH_RUN(num_from_float64);
    BENCH_RUN(num_from_float64_raw);
    BENCH_RUN(num_hash_round);
    BENCH_RUN(num_sum);
    BENCH_RUN(num_sum_raw);
}
