/* compress/flate, on Go's BenchmarkEncode and BenchmarkDecode.
 *
 * Go runs both over two files, the digits of e and Newton's Opticks, at four
 * levels and three sizes, as sub-benchmarks named like Digits/Speed/1e5. Each
 * one is a row of its own here, flate_encode_digits_speed_1e5, and
 * go/flate_test.go has the Go side split the same way. The input is the file
 * repeated or cut to the size, as in Go. Encode reuses one writer, resetting
 * it onto io_discard every time round. Decode makes a new reader every time
 * round, as Go's does with NewReader, and copies it to io_discard.
 *
 * Both sides read testdata/ from the root of this repo, which has the same
 * two files as Go's source tree.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/burrow.h"
#include "burrow/compress/flate.h"
#include "burrow/mem/heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Byte *read_file(const char *path, Int *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr,
                "flate_bench: cannot open %s; run from the root of burrow-bench\n",
                path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    Byte *p = malloc((size_t)n);
    if (p == NULL || fread(p, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "flate_bench: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    *len = n;
    return p;
}

/* The file repeated or cut to n bytes, which is what Go's loop over buf0
 * builds. */
static Byte *sized_input(const char *path, Int n) {
    Int len;
    Byte *src = read_file(path, &len);
    Byte *out = malloc((size_t)n);
    for (Int i = 0; i < n; i += len) {
        Int k = n - i < len ? n - i : len;
        memcpy(out + i, src, (size_t)k);
    }
    free(src);
    return out;
}

static void encode(Bench *b, const char *path, int level, Int n) {
    Byte *in = sized_input(path, n);
    Error err;
    FlateWriter *w = flate_new_writer(heap_allocator(), io_discard, level, &err);
    if (w == NULL)
        abort();
    Slice s = slice_from(in, n, n, TYPE_BYTE);
    BENCH_LOOP(b) {
        flate_writer_reset(w, io_discard);
        flate_writer_write(w, s, &err);
        flate_writer_close(w);
    }
    flate_writer_free(w);
    free(in);
}

static void decode(Bench *b, const char *path, int level, Int n) {
    Int file_len;
    Byte *file = read_file(path, &file_len);
    Byte *in = sized_input(path, n);
    Alloc *heap = heap_allocator();
    BytesBuffer compressed = BYTES_BUFFER(heap);
    Error err;
    FlateWriter *w =
        flate_new_writer(heap, bytes_buffer_as_io_writer(&compressed), level, &err);
    if (w == NULL)
        abort();
    /* A write per copy of the file, as Go's io.Copy per copy does. */
    for (Int i = 0; i < n; i += file_len) {
        Int k = n - i < file_len ? n - i : file_len;
        flate_writer_write(w, slice_from(in + i, k, k, TYPE_BYTE), &err);
    }
    flate_writer_close(w);
    flate_writer_free(w);
    Slice c = bytes_buffer_bytes(&compressed);
    int64_t total = 0;
    BENCH_LOOP(b) {
        BytesReader br;
        bytes_reader_reset(&br, c);
        IoReadCloser rc = flate_new_reader(heap, bytes_reader_as_io_reader(&br));
        total += io_copy(heap, io_discard, io_read_closer_as_io_reader(rc), &err);
        flate_reader_free(rc);
    }
    if (total != b->n * n)
        abort();
    bytes_buffer_free(&compressed);
    free(in);
    free(file);
}

#define DIGITS "testdata/e.txt"
#define NEWTON "testdata/Isaac.Newton-Opticks.txt"

