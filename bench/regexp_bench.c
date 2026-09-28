/* regexp, on Go's benchmarks from all_test.go and exec_test.go.
 *
 * The single benchmarks keep Go's names with a regexp_ prefix, so Go's
 * BenchmarkOnePassShortA is regexp_onepass_short_a here. Go's BenchmarkMatch
 * runs every pattern in benchData over every size in benchSizes as
 * sub-benchmarks, and those are regexp_match_easy0_16 and so on. The 32M size
 * is left out, since the hard patterns take seconds per run at that length,
 * and so are the two RunParallel benchmarks, which the harness has no
 * equivalent for.
 *
 * Anything a call allocates comes from an arena that is reset every time
 * round, which is what a C caller would do. Go pays for the same memory
 * through its collector instead.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/burrow.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"

#include <stdlib.h>
#include <string.h>

static Regexp *must(const char *pat) {
    Regexp *re = regexp_must_compile(heap_allocator(), str_from_cstr(pat));
    return (Regexp *)bench_hide(re);
}

static Slice bytes_of(const char *s) {
    return slice_from((void *)(uintptr_t)s, (Int)strlen(s), (Int)strlen(s), TYPE_BYTE);
}

/* s repeated n times, from the heap. */
static char *repeat(const char *s, int n) {
    size_t len = strlen(s);
    char *out = malloc(len * (size_t)n + 1);
    for (int i = 0; i < n; i++)
        memcpy(out + len * (size_t)i, s, len);
    out[len * (size_t)n] = 0;
    return out;
}

BENCH(regexp_find) {
    Regexp *re = must("a+b+");
    Slice s = bytes_of("acbbaaabbdd");
    BENCH_LOOP(b) {
        Slice m = regexp_find(re, s);
        if (m.len != 5)
            abort();
    }
    regexp_free(re);
}

BENCH(regexp_find_all_no_matches) {
    Regexp *re = must("a+b+");
    Slice s = bytes_of("acddee");
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Slice all = regexp_find_all(re, a, s, -1);
        if (all.p != NULL)
            abort();
        arena_reset(&ar);
    }
    arena_free(&ar);
    regexp_free(re);
}

BENCH(regexp_find_all_ten_matches) {
    Regexp *re = must("a+b+");
    char *x = repeat("acddeeabbax", 10);
    Slice s = bytes_of(x);
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Slice all = regexp_find_all(re, a, s, -1);
        if (all.len != 10)
            abort();
        arena_reset(&ar);
    }
    arena_free(&ar);
    free(x);
    regexp_free(re);
}

BENCH(regexp_find_string) {
    Regexp *re = must("a+b+");
    Str s = BURROW_S("acbbaaabbdd");
    BENCH_LOOP(b) {
        Str m = regexp_find_string(re, s);
        if (m.len != 5)
            abort();
    }
    regexp_free(re);
}

BENCH(regexp_find_submatch) {
    Regexp *re = must("a(a+b+)b");
    Slice s = bytes_of("acbbaaabbdd");
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Slice m = regexp_find_submatch(re, a, s);
        if (m.len != 2 || ((Slice *)m.p)[0].len != 5 || ((Slice *)m.p)[1].len != 3)
            abort();
        arena_reset(&ar);
    }
    arena_free(&ar);
    regexp_free(re);
}

BENCH(regexp_find_string_submatch) {
    Regexp *re = must("a(a+b+)b");
    Str s = BURROW_S("acbbaaabbdd");
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Slice m = regexp_find_string_submatch(re, a, s);
        if (m.len != 2 || ((Str *)m.p)[0].len != 5 || ((Str *)m.p)[1].len != 3)
            abort();
        arena_reset(&ar);
    }
    arena_free(&ar);
    regexp_free(re);
}

/* Go's Literal, NotLiteral, MatchClass and MatchClass_InRange: MatchString
 * that has to succeed. */
static void match_string(Bench *b, const char *pat, const char *unit, int n,
                         const char *tail) {
    Regexp *re = must(pat);
    char *head = repeat(unit, n);
    size_t hl = strlen(head), tl = strlen(tail);
    char *x = malloc(hl + tl + 1);
    memcpy(x, head, hl);
    memcpy(x + hl, tail, tl + 1);
    Str s = str_from_cstr(x);
    BENCH_LOOP(b) {
        if (!regexp_match_string(re, s))
            abort();
    }
    free(x);
    free(head);
    regexp_free(re);
}

