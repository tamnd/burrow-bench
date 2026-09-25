/* The digests in crypto/md5, crypto/sha1, crypto/sha256, crypto/sha512 and
 * crypto/sha3, on the rows Go's own benchmarks have.
 *
 * Each one hashes an 8 KB buffer and an 8 byte one through the hash.Hash
 * interface, with a reset and a sum every time round, the same work as
 * BenchmarkHash8K and BenchmarkHash8Bytes in Go's tests. The 8 KB row is the
 * per byte cost and the 8 byte row is the per call cost.
 *
 * SHA-1, SHA-256 and SHA-512 pick a hardware path at run time on both sides.
 * GODEBUG=cpu.all=off turns it off for both, so the portable code can be
 * compared too.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/crypto/md5.h"
#include "burrow/crypto/sha1.h"
#include "burrow/crypto/sha256.h"
#include "burrow/crypto/sha3.h"
#include "burrow/crypto/sha512.h"
#include "burrow/hash.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/slice.h"
#include "burrow/type.h"

#include <stdint.h>

static void run_hash(Bench *b, Hash h, Alloc *a, int64_t size) {
    Slice buf = slice_make(a, TYPE_BYTE, size, size);
    Slice sum = slice_make(a, TYPE_BYTE, hash_size(h), hash_size(h));
    BENCH_LOOP(b) {
        hash_reset(h);
        hash_write(h, buf, NULL);
        hash_sum(a, h, slice_sub(sum, 0, 0));
    }
    bench_keep(sum.p);
}

typedef Hash (*MakeHash)(Alloc *a);

static void run(Bench *b, MakeHash mh, int64_t size) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    run_hash(b, mh(a), a, size);
    arena_free(&ar);
}

static Hash sha3_256_hash(Alloc *a) {
    return sha3_as_hash(sha3_new256(a));
}

BENCH(md5_hash8k) {
    run(b, md5_new, 8192);
}

BENCH(md5_hash8bytes) {
    run(b, md5_new, 8);
}

BENCH(sha1_hash8k) {
    run(b, sha1_new, 8192);
}

BENCH(sha1_hash8bytes) {
    run(b, sha1_new, 8);
}

BENCH(sha256_hash8k) {
    run(b, sha256_new, 8192);
}

BENCH(sha256_hash8bytes) {
    run(b, sha256_new, 8);
}

BENCH(sha512_hash8k) {
    run(b, sha512_new, 8192);
}

BENCH(sha512_hash8bytes) {
    run(b, sha512_new, 8);
}

BENCH(sha3_256_hash8k) {
    run(b, sha3_256_hash, 8192);
}

BENCH(sha3_256_hash8bytes) {
    run(b, sha3_256_hash, 8);
}

void register_crypto_benchmarks(void);

void register_crypto_benchmarks(void) {
    BENCH_RUN(md5_hash8k);
    BENCH_RUN(md5_hash8bytes);
    BENCH_RUN(sha1_hash8k);
    BENCH_RUN(sha1_hash8bytes);
    BENCH_RUN(sha256_hash8k);
    BENCH_RUN(sha256_hash8bytes);
    BENCH_RUN(sha512_hash8k);
    BENCH_RUN(sha512_hash8bytes);
    BENCH_RUN(sha3_256_hash8k);
    BENCH_RUN(sha3_256_hash8bytes);
}
