/* Numbers to text and back, the rows Go's own strconv benchmarks have.
 *
 * Every row appends into a buffer that already has room, on both sides, so
 * what is measured is the conversion and not the allocator. Go's AppendFloat
 * benchmarks are written the same way. The inputs are Go's too, taken from
 * BenchmarkAppendFloat and BenchmarkAtof64 in strconv, so a gap here is a gap
 * on the cases Go chose to watch.
 *
 * The float rows are the ones to read. Both sides run the same algorithm, Go
 * 1.27's unrounded scaling, and burrow's formatting runs it twice, once to
 * count the bytes and once to write them, which is the price of never
 * allocating more than the result needs.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/slice.h"
#include "burrow/strconv.h"
#include "burrow/type.h"

#include <string.h>

static Byte buf[128];

static Slice empty_buf(void) {
    return slice_from(buf, 0, (Int)sizeof buf, TYPE_BYTE);
}

static Str cstr(const char *s) {
    Str r = {(const Byte *)s, (Int)strlen(s)};
    return r;
}

/* ------------------------------------------------------------------ integers */

BENCH(strconv_atoi) {
    Str s = cstr("12345678");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strconv_atoi(s, NULL));
    }
}

BENCH(strconv_atoi_neg) {
    Str s = cstr("-12345678");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strconv_atoi(s, NULL));
    }
}

BENCH(strconv_parse_int_hex) {
    Str s = cstr("7fffffffffffffff");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strconv_parse_int(s, 16, 64, NULL));
    }
}

BENCH(strconv_append_int) {
    Alloc *a = heap_allocator();
    BENCH_LOOP(b) {
        Slice d = strconv_append_int(a, empty_buf(), -1234567890123456789LL, 10);
        bench_keep(d.p);
    }
}

BENCH(strconv_append_int_small) {
    Alloc *a = heap_allocator();
    BENCH_LOOP(b) {
        Slice d = strconv_append_int(a, empty_buf(), (int64_t)(bench_i_ & 63), 10);
        bench_keep(d.p);
    }
}

BENCH(strconv_append_uint_hex) {
    Alloc *a = heap_allocator();
    BENCH_LOOP(b) {
        Slice d = strconv_append_uint(a, empty_buf(), 0xdeadbeefcafef00dULL, 16);
        bench_keep(d.p);
    }
}

/* ---------------------------------------------------------------- floats in */

static void atof64(Bench *b, const char *text) {
    Str s = cstr(text);
    BENCH_LOOP(b) {
        double f = strconv_parse_float(s, 64, NULL);
        uint64_t u;
        memcpy(&u, &f, sizeof u);
        bench_keep_u64(u);
    }
}

BENCH(strconv_atof64_decimal) {
    atof64(b, "33909");
}

BENCH(strconv_atof64_float) {
    atof64(b, "339.7784");
}

BENCH(strconv_atof64_exp) {
    atof64(b, "-5.09e75");
}

BENCH(strconv_atof64_big) {
    atof64(b, "123456789123456789123456789");
}

BENCH(strconv_atof64_shortest) {
    atof64(b, "2.2250738585072014e-308");
}

BENCH(strconv_atof64_hard) {
    atof64(b, "622666234635.321003e-320");
}

BENCH(strconv_atof32_float) {
    Str s = cstr("339.7784");
    BENCH_LOOP(b) {
        double f = strconv_parse_float(s, 32, NULL);
        uint64_t u;
        memcpy(&u, &f, sizeof u);
        bench_keep_u64(u);
    }
}

/* --------------------------------------------------------------- floats out */

static void ftoa(Bench *b, double f, Byte fmt, Int prec, Int bits) {
    Alloc *a = heap_allocator();
    BENCH_LOOP(b) {
        Slice d = strconv_append_float(a, empty_buf(), f, fmt, prec, bits);
        bench_keep(d.p);
    }
}

BENCH(strconv_ftoa_decimal) {
    ftoa(b, 33909, 'g', -1, 64);
}

BENCH(strconv_ftoa_float) {
    ftoa(b, 339.7784, 'g', -1, 64);
}

BENCH(strconv_ftoa_exp) {
    ftoa(b, -5.09e75, 'g', -1, 64);
}

BENCH(strconv_ftoa_neg_exp) {
    ftoa(b, -5.11e-95, 'g', -1, 64);
}

BENCH(strconv_ftoa_long_exp) {
    ftoa(b, 1.234567890123456e-78, 'g', -1, 64);
}

BENCH(strconv_ftoa_big) {
    ftoa(b, 123456789123456789123456789.0, 'g', -1, 64);
}

