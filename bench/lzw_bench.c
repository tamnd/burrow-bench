/* compress/lzw, on Go's BenchmarkDecoder and BenchmarkEncoder.
 *
 * Both take the digits of e from testdata/e.txt, repeated or cut to 1e4, 1e5
 * and 1e6 bytes, with the GIF bit order and 8 bit literals. Go runs each size
 * twice, once with a new reader or writer every time round and once reusing
 * one through Reset, as sub-benchmarks named 1e4 and 1e-Reuse4. Here they are
 * lzw_decode_1e4 and lzw_decode_reuse_1e4, and the same for encode.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/burrow.h"
#include "burrow/compress/lzw.h"
#include "burrow/mem/heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Byte *read_file(const char *path, Int *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "lzw_bench: cannot open %s; run from the root of burrow-bench\n",
                path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    Byte *p = malloc((size_t)n);
    if (p == NULL || fread(p, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "lzw_bench: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    *len = n;
    return p;
}

#define DIGITS "testdata/e.txt"

/* The file repeated or cut to n bytes, as Go's loop over buf0 builds it. */
static Byte *sized_input(Int n) {
    Int len;
    Byte *src = read_file(DIGITS, &len);
    Byte *out = malloc((size_t)n);
    for (Int i = 0; i < n; i += len) {
        Int k = n - i < len ? n - i : len;
        memcpy(out + i, src, (size_t)k);
    }
    free(src);
    return out;
}

/* Go's getInputBuf: one write per copy of the file into a new writer. */
static BytesBuffer compressed_input(Alloc *heap, Int n) {
    Int len;
    Byte *src = read_file(DIGITS, &len);
    BytesBuffer out = BYTES_BUFFER(heap);
    Error err;
    LzwWriter *w = lzw_new_writer(heap, bytes_buffer_as_io_writer(&out), LZW_LSB, 8);
    if (w == NULL)
        abort();
    for (Int i = 0; i < n; i += len) {
        Int k = n - i < len ? n - i : len;
        lzw_writer_write(w, slice_from(src, k, k, TYPE_BYTE), &err);
    }
    if (BURROW_FAILED(lzw_writer_close(w)))
        abort();
    lzw_writer_free(w);
    free(src);
    return out;
}

static void decode(Bench *b, Int n) {
    Alloc *heap = heap_allocator();
    BytesBuffer c = compressed_input(heap, n);
    Error err;
    int64_t total = 0;
    BENCH_LOOP(b) {
        BytesReader br;
        bytes_reader_reset(&br, bytes_buffer_bytes(&c));
        LzwReader *r = lzw_new_reader(heap, bytes_reader_as_io_reader(&br), LZW_LSB, 8);
        total += io_copy(heap, io_discard, lzw_reader_as_io_reader(r), &err);
        lzw_reader_free(r);
    }
    if (total != b->n * n)
        abort();
    bytes_buffer_free(&c);
}

static void decode_reuse(Bench *b, Int n) {
    Alloc *heap = heap_allocator();
    BytesBuffer c = compressed_input(heap, n);
    Error err;
    BytesReader br;
    bytes_reader_reset(&br, bytes_buffer_bytes(&c));
    LzwReader *r = lzw_new_reader(heap, bytes_reader_as_io_reader(&br), LZW_LSB, 8);
    if (r == NULL)
        abort();
    int64_t total = 0;
    BENCH_LOOP(b) {
        total += io_copy(heap, io_discard, lzw_reader_as_io_reader(r), &err);
        lzw_reader_close(r);
        bytes_reader_reset(&br, bytes_buffer_bytes(&c));
        lzw_reader_reset(r, bytes_reader_as_io_reader(&br), LZW_LSB, 8);
    }
    if (total != b->n * n)
        abort();
    lzw_reader_free(r);
    bytes_buffer_free(&c);
}

static void encode(Bench *b, Int n) {
    Alloc *heap = heap_allocator();
    Byte *in = sized_input(n);
    Slice s = slice_from(in, n, n, TYPE_BYTE);
    Error err;
    BENCH_LOOP(b) {
        LzwWriter *w = lzw_new_writer(heap, io_discard, LZW_LSB, 8);
        lzw_writer_write(w, s, &err);
        lzw_writer_close(w);
        lzw_writer_free(w);
    }
    free(in);
}

static void encode_reuse(Bench *b, Int n) {
    Alloc *heap = heap_allocator();
    Byte *in = sized_input(n);
    Slice s = slice_from(in, n, n, TYPE_BYTE);
    Error err;
    LzwWriter *w = lzw_new_writer(heap, io_discard, LZW_LSB, 8);
    if (w == NULL)
        abort();
    BENCH_LOOP(b) {
        lzw_writer_write(w, s, &err);
        lzw_writer_close(w);
        lzw_writer_reset(w, io_discard, LZW_LSB, 8);
    }
    lzw_writer_free(w);
    free(in);
}

/* Written out one per line so that tools/check-pairs.sh can find them. */
BENCH(lzw_decode_1e4) {
    decode(b, 10000);
}
BENCH(lzw_decode_1e5) {
    decode(b, 100000);
}
BENCH(lzw_decode_1e6) {
    decode(b, 1000000);
}
BENCH(lzw_decode_reuse_1e4) {
    decode_reuse(b, 10000);
}
BENCH(lzw_decode_reuse_1e5) {
    decode_reuse(b, 100000);
}
BENCH(lzw_decode_reuse_1e6) {
    decode_reuse(b, 1000000);
}
BENCH(lzw_encode_1e4) {
    encode(b, 10000);
}
BENCH(lzw_encode_1e5) {
    encode(b, 100000);
}
BENCH(lzw_encode_1e6) {
    encode(b, 1000000);
}
BENCH(lzw_encode_reuse_1e4) {
    encode_reuse(b, 10000);
}
BENCH(lzw_encode_reuse_1e5) {
    encode_reuse(b, 100000);
}
BENCH(lzw_encode_reuse_1e6) {
    encode_reuse(b, 1000000);
}

void register_lzw_benchmarks(void);
void register_lzw_benchmarks(void) {
    BENCH_RUN(lzw_decode_1e4);
    BENCH_RUN(lzw_decode_1e5);
    BENCH_RUN(lzw_decode_1e6);
    BENCH_RUN(lzw_decode_reuse_1e4);
    BENCH_RUN(lzw_decode_reuse_1e5);
    BENCH_RUN(lzw_decode_reuse_1e6);
    BENCH_RUN(lzw_encode_1e4);
    BENCH_RUN(lzw_encode_1e5);
    BENCH_RUN(lzw_encode_1e6);
    BENCH_RUN(lzw_encode_reuse_1e4);
    BENCH_RUN(lzw_encode_reuse_1e5);
    BENCH_RUN(lzw_encode_reuse_1e6);
}
