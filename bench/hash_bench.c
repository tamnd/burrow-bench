/* The hash, on its own, against the one Go's maps use.
 *
 * Every map operation starts with a hash, so this is the floor under every
 * number in map_bench.c. It gets its own file because the map benchmarks could
 * not say how much of their time was the hash and how much was the table, and
 * the answer turned out to matter: burrow's hash was FNV-1a a byte at a time
 * until the map benchmarks made the case for replacing it.
 *
 * Go's side is hash/maphash, which is the runtime hash the map uses reached
 * through a public door. maphash.String and maphash.Bytes are the runtime's
 * string hash. maphash.Comparable is the runtime's hash for any comparable
 * type, which for an int is the eight byte path. So the pairing is honest, with
 * one caveat: maphash.Comparable goes through generic machinery that a map does
 * not, so the Go int number is a slight overstatement of what Go's map pays.
 * Nothing in reach measures Go's map hash more directly than this.
 *
 * Four lengths, because a hash has a per call cost and a per byte cost and one
 * length cannot separate them. Eight bytes is an int or a pointer key and is
 * all per call cost. Eleven bytes is a short name. Sixty four bytes is a path
 * or a URL. A kilobyte is where the per byte cost is the whole thing and where
 * an AES round per sixteen bytes should win outright.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/type.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enough keys that the loop is not hashing one value out of a register, and
 * few enough that they all stay in L1 and the benchmark measures the hash
 * rather than the memory it reads. A power of two so the index wraps with an
 * and rather than a divide. */
#define KEYS 256
#define KEY_MASK (KEYS - 1)

#define LONG_LEN 64
#define HUGE_LEN 1024

/* One seed for every benchmark here, fixed, because a hash of a fixed seed is
 * what a map does after map_make and because a number that moves every run is
 * not a benchmark. */
#define SEED 0x5eed5eed5eed5eedULL

static Int int_keys[KEYS];
static double float_keys[KEYS];
static char short_bytes[KEYS][16];
static Str short_keys[KEYS];
static char long_bytes[KEYS][LONG_LEN + 1];
static Str long_keys[KEYS];
static char *huge_bytes;
static Str huge_key;

static void build_keys(void) {
    int i, j;

    if (huge_bytes != NULL)
        return;

    for (i = 0; i < KEYS; i++) {
        int_keys[i] = i * 7 + 1;
        float_keys[i] = (double)i * 1.5 + 0.25;

        snprintf(short_bytes[i], sizeof short_bytes[i], "key%08d", i);
        short_keys[i] = (Str){(const Byte *)short_bytes[i], 11};

        /* A common prefix and a varying tail, which is what a hash gets from
         * paths and from keys built out of a namespace.
         *
         * The length is checked rather than assumed. The first version of this
         * wrote a 62 byte path and then told Str it was 64 bytes long, so the C
         * side hashed two bytes the Go side never saw. It is checked on the Go
         * side too, which is what caught it. */
        if (snprintf(long_bytes[i], sizeof long_bytes[i],
                     "/some/reasonably/longer/path/that/a/real/program/would/use/%05d",
                     i) != LONG_LEN) {
            fprintf(stderr,
                    "hash_bench: long key is not the length the Go side uses\n");
            exit(1);
        }
        long_keys[i] = (Str){(const Byte *)long_bytes[i], LONG_LEN};
    }

    huge_bytes = (char *)malloc(HUGE_LEN);
    if (huge_bytes == NULL) {
        fprintf(stderr, "hash_bench: out of memory building the kilobyte key\n");
        exit(1);
    }
    for (j = 0; j < HUGE_LEN; j++)
        huge_bytes[j] = (char)('a' + (j % 26));
    huge_key = (Str){(const Byte *)huge_bytes, HUGE_LEN};
}

BENCH(hash_int) {
    int i = 0;
    build_keys();

    BENCH_LOOP(b) {
        bench_keep_u64(type_hash(TYPE_INT, &int_keys[i], SEED));
        i = (i + 1) & KEY_MASK;
    }
}

BENCH(hash_float64) {
    int i = 0;
    build_keys();

    BENCH_LOOP(b) {
        bench_keep_u64(type_hash(TYPE_FLOAT64, &float_keys[i], SEED));
        i = (i + 1) & KEY_MASK;
    }
}

BENCH(hash_str_short) {
    int i = 0;
    build_keys();

    BENCH_LOOP(b) {
        bench_keep_u64(type_hash(TYPE_STRING, &short_keys[i], SEED));
        i = (i + 1) & KEY_MASK;
    }
}

BENCH(hash_str_long) {
    int i = 0;
    build_keys();

    BENCH_LOOP(b) {
        bench_keep_u64(type_hash(TYPE_STRING, &long_keys[i], SEED));
        i = (i + 1) & KEY_MASK;
    }
}

/* One buffer rather than an array of them, because a kilobyte times two hundred
 * and fifty six would not fit in L2 and this is a benchmark about the hash. */
BENCH(hash_str_huge) {
    build_keys();

    BENCH_LOOP(b) {
        bench_keep_u64(type_hash(TYPE_STRING, &huge_key, SEED));
    }
}

void register_hash_benchmarks(void);

void register_hash_benchmarks(void) {
    BENCH_RUN(hash_int);
    BENCH_RUN(hash_float64);
    BENCH_RUN(hash_str_short);
    BENCH_RUN(hash_str_long);
    BENCH_RUN(hash_str_huge);
}