BENCH(strconv_ftoa_binary_exp) {
    ftoa(b, -1, 'b', -1, 64);
}

BENCH(strconv_ftoa32_float) {
    ftoa(b, (double)339.7784f, 'g', -1, 32);
}

BENCH(strconv_ftoa32_shortest) {
    ftoa(b, (double)1.6f, 'g', -1, 32);
}

BENCH(strconv_ftoa64_fixed1) {
    ftoa(b, 123456, 'e', 3, 64);
}

BENCH(strconv_ftoa64_fixed3) {
    ftoa(b, 1.23456e+78, 'e', 3, 64);
}

BENCH(strconv_ftoa64_fixed12) {
    ftoa(b, 1.23456789e-78, 'e', 12, 64);
}

BENCH(strconv_ftoa64_fixed17) {
    ftoa(b, 1.2345678901234567e-78, 'e', 17, 64);
}

BENCH(strconv_ftoa64_f) {
    ftoa(b, 339.7784, 'f', 6, 64);
}

BENCH(strconv_ftoa64_slow_path) {
    ftoa(b, 622666234635.3213e-320, 'e', -1, 64);
}

/* ------------------------------------------------------------------- quoting */

BENCH(strconv_append_quote) {
    Alloc *a = heap_allocator();
    Str s = cstr("\a\b\f\r\n\t\v\a\b\f\r\n\t\v\a\b\f\r\n\t\v");
    BENCH_LOOP(b) {
        Slice d = strconv_append_quote(a, empty_buf(), s);
        bench_keep(d.p);
    }
}

BENCH(strconv_append_quote_rune) {
    Alloc *a = heap_allocator();
    BENCH_LOOP(b) {
        Slice d = strconv_append_quote_rune(a, empty_buf(), '\a');
        bench_keep(d.p);
    }
}

BENCH(strconv_unquote_easy) {
    Arena ar;
    arena_init(&ar, heap_allocator(), 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr("\"Give me a rock, paper and scissors and I will move the world.\"");
    BENCH_LOOP(b) {
        Str u = strconv_unquote(a, s, NULL);
        bench_keep(u.p);
    }
    arena_free(&ar);
}

BENCH(strconv_unquote_hard) {
    Arena ar;
    arena_init(&ar, heap_allocator(), 0);
    Alloc *a = arena_allocator(&ar);
    ArenaMark m = arena_mark(&ar);
    Str s = cstr("\"\\x47ive me a \\x72ock, \\x70aper and \\x73cissors and \\x49 will move "
                 "the world.\"");
    BENCH_LOOP(b) {
        Str u = strconv_unquote(a, s, NULL);
        bench_keep(u.p);
        arena_release(&ar, m);
    }
    arena_free(&ar);
}

void register_strconv_benchmarks(void);

void register_strconv_benchmarks(void) {
    BENCH_RUN(strconv_atoi);
    BENCH_RUN(strconv_atoi_neg);
    BENCH_RUN(strconv_parse_int_hex);
    BENCH_RUN(strconv_append_int);
    BENCH_RUN(strconv_append_int_small);
    BENCH_RUN(strconv_append_uint_hex);
    BENCH_RUN(strconv_atof64_decimal);
    BENCH_RUN(strconv_atof64_float);
    BENCH_RUN(strconv_atof64_exp);
    BENCH_RUN(strconv_atof64_big);
    BENCH_RUN(strconv_atof64_shortest);
    BENCH_RUN(strconv_atof64_hard);
    BENCH_RUN(strconv_atof32_float);
    BENCH_RUN(strconv_ftoa_decimal);
    BENCH_RUN(strconv_ftoa_float);
    BENCH_RUN(strconv_ftoa_exp);
    BENCH_RUN(strconv_ftoa_neg_exp);
    BENCH_RUN(strconv_ftoa_long_exp);
    BENCH_RUN(strconv_ftoa_big);
    BENCH_RUN(strconv_ftoa_binary_exp);
    BENCH_RUN(strconv_ftoa32_float);
    BENCH_RUN(strconv_ftoa32_shortest);
    BENCH_RUN(strconv_ftoa64_fixed1);
    BENCH_RUN(strconv_ftoa64_fixed3);
    BENCH_RUN(strconv_ftoa64_fixed12);
    BENCH_RUN(strconv_ftoa64_fixed17);
    BENCH_RUN(strconv_ftoa64_f);
    BENCH_RUN(strconv_ftoa64_slow_path);
    BENCH_RUN(strconv_append_quote);
    BENCH_RUN(strconv_append_quote_rune);
    BENCH_RUN(strconv_unquote_easy);
    BENCH_RUN(strconv_unquote_hard);
}
