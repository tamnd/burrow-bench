/* The strings package, on the rows Go's own strings benchmarks have.
 *
 * The inputs follow strings_test.go and replace_test.go. The one that does not
 * is the hard input, a megabyte of HTML tokens, which Go builds with math/rand
 * seeded at 99. Both sides here build it with the same xorshift instead, so the
 * two searches walk the same bytes.
 *
 * Anything that allocates does it from an arena that is reset every iteration,
 * which is the closest C gets to Go's allocator handing back a young object
 * for free. The builder row writes into a builder that is thrown away each time,
 * the way BenchmarkBuildString_Builder does.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/func.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/slice.h"
#include "burrow/strings.h"
#include "burrow/type.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HARD_LEN (1 << 20)

static Str hard;

/* Go's makeBenchInputHard with an xorshift in place of math/rand. */
static void build_hard(void) {
    static const char *const tokens[] = {"<a>",  "<p>",  "<b>",   "<strong>", "</a>",
                                         "</p>", "</b>", "</strong>", "hello", "world"};
    if (hard.len > 0)
        return;
    char *x = malloc(HARD_LEN);
    if (x == NULL)
        abort();
    size_t n = 0;
    uint64_t r = 99;
    for (;;) {
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        const char *t = tokens[r % 10];
        size_t len = strlen(t);
        if (n + len >= HARD_LEN)
            break;
        memcpy(x + n, t, len);
        n += len;
    }
    hard = (Str){(const Byte *)x, (Int)n};
}

static Str cstr(const char *s) {
    return (Str){(const Byte *)s, (Int)strlen(s)};
}

/* ------------------------------------------------------------------ searching */

static void index_hard(Bench *b, const char *sep) {
    build_hard();
    Str s = cstr(sep);
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_index(hard, s));
    }
}

BENCH(strings_index_hard1) {
    index_hard(b, "<>");
}

BENCH(strings_index_hard2) {
    index_hard(b, "</pre>");
}

BENCH(strings_index_hard3) {
    index_hard(b, "<b>hello world</b>");
}

BENCH(strings_last_index_hard2) {
    build_hard();
    Str s = cstr("</pre>");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_last_index(hard, s));
    }
}

BENCH(strings_count_hard2) {
    build_hard();
    Str s = cstr("</pre>");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_count(hard, s));
    }
}

/* Go's BenchmarkIndex: a word near the end of a short sentence. */
BENCH(strings_index) {
    Str s = cstr("some_text=some☺value");
    Str sep = cstr("v");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_index(s, sep));
    }
}

BENCH(strings_index_rune) {
    Str s = cstr("some_text=some☺value");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_index_rune(s, 0x263A));
    }
}

BENCH(strings_index_any_ascii) {
    Str s = cstr("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.");
    Str chars = cstr(",.;:");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_index_any(s, chars));
    }
}

BENCH(strings_equal_fold) {
    Str s = cstr("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890");
    Str t = cstr("abcdefghijklmnopqrstuvwxyz1234567890");
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strings_equal_fold(s, t));
    }
}

BENCH(strings_trim_space) {
    Str s = cstr("  \t\n  some text with spaces around it  \t\n  ");
    BENCH_LOOP(b) {
        Str t = strings_trim_space(s);
        bench_keep(t.p);
    }
}

/* ----------------------------------------------------------------- building */

static const char fields_input[] =
    "the quick brown fox jumps over the lazy dog, and then again, the quick brown "
    "fox jumps over the lazy dog once more before going home for the night";

