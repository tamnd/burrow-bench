/* net/url, on the rows Go's own url benchmarks have, plus Parse and
 * ResolveReference, which Go has no benchmark for.
 *
 * The escape inputs are Go's escapeBenchmarks. Every row that makes a string
 * or a Url takes it from an arena that is reset every time round, as the
 * strings rows do.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/net/url.h"

#include <string.h>

static Str cstr(const char *s) {
    return (Str){(const Byte *)s, (Int)strlen(s)};
}

#define LONG_UNESCAPED                                                                 \
    "padded/with+various%characters?that=need$some@escaping+paddedsowebreak/256bytes"
#define LONG_QUERY                                                                     \
    "padded%2Fwith%2Bvarious%25characters%3Fthat%3Dneed%24some%40escaping%"            \
    "2Bpaddedsowebr"                                                                   \
    "eak%2F256bytes"
#define LONG_PATH                                                                      \
    "padded%2Fwith+various%25characters%3Fthat=need$some@escaping+paddedsowebreak%"    \
    "2F256b"                                                                           \
    "ytes"

static const char *const unescaped[] = {
    "one two",
    "Фотки собак",
    "shortrun(break)shortrun",
    "longerrunofcharacters(break)anotherlongerrunofcharacters",
    LONG_UNESCAPED LONG_UNESCAPED LONG_UNESCAPED LONG_UNESCAPED,
};

static const char *const queried[] = {
    "one+two",
    "%D0%A4%D0%BE%D1%82%D0%BA%D0%B8+%D1%81%D0%BE%D0%B1%D0%B0%D0%BA",
    "shortrun%28break%29shortrun",
    "longerrunofcharacters%28break%29anotherlongerrunofcharacters",
    LONG_QUERY LONG_QUERY LONG_QUERY LONG_QUERY,
};

static const char *const pathed[] = {
    "one%20two",
    "%D0%A4%D0%BE%D1%82%D0%BA%D0%B8%20%D1%81%D0%BE%D0%B1%D0%B0%D0%BA",
    "shortrun%28break%29shortrun",
    "longerrunofcharacters%28break%29anotherlongerrunofcharacters",
    LONG_PATH LONG_PATH LONG_PATH LONG_PATH,
};

/* One URL with every part, and one of the plain kind a crawler sees. */
static const char *const urls[] = {
    "https://user:pass@go.dev:8443/doc/a%20b/c?q=url&m=text#top",
    "http://www.google.com/search?q=go+language",
};

typedef Str (*escape_fn)(Alloc *a, Str s);
typedef Str (*unescape_fn)(Alloc *a, Str s, Error *err);