/* Written out one per line so that tools/check-pairs.sh can find them. */
BENCH(flate_encode_digits_huffman_1e4) {
    encode(b, DIGITS, -2, 10000);
}
BENCH(flate_encode_digits_huffman_1e5) {
    encode(b, DIGITS, -2, 100000);
}
BENCH(flate_encode_digits_huffman_1e6) {
    encode(b, DIGITS, -2, 1000000);
}
BENCH(flate_encode_digits_speed_1e4) {
    encode(b, DIGITS, 1, 10000);
}
BENCH(flate_encode_digits_speed_1e5) {
    encode(b, DIGITS, 1, 100000);
}
BENCH(flate_encode_digits_speed_1e6) {
    encode(b, DIGITS, 1, 1000000);
}
BENCH(flate_encode_digits_default_1e4) {
    encode(b, DIGITS, -1, 10000);
}
BENCH(flate_encode_digits_default_1e5) {
    encode(b, DIGITS, -1, 100000);
}
BENCH(flate_encode_digits_default_1e6) {
    encode(b, DIGITS, -1, 1000000);
}
BENCH(flate_encode_digits_compression_1e4) {
    encode(b, DIGITS, 9, 10000);
}
BENCH(flate_encode_digits_compression_1e5) {
    encode(b, DIGITS, 9, 100000);
}
BENCH(flate_encode_digits_compression_1e6) {
    encode(b, DIGITS, 9, 1000000);
}
BENCH(flate_encode_newton_huffman_1e4) {
    encode(b, NEWTON, -2, 10000);
}
BENCH(flate_encode_newton_huffman_1e5) {
    encode(b, NEWTON, -2, 100000);
}
BENCH(flate_encode_newton_huffman_1e6) {
    encode(b, NEWTON, -2, 1000000);
}
BENCH(flate_encode_newton_speed_1e4) {
    encode(b, NEWTON, 1, 10000);
}
BENCH(flate_encode_newton_speed_1e5) {
    encode(b, NEWTON, 1, 100000);
}
BENCH(flate_encode_newton_speed_1e6) {
    encode(b, NEWTON, 1, 1000000);
}
BENCH(flate_encode_newton_default_1e4) {
    encode(b, NEWTON, -1, 10000);
}
BENCH(flate_encode_newton_default_1e5) {
    encode(b, NEWTON, -1, 100000);
}
BENCH(flate_encode_newton_default_1e6) {
    encode(b, NEWTON, -1, 1000000);
}
BENCH(flate_encode_newton_compression_1e4) {
    encode(b, NEWTON, 9, 10000);
}
BENCH(flate_encode_newton_compression_1e5) {
    encode(b, NEWTON, 9, 100000);
}
BENCH(flate_encode_newton_compression_1e6) {
    encode(b, NEWTON, 9, 1000000);
}
BENCH(flate_decode_digits_huffman_1e4) {
    decode(b, DIGITS, -2, 10000);
}
BENCH(flate_decode_digits_huffman_1e5) {
    decode(b, DIGITS, -2, 100000);
}
BENCH(flate_decode_digits_huffman_1e6) {
    decode(b, DIGITS, -2, 1000000);
}
BENCH(flate_decode_digits_speed_1e4) {
    decode(b, DIGITS, 1, 10000);
}
BENCH(flate_decode_digits_speed_1e5) {
    decode(b, DIGITS, 1, 100000);
}
BENCH(flate_decode_digits_speed_1e6) {
    decode(b, DIGITS, 1, 1000000);
}
BENCH(flate_decode_digits_default_1e4) {
    decode(b, DIGITS, -1, 10000);
}
BENCH(flate_decode_digits_default_1e5) {
    decode(b, DIGITS, -1, 100000);
}
BENCH(flate_decode_digits_default_1e6) {
    decode(b, DIGITS, -1, 1000000);
}
BENCH(flate_decode_digits_compression_1e4) {
    decode(b, DIGITS, 9, 10000);
}
BENCH(flate_decode_digits_compression_1e5) {
    decode(b, DIGITS, 9, 100000);
}
BENCH(flate_decode_digits_compression_1e6) {
    decode(b, DIGITS, 9, 1000000);
}
BENCH(flate_decode_newton_huffman_1e4) {
    decode(b, NEWTON, -2, 10000);
}
BENCH(flate_decode_newton_huffman_1e5) {
    decode(b, NEWTON, -2, 100000);
}
BENCH(flate_decode_newton_huffman_1e6) {
    decode(b, NEWTON, -2, 1000000);
}
BENCH(flate_decode_newton_speed_1e4) {
    decode(b, NEWTON, 1, 10000);
}
BENCH(flate_decode_newton_speed_1e5) {
    decode(b, NEWTON, 1, 100000);
}
BENCH(flate_decode_newton_speed_1e6) {
    decode(b, NEWTON, 1, 1000000);
}
BENCH(flate_decode_newton_default_1e4) {
    decode(b, NEWTON, -1, 10000);
}
BENCH(flate_decode_newton_default_1e5) {
    decode(b, NEWTON, -1, 100000);
}
BENCH(flate_decode_newton_default_1e6) {
    decode(b, NEWTON, -1, 1000000);
}
BENCH(flate_decode_newton_compression_1e4) {
    decode(b, NEWTON, 9, 10000);
}
BENCH(flate_decode_newton_compression_1e5) {
    decode(b, NEWTON, 9, 100000);
}
BENCH(flate_decode_newton_compression_1e6) {
    decode(b, NEWTON, 9, 1000000);
}