BENCH(strings_fields) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr(fields_input);
    BENCH_LOOP(b) {
        Slice f = strings_fields(a, s);
        bench_keep(f.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(strings_split_single_byte) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr(fields_input);
    Str sep = cstr(" ");
    BENCH_LOOP(b) {
        Slice f = strings_split(a, s, sep);
        bench_keep(f.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

static bool count_piece(void *env, const void *v) {
    (void)v;
    ++*(Int *)env;
    return true;
}

BENCH(strings_split_seq_single_byte) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr(fields_input);
    Str sep = cstr(" ");
    BENCH_LOOP(b) {
        Int n = 0;
        IterSeq seq = strings_split_seq(a, s, sep);
        BURROW_CALLF(seq, BURROW_FN(IterYield, count_piece, &n));
        bench_keep_u64((uint64_t)n);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(strings_join) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    static Str parts[16];
    for (int i = 0; i < 16; i++)
        parts[i] = cstr("element");
    Slice elems = slice_from(parts, 16, 16, TYPE_STRING);
    Str sep = cstr(", ");
    BENCH_LOOP(b) {
        Str j = strings_join(a, elems, sep);
        bench_keep(j.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(strings_repeat) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr("-");
    BENCH_LOOP(b) {
        /* Eighty dashes come from a static table, as in Go, so nothing is
         * allocated and there is no arena to reset. */
        Str r = strings_repeat(a, s, 80);
        bench_keep(r.p);
    }
    arena_free(&ar);
}

BENCH(strings_to_upper) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr("the quick brown fox jumps over the lazy dog");
    BENCH_LOOP(b) {
        Str u = strings_to_upper(a, s);
        bench_keep(u.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(strings_to_upper_unchanged) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr("THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG");
    BENCH_LOOP(b) {
        Str u = strings_to_upper(a, s);
        bench_keep(u.p);
    }
    arena_free(&ar);
}

BENCH(strings_replace_all) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr("banana banana banana banana banana banana banana banana");
    Str old = cstr("a"), repl = cstr("<>");
    BENCH_LOOP(b) {
        Str r = strings_replace_all(a, s, old, repl);
        bench_keep(r.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(strings_builder) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str piece = cstr("some string ");
    BENCH_LOOP(b) {
        StringsBuilder sb = STRINGS_BUILDER(a);
        for (int i = 0; i < 16; i++)
            strings_builder_write_string(&sb, piece, NULL);
        Str s = strings_builder_string(&sb);
        bench_keep(s.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

/* ---------------------------------------------------------------- replacers */

static const Str html_pairs[] = {
    BURROW_S_INIT("&"),  BURROW_S_INIT("&amp;"),  BURROW_S_INIT("<"),  BURROW_S_INIT("&lt;"),
    BURROW_S_INIT(">"),  BURROW_S_INIT("&gt;"),   BURROW_S_INIT("\""), BURROW_S_INIT("&quot;"),
    BURROW_S_INIT("'"),  BURROW_S_INIT("&apos;"),
};

static const Str unescape_pairs[] = {
    BURROW_S_INIT("&amp;"),  BURROW_S_INIT("&"),  BURROW_S_INIT("&lt;"),   BURROW_S_INIT("<"),
    BURROW_S_INIT("&gt;"),   BURROW_S_INIT(">"),  BURROW_S_INIT("&quot;"), BURROW_S_INIT("\""),
    BURROW_S_INIT("&apos;"), BURROW_S_INIT("'"),
};

static void run_replacer(Bench *b, const Str *pairs, Int n, Str s) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Arena keep;
    arena_init(&keep, NULL, 0);
    StringsReplacer *r =
        strings_new_replacer(arena_allocator(&keep), slice_from((void *)(uintptr_t)pairs, n, n, TYPE_STRING));
    strings_replacer_replace(r, a, s);
    arena_reset(&ar);
    BENCH_LOOP(b) {
        Str out = strings_replacer_replace(r, a, s);
        bench_keep(out.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
    arena_free(&keep);
}

BENCH(strings_html_escape) {
    run_replacer(b, html_pairs, 10, cstr("I <3 to escape HTML & other text too."));
}

BENCH(strings_generic_match2) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Str s = strings_repeat(arena_allocator(&ar), cstr("It&apos;s &lt;b&gt;HTML&lt;/b&gt;!"),
                           100);
    run_replacer(b, unescape_pairs, 10, s);
    arena_free(&ar);
}

BENCH(strings_single_match) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Str s = strings_repeat(arena_allocator(&ar), cstr("abcdefghijklmno"), 1000);
    static const Str pairs[] = {BURROW_S_INIT("abcdef"), BURROW_S_INIT("[match]")};
    run_replacer(b, pairs, 2, s);
    arena_free(&ar);
}

void register_strings_benchmarks(void);

void register_strings_benchmarks(void) {
    BENCH_RUN(strings_index_hard1);
    BENCH_RUN(strings_index_hard2);
    BENCH_RUN(strings_index_hard3);
    BENCH_RUN(strings_last_index_hard2);
    BENCH_RUN(strings_count_hard2);
    BENCH_RUN(strings_index);
    BENCH_RUN(strings_index_rune);
    BENCH_RUN(strings_index_any_ascii);
    BENCH_RUN(strings_equal_fold);
    BENCH_RUN(strings_trim_space);
    BENCH_RUN(strings_fields);
    BENCH_RUN(strings_split_single_byte);
    BENCH_RUN(strings_split_seq_single_byte);
    BENCH_RUN(strings_join);
    BENCH_RUN(strings_repeat);
    BENCH_RUN(strings_to_upper);
    BENCH_RUN(strings_to_upper_unchanged);
    BENCH_RUN(strings_replace_all);
    BENCH_RUN(strings_builder);
    BENCH_RUN(strings_html_escape);
    BENCH_RUN(strings_generic_match2);
    BENCH_RUN(strings_single_match);
}