static void escape(Bench *b, escape_fn f, const char *in) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr(in);
    BENCH_LOOP(b) {
        Str r = f(a, s);
        bench_keep(r.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

static void unescape(Bench *b, unescape_fn f, const char *in) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr(in);
    BENCH_LOOP(b) {
        Error err;
        Str r = f(a, s, &err);
        bench_keep(r.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(url_query_escape_0) {
    escape(b, url_query_escape, unescaped[0]);
}

BENCH(url_path_escape_0) {
    escape(b, url_path_escape, unescaped[0]);
}

BENCH(url_query_unescape_0) {
    unescape(b, url_query_unescape, queried[0]);
}

BENCH(url_path_unescape_0) {
    unescape(b, url_path_unescape, pathed[0]);
}

BENCH(url_query_escape_1) {
    escape(b, url_query_escape, unescaped[1]);
}

BENCH(url_path_escape_1) {
    escape(b, url_path_escape, unescaped[1]);
}

BENCH(url_query_unescape_1) {
    unescape(b, url_query_unescape, queried[1]);
}

BENCH(url_path_unescape_1) {
    unescape(b, url_path_unescape, pathed[1]);
}

BENCH(url_query_escape_2) {
    escape(b, url_query_escape, unescaped[2]);
}

BENCH(url_path_escape_2) {
    escape(b, url_path_escape, unescaped[2]);
}

BENCH(url_query_unescape_2) {
    unescape(b, url_query_unescape, queried[2]);
}

BENCH(url_path_unescape_2) {
    unescape(b, url_path_unescape, pathed[2]);
}

BENCH(url_query_escape_3) {
    escape(b, url_query_escape, unescaped[3]);
}

BENCH(url_path_escape_3) {
    escape(b, url_path_escape, unescaped[3]);
}

BENCH(url_query_unescape_3) {
    unescape(b, url_query_unescape, queried[3]);
}

BENCH(url_path_unescape_3) {
    unescape(b, url_path_unescape, pathed[3]);
}

BENCH(url_query_escape_4) {
    escape(b, url_query_escape, unescaped[4]);
}

BENCH(url_path_escape_4) {
    escape(b, url_path_escape, unescaped[4]);
}

BENCH(url_query_unescape_4) {
    unescape(b, url_query_unescape, queried[4]);
}

BENCH(url_path_unescape_4) {
    unescape(b, url_path_unescape, pathed[4]);
}

static void parse(Bench *b, const char *in) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str s = cstr(in);
    BENCH_LOOP(b) {
        Error err;
        Url *u = url_parse(a, s, &err);
        bench_keep(u);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

static void string(Bench *b, const char *in) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Url *u = url_parse(heap_allocator(), cstr(in), NULL);
    BENCH_LOOP(b) {
        Str r = url_string(u, a);
        bench_keep(r.p);
        arena_reset(&ar);
    }
    url_free(heap_allocator(), u);
    arena_free(&ar);
}

BENCH(url_parse_full) {
    parse(b, urls[0]);
}

BENCH(url_parse_plain) {
    parse(b, urls[1]);
}

BENCH(url_string_full) {
    string(b, urls[0]);
}

BENCH(url_string_plain) {
    string(b, urls[1]);
}

/* RFC 3986's example base and one of its dot-segment references. */
BENCH(url_resolve_reference) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Url *base = url_parse(heap_allocator(), cstr("http://a/b/c/d;p?q"), NULL);
    Url *ref = url_parse(heap_allocator(), cstr("../../g"), NULL);
    BENCH_LOOP(b) {
        Url *u = url_resolve_reference(base, a, ref);
        bench_keep(u);
        arena_reset(&ar);
    }
    url_free(heap_allocator(), ref);
    url_free(heap_allocator(), base);
    arena_free(&ar);
}

/* Go's encodeQueryTests row with two keys, and the query parse that undoes
 * it. The values are built once outside the loop, in both languages. */
BENCH(url_encode_query) {
    Arena keep;
    arena_init(&keep, NULL, 0);
    UrlValues v = url_values_make(arena_allocator(&keep));
    url_values_add(v, cstr("q"), cstr("puppies"));
    url_values_add(v, cstr("oe"), cstr("utf8"));
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Str r = url_values_encode(v, a);
        bench_keep(r.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
    arena_free(&keep);
}

BENCH(url_parse_query) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str q = cstr("oe=utf8&q=puppies&tag=a&tag=b&page=2");
    BENCH_LOOP(b) {
        Error err;
        UrlValues v = url_parse_query(a, q, &err);
        bench_keep(v);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

void register_url_benchmarks(void);

void register_url_benchmarks(void) {
    BENCH_RUN(url_query_escape_0);
    BENCH_RUN(url_path_escape_0);
    BENCH_RUN(url_query_unescape_0);
    BENCH_RUN(url_path_unescape_0);
    BENCH_RUN(url_query_escape_1);
    BENCH_RUN(url_path_escape_1);
    BENCH_RUN(url_query_unescape_1);
    BENCH_RUN(url_path_unescape_1);
    BENCH_RUN(url_query_escape_2);
    BENCH_RUN(url_path_escape_2);
    BENCH_RUN(url_query_unescape_2);
    BENCH_RUN(url_path_unescape_2);
    BENCH_RUN(url_query_escape_3);
    BENCH_RUN(url_path_escape_3);
    BENCH_RUN(url_query_unescape_3);
    BENCH_RUN(url_path_unescape_3);
    BENCH_RUN(url_query_escape_4);
    BENCH_RUN(url_path_escape_4);
    BENCH_RUN(url_query_unescape_4);
    BENCH_RUN(url_path_unescape_4);
    BENCH_RUN(url_parse_full);
    BENCH_RUN(url_parse_plain);
    BENCH_RUN(url_string_full);
    BENCH_RUN(url_string_plain);
    BENCH_RUN(url_resolve_reference);
    BENCH_RUN(url_encode_query);
    BENCH_RUN(url_parse_query);
}