BENCH(regexp_literal) {
    match_string(b, "y", "x", 50, "y");
}
BENCH(regexp_not_literal) {
    match_string(b, ".y", "x", 50, "y");
}
BENCH(regexp_match_class) {
    match_string(b, "[abcdw]", "xxxx", 20, "w");
}
BENCH(regexp_match_class_in_range) {
    match_string(b, "[ac]", "bbbb", 20, "c");
}

BENCH(regexp_replace_all) {
    Regexp *re = must("[cjrw]");
    Str x = BURROW_S("abcdefghijklmnopqrstuvwxyz");
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Str r = regexp_replace_all_string(re, a, x, BURROW_STR_EMPTY);
        bench_keep(r.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
    regexp_free(re);
}

/* The anchored and one pass benchmarks: Match on bytes, with the result
 * thrown away as Go throws it away. long is the alphabet doubled 15 times. */
static void match_bytes(Bench *b, const char *pat, const char *text, bool long_text) {
    Regexp *re = must(pat);
    char *x = long_text ? repeat(text, 1 << 15) : repeat(text, 1);
    Slice s = bytes_of(x);
    BENCH_LOOP(b) {
        bench_keep_u64(regexp_match(re, s));
    }
    free(x);
    regexp_free(re);
}

#define ALPHABET "abcdefghijklmnopqrstuvwxyz"

BENCH(regexp_anchored_literal_short_non_match) {
    match_bytes(b, "^zbc(d|e)", ALPHABET, false);
}
BENCH(regexp_anchored_literal_long_non_match) {
    match_bytes(b, "^zbc(d|e)", ALPHABET, true);
}
BENCH(regexp_anchored_short_match) {
    match_bytes(b, "^.bc(d|e)", ALPHABET, false);
}
BENCH(regexp_anchored_long_match) {
    match_bytes(b, "^.bc(d|e)", ALPHABET, true);
}
BENCH(regexp_onepass_short_a) {
    match_bytes(b, "^.bc(d|e)*$", "abcddddddeeeededd", false);
}
BENCH(regexp_not_onepass_short_a) {
    match_bytes(b, ".bc(d|e)*$", "abcddddddeeeededd", false);
}
BENCH(regexp_onepass_short_b) {
    match_bytes(b, "^.bc(?:d|e)*$", "abcddddddeeeededd", false);
}
BENCH(regexp_not_onepass_short_b) {
    match_bytes(b, ".bc(?:d|e)*$", "abcddddddeeeededd", false);
}
BENCH(regexp_onepass_long_prefix) {
    match_bytes(b, "^abcdefghijklmnopqrstuvwxyz.*$", ALPHABET, false);
}
BENCH(regexp_onepass_long_not_prefix) {
    match_bytes(b, "^.bcdefghijklmnopqrstuvwxyz.*$", ALPHABET, false);
}

static void quote_meta(Bench *b, Str s) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    s = str_from_bytes(bench_hide(s.p), s.len);
    BENCH_LOOP(b) {
        Str q = regexp_quote_meta(a, s);
        bench_keep(q.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(regexp_quote_meta_all) {
    quote_meta(b, BURROW_S("$()*+.?[\\]^{|}"));
}
BENCH(regexp_quote_meta_none) {
    quote_meta(b, BURROW_S(ALPHABET));
}

/* Go's BenchmarkCompile. Each compile gets its own memory from the heap and
 * gives it back with regexp_free. */
static void compile(Bench *b, Str pat) {
    Alloc *heap = heap_allocator();
    Error err;
    BENCH_LOOP(b) {
        Regexp *re = regexp_compile(heap, pat, &err);
        if (re == NULL)
            abort();
        regexp_free(re);
    }
}

BENCH(regexp_compile_onepass) {
    compile(b, BURROW_S("^a.[l-nA-Cg-j]?e$"));
}
BENCH(regexp_compile_medium) {
    compile(b, BURROW_S("^((a|b|[d-z0-9])*(\xe6\x97\xa5){4,5}.)+$"));
}
BENCH(regexp_compile_hard) {
    char *open = repeat("((abc)*|", 50);
    char *close = repeat(")", 50);
    size_t ol = strlen(open);
    char *pat = malloc(ol + 51);
    memcpy(pat, open, ol);
    memcpy(pat + ol, close, 51);
    compile(b, str_from_cstr(pat));
    free(pat);
    free(close);
    free(open);
}

/* exec_test.go's makeText: printable ASCII with a newline now and then, from
 * the same generator, so both sides search the same bytes. */
static Byte *make_text(Int n) {
    Byte *t = malloc((size_t)n);
    uint32_t x = ~(uint32_t)0;
    for (Int i = 0; i < n; i++) {
        x += x;
        x ^= 1;
        if ((int32_t)x < 0)
            x ^= 0x88888eef;
        if (x % 31 == 0)
            t[i] = '\n';
        else
            t[i] = (Byte)(x % (0x7E + 1 - 0x20) + 0x20);
    }
    return t;
}

/* BenchmarkMatch: none of the patterns match the text. */
static void match_data(Bench *b, const char *pat, Int n) {
    Regexp *re = must(pat);
    Byte *t = make_text(n);
    Slice s = slice_from(t, n, n, TYPE_BYTE);
    BENCH_LOOP(b) {
        if (regexp_match(re, s))
            abort();
    }
    free(t);
    regexp_free(re);
}

/* BenchmarkMatch_onepass_regex: a one pass pattern that matches everything. */
static void match_onepass(Bench *b, Int n) {
    Regexp *re = must("(?s)\\A.*\\z");
    Byte *t = make_text(n);
    Slice s = slice_from(t, n, n, TYPE_BYTE);
    BENCH_LOOP(b) {
        if (!regexp_match(re, s))
            abort();
    }
    free(t);
    regexp_free(re);
}

/* Written out one per line so that tools/check-pairs.sh can find them. */
BENCH(regexp_match_easy0_16) {
    match_data(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 16);
}
BENCH(regexp_match_easy0_32) {
    match_data(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 32);
}
BENCH(regexp_match_easy0_1k) {
    match_data(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 1024);
}
BENCH(regexp_match_easy0_32k) {
    match_data(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 32768);
}
BENCH(regexp_match_easy0_1m) {
    match_data(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 1048576);
}
BENCH(regexp_match_easy0i_16) {
    match_data(b, "(?i)ABCDEFGHIJklmnopqrstuvwxyz$", 16);
}
BENCH(regexp_match_easy0i_32) {
    match_data(b, "(?i)ABCDEFGHIJklmnopqrstuvwxyz$", 32);
}
BENCH(regexp_match_easy0i_1k) {
    match_data(b, "(?i)ABCDEFGHIJklmnopqrstuvwxyz$", 1024);
}
BENCH(regexp_match_easy0i_32k) {
    match_data(b, "(?i)ABCDEFGHIJklmnopqrstuvwxyz$", 32768);
}
BENCH(regexp_match_easy0i_1m) {
    match_data(b, "(?i)ABCDEFGHIJklmnopqrstuvwxyz$", 1048576);
}
BENCH(regexp_match_easy1_16) {
    match_data(b, "A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$", 16);
}
BENCH(regexp_match_easy1_32) {
    match_data(b, "A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$", 32);
}
BENCH(regexp_match_easy1_1k) {
    match_data(b, "A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$", 1024);
}
BENCH(regexp_match_easy1_32k) {
    match_data(b, "A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$", 32768);
}
BENCH(regexp_match_easy1_1m) {
    match_data(b, "A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$", 1048576);
}
BENCH(regexp_match_medium_16) {
    match_data(b, "[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 16);
}
BENCH(regexp_match_medium_32) {
    match_data(b, "[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 32);
}
BENCH(regexp_match_medium_1k) {
    match_data(b, "[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 1024);
}
BENCH(regexp_match_medium_32k) {
    match_data(b, "[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 32768);
}
BENCH(regexp_match_medium_1m) {
    match_data(b, "[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 1048576);
}
BENCH(regexp_match_hard_16) {
    match_data(b, "[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 16);
}
BENCH(regexp_match_hard_32) {
    match_data(b, "[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 32);
}
BENCH(regexp_match_hard_1k) {
    match_data(b, "[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 1024);
}
BENCH(regexp_match_hard_32k) {
    match_data(b, "[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 32768);
}
BENCH(regexp_match_hard_1m) {
    match_data(b, "[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$", 1048576);
}
BENCH(regexp_match_hard1_16) {
    match_data(b, "ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ", 16);
}
BENCH(regexp_match_hard1_32) {
    match_data(b, "ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ", 32);
}
BENCH(regexp_match_hard1_1k) {
    match_data(b, "ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ", 1024);
}
BENCH(regexp_match_hard1_32k) {
    match_data(b, "ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ", 32768);
}
BENCH(regexp_match_hard1_1m) {
    match_data(b, "ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ",
               1048576);
}
BENCH(regexp_match_onepass_regex_16) {
    match_onepass(b, 16);
}
BENCH(regexp_match_onepass_regex_32) {
    match_onepass(b, 32);
}
BENCH(regexp_match_onepass_regex_1k) {
    match_onepass(b, 1024);
}
BENCH(regexp_match_onepass_regex_32k) {
    match_onepass(b, 32768);
}
BENCH(regexp_match_onepass_regex_1m) {
    match_onepass(b, 1048576);
}

void register_regexp_benchmarks(void);
void register_regexp_benchmarks(void) {
    BENCH_RUN(regexp_find);
    BENCH_RUN(regexp_find_all_no_matches);
    BENCH_RUN(regexp_find_all_ten_matches);
    BENCH_RUN(regexp_find_string);
    BENCH_RUN(regexp_find_submatch);
    BENCH_RUN(regexp_find_string_submatch);
    BENCH_RUN(regexp_literal);
    BENCH_RUN(regexp_not_literal);
    BENCH_RUN(regexp_match_class);
    BENCH_RUN(regexp_match_class_in_range);
    BENCH_RUN(regexp_replace_all);
    BENCH_RUN(regexp_anchored_literal_short_non_match);
    BENCH_RUN(regexp_anchored_literal_long_non_match);
    BENCH_RUN(regexp_anchored_short_match);
    BENCH_RUN(regexp_anchored_long_match);
    BENCH_RUN(regexp_onepass_short_a);
    BENCH_RUN(regexp_not_onepass_short_a);
    BENCH_RUN(regexp_onepass_short_b);
    BENCH_RUN(regexp_not_onepass_short_b);
    BENCH_RUN(regexp_onepass_long_prefix);
    BENCH_RUN(regexp_onepass_long_not_prefix);
    BENCH_RUN(regexp_quote_meta_all);
    BENCH_RUN(regexp_quote_meta_none);
    BENCH_RUN(regexp_compile_onepass);
    BENCH_RUN(regexp_compile_medium);
    BENCH_RUN(regexp_compile_hard);
    BENCH_RUN(regexp_match_easy0_16);
    BENCH_RUN(regexp_match_easy0_32);
    BENCH_RUN(regexp_match_easy0_1k);
    BENCH_RUN(regexp_match_easy0_32k);
    BENCH_RUN(regexp_match_easy0_1m);
    BENCH_RUN(regexp_match_easy0i_16);
    BENCH_RUN(regexp_match_easy0i_32);
    BENCH_RUN(regexp_match_easy0i_1k);
    BENCH_RUN(regexp_match_easy0i_32k);
    BENCH_RUN(regexp_match_easy0i_1m);
    BENCH_RUN(regexp_match_easy1_16);
    BENCH_RUN(regexp_match_easy1_32);
    BENCH_RUN(regexp_match_easy1_1k);
    BENCH_RUN(regexp_match_easy1_32k);
    BENCH_RUN(regexp_match_easy1_1m);
    BENCH_RUN(regexp_match_medium_16);
    BENCH_RUN(regexp_match_medium_32);
    BENCH_RUN(regexp_match_medium_1k);
    BENCH_RUN(regexp_match_medium_32k);
    BENCH_RUN(regexp_match_medium_1m);
    BENCH_RUN(regexp_match_hard_16);
    BENCH_RUN(regexp_match_hard_32);
    BENCH_RUN(regexp_match_hard_1k);
    BENCH_RUN(regexp_match_hard_32k);
    BENCH_RUN(regexp_match_hard_1m);
    BENCH_RUN(regexp_match_hard1_16);
    BENCH_RUN(regexp_match_hard1_32);
    BENCH_RUN(regexp_match_hard1_1k);
    BENCH_RUN(regexp_match_hard1_32k);
    BENCH_RUN(regexp_match_hard1_1m);
    BENCH_RUN(regexp_match_onepass_regex_16);
    BENCH_RUN(regexp_match_onepass_regex_32);
    BENCH_RUN(regexp_match_onepass_regex_1k);
    BENCH_RUN(regexp_match_onepass_regex_32k);
    BENCH_RUN(regexp_match_onepass_regex_1m);
}
