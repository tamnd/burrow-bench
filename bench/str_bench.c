/* Str against the C string functions it replaces.
 *
 * The comparison to keep in mind while reading these: a C string knows where it
 * ends by looking, and a Str knows because it was told. So Str should win on
 * anything that needs the length and should be level on anything that does not.
 * Where it loses, that is a bug in burrow and not a fact about the design.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"

#include <string.h>

/* Three lengths, because string code changes character completely across them.
 * Short is a map key or a field name and is dominated by the call. Medium is a
 * line of text. Long is a buffer, where the loop is the whole cost. */
static const char short_c[] = "burrow";
static const char medium_c[] =
    "the quick brown fox jumps over the lazy dog, and then does it again";

#define LONG_LEN 4096
static char long_a[LONG_LEN + 1];
static char long_b[LONG_LEN + 1];

static void fill_long(void) {
    if (long_a[0] != '\0')
        return;
    for (int i = 0; i < LONG_LEN; i++) {
        long_a[i] = (char)('a' + (i % 26));
        long_b[i] = (char)('a' + (i % 26));
    }
    /* Differ in the last byte only, so that a comparison has to read all of it.
     * A pair that differs early measures the setup and nothing else. */
    long_b[LONG_LEN - 1] = 'Z';
}

/* -------------------------------------------------------------- from_cstr */

/* This should compile down to strlen and a two word struct, so the two numbers
 * below should be the same number. If they are not, something is copying. */
BENCH(str_from_cstr_short) {
    BENCH_LOOP(b) {
        Str s = str_from_cstr(short_c);
        bench_keep_u64((uint64_t)s.len);
    }
}

BENCH(strlen_short) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strlen(short_c));
    }
}

BENCH(str_from_cstr_medium) {
    BENCH_LOOP(b) {
        Str s = str_from_cstr(medium_c);
        bench_keep_u64((uint64_t)s.len);
    }
}

BENCH(strlen_medium) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)strlen(medium_c));
    }
}

/* ------------------------------------------------------------------- eq */

/* Two strings of the same length that differ in the last byte, which is the
 * case where Str's length check cannot help and the comparison does the work. */
BENCH(str_eq_long) {
    fill_long();
    Str a = str_from_bytes(long_a, LONG_LEN);
    Str c = str_from_bytes(long_b, LONG_LEN);

    BENCH_LOOP(b) {
        bench_keep_u64(str_eq(a, c) ? 1u : 0u);
    }
}

BENCH(memcmp_long) {
    fill_long();
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(memcmp(long_a, long_b, LONG_LEN) == 0));
    }
}

/* Different lengths, which is the case strcmp has to walk and Str answers from
 * the two length words without touching either buffer. This is the one place
 * the design should show a large gap rather than a small one. */
BENCH(str_eq_different_lengths) {
    fill_long();
    Str a = str_from_bytes(long_a, LONG_LEN);
    Str c = str_from_bytes(long_b, LONG_LEN - 1);

    BENCH_LOOP(b) {
        bench_keep_u64(str_eq(a, c) ? 1u : 0u);
    }
}

BENCH(strcmp_different_lengths) {
    fill_long();
    /* strcmp has no length to check, so it reads to the difference either way.
     * Both buffers are NUL terminated for this one, which fill_long arranges
     * by leaving the last byte of each array alone. */
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(strcmp(long_a, long_b) == 0));
    }
}

BENCH(str_eq_short) {
    Str a = str_from_cstr(short_c);
    Str c = str_from_cstr(short_c);

    BENCH_LOOP(b) {
        bench_keep_u64(str_eq(a, c) ? 1u : 0u);
    }
}

BENCH(strcmp_short) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)(strcmp(short_c, short_c) == 0));
    }
}

/* ------------------------------------------------------------------ cmp */

BENCH(str_cmp_long) {
    fill_long();
    Str a = str_from_bytes(long_a, LONG_LEN);
    Str c = str_from_bytes(long_b, LONG_LEN);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)str_cmp(a, c));
    }
}

/* ----------------------------------------------------------------- clone */

BENCH(str_clone_medium) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = str_from_cstr(medium_c);

    BENCH_LOOP(b) {
        Str c = str_clone(a, s);
        bench_keep(c.p);
        arena_reset(&ar);
    }

    AllocStats st = mem_stats(a);
    bench_report_allocs(b, st.bytes_total, st.allocs);
    arena_free(&ar);
}

/* ------------------------------------------------------------------- at */

/* The bounds checked index against the raw one. The gap is what Go's index
 * check costs, and it is the number people quote when they argue the check is
 * too expensive to have. */
BENCH(str_at) {
    fill_long();
    Str s = str_from_bytes(long_a, LONG_LEN);

    BENCH_LOOP(b) {
        uint64_t sum = 0;
        for (Int i = 0; i < 64; i++)
            sum += str_at(s, i);
        bench_keep_u64(sum);
    }
}

BENCH(raw_index) {
    fill_long();

    BENCH_LOOP(b) {
        uint64_t sum = 0;
        for (int i = 0; i < 64; i++)
            sum += (unsigned char)long_a[i];
        bench_keep_u64(sum);
    }
}

void register_str_benchmarks(void);

void register_str_benchmarks(void) {
    BENCH_RUN(str_from_cstr_short);
    BENCH_RUN(strlen_short);
    BENCH_RUN(str_from_cstr_medium);
    BENCH_RUN(strlen_medium);
    BENCH_RUN(str_eq_short);
    BENCH_RUN(strcmp_short);
    BENCH_RUN(str_eq_long);
    BENCH_RUN(memcmp_long);
    BENCH_RUN(str_eq_different_lengths);
    BENCH_RUN(strcmp_different_lengths);
    BENCH_RUN(str_cmp_long);
    BENCH_RUN(str_clone_medium);
    BENCH_RUN(str_at);
    BENCH_RUN(raw_index);
}