void register_flate_benchmarks(void);
void register_flate_benchmarks(void) {
    BENCH_RUN(flate_encode_digits_huffman_1e4);
    BENCH_RUN(flate_encode_digits_huffman_1e5);
    BENCH_RUN(flate_encode_digits_huffman_1e6);
    BENCH_RUN(flate_encode_digits_speed_1e4);
    BENCH_RUN(flate_encode_digits_speed_1e5);
    BENCH_RUN(flate_encode_digits_speed_1e6);
    BENCH_RUN(flate_encode_digits_default_1e4);
    BENCH_RUN(flate_encode_digits_default_1e5);
    BENCH_RUN(flate_encode_digits_default_1e6);
    BENCH_RUN(flate_encode_digits_compression_1e4);
    BENCH_RUN(flate_encode_digits_compression_1e5);
    BENCH_RUN(flate_encode_digits_compression_1e6);
    BENCH_RUN(flate_encode_newton_huffman_1e4);
    BENCH_RUN(flate_encode_newton_huffman_1e5);
    BENCH_RUN(flate_encode_newton_huffman_1e6);
    BENCH_RUN(flate_encode_newton_speed_1e4);
    BENCH_RUN(flate_encode_newton_speed_1e5);
    BENCH_RUN(flate_encode_newton_speed_1e6);
    BENCH_RUN(flate_encode_newton_default_1e4);
    BENCH_RUN(flate_encode_newton_default_1e5);
    BENCH_RUN(flate_encode_newton_default_1e6);
    BENCH_RUN(flate_encode_newton_compression_1e4);
    BENCH_RUN(flate_encode_newton_compression_1e5);
    BENCH_RUN(flate_encode_newton_compression_1e6);
    BENCH_RUN(flate_decode_digits_huffman_1e4);
    BENCH_RUN(flate_decode_digits_huffman_1e5);
    BENCH_RUN(flate_decode_digits_huffman_1e6);
    BENCH_RUN(flate_decode_digits_speed_1e4);
    BENCH_RUN(flate_decode_digits_speed_1e5);
    BENCH_RUN(flate_decode_digits_speed_1e6);
    BENCH_RUN(flate_decode_digits_default_1e4);
    BENCH_RUN(flate_decode_digits_default_1e5);
    BENCH_RUN(flate_decode_digits_default_1e6);
    BENCH_RUN(flate_decode_digits_compression_1e4);
    BENCH_RUN(flate_decode_digits_compression_1e5);
    BENCH_RUN(flate_decode_digits_compression_1e6);
    BENCH_RUN(flate_decode_newton_huffman_1e4);
    BENCH_RUN(flate_decode_newton_huffman_1e5);
    BENCH_RUN(flate_decode_newton_huffman_1e6);
    BENCH_RUN(flate_decode_newton_speed_1e4);
    BENCH_RUN(flate_decode_newton_speed_1e5);
    BENCH_RUN(flate_decode_newton_speed_1e6);
    BENCH_RUN(flate_decode_newton_default_1e4);
    BENCH_RUN(flate_decode_newton_default_1e5);
    BENCH_RUN(flate_decode_newton_default_1e6);
    BENCH_RUN(flate_decode_newton_compression_1e4);
    BENCH_RUN(flate_decode_newton_compression_1e5);
    BENCH_RUN(flate_decode_newton_compression_1e6);
}
