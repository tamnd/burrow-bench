/* compress/bzip2, on Go's BenchmarkDecodeDigits, BenchmarkDecodeNewton and
 * BenchmarkDecodeRand.
 *
 * Each one reads a .bz2 file from Go's compress/bzip2/testdata, copied into
 * testdata/ here, and decodes it into io_discard every time round with a new
 * reader over a BytesReader, as Go's does. The table is in
 * nanoseconds per decode, so the size does not come into it.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/burrow.h"
#include "burrow/compress/bzip2.h"
#include "burrow/mem/heap.h"

#include <stdio.h>
#include <stdlib.h>

static Byte *read_file(const char *path, Int *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr,
                "bzip2_bench: cannot open %s; run from the root of burrow-bench\n",
                path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    Byte *p = malloc((size_t)n);
    if (p == NULL || fread(p, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "bzip2_bench: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    *len = n;
    return p;
}

static int64_t decode_once(Alloc *heap, Slice c) {
    Error err;
    BytesReader br;
    bytes_reader_reset(&br, c);
    IoReader r = bzip2_new_reader(heap, bytes_reader_as_io_reader(&br));
    if (r.vt == NULL)
        abort();
    int64_t n = io_copy(heap, io_discard, r, &err);
    bzip2_reader_free(r);
    if (BURROW_FAILED(err))
        abort();
    return n;
}

static void decode(Bench *b, const char *path) {
    Int len;
    Byte *file = read_file(path, &len);
    Alloc *heap = heap_allocator();
    Slice c = slice_from(file, len, len, TYPE_BYTE);
    int64_t size = decode_once(heap, c);
    int64_t total = 0;
    BENCH_LOOP(b) {
        total += decode_once(heap, c);
    }
    if (total != b->n * size)
        abort();
    free(file);
}

BENCH(bzip2_decode_digits) {
    decode(b, "testdata/e.txt.bz2");
}
BENCH(bzip2_decode_newton) {
    decode(b, "testdata/Isaac.Newton-Opticks.txt.bz2");
}
BENCH(bzip2_decode_rand) {
    decode(b, "testdata/random.data.bz2");
}

void register_bzip2_benchmarks(void);
void register_bzip2_benchmarks(void) {
    BENCH_RUN(bzip2_decode_digits);
    BENCH_RUN(bzip2_decode_newton);
    BENCH_RUN(bzip2_decode_rand);
}
