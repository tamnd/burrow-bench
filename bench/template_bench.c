/* text/template, on Go's BenchmarkParseLarge text, and executing a range over
 * a list of 100 records, which Go has no benchmark for.
 *
 * Parsing goes through text/template rather than straight to the parse
 * package, so the row includes making the template and adding the tree to it,
 * on both sides. Every template is made in an arena that is reset every time
 * round.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/declare.h"
#include "burrow/fmt.h"
#include "burrow/io.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/slice.h"
#include "burrow/strings.h"
#include "burrow/text/template.h"

#include <stdlib.h>

#define TB_N 100

#define TB_ITEM_FIELDS(F, T)                                                           \
    F(T, Str, Name, "")                                                                \
    F(T, Int, Count, "")
BURROW_STRUCT(TbItem, TB_ITEM_FIELDS);

BURROW_SLICE_TYPE(TbItems, TbItem);

static Arena items_arena;
static TbItems items;

/* The same list go/template_test.go builds. */
static void build_items(void) {
    if (items.len > 0)
        return;
    arena_init(&items_arena, NULL, 0);
    Alloc *a = arena_allocator(&items_arena);
    items = slice_make(a, TYPE_OF(TbItem), TB_N, TB_N);
    TbItem *it = items.p;
    for (Int i = 0; i < TB_N; i++) {
        it[i].Name = fmt_sprintf_v(a, BURROW_S("item-%d"), i);
        it[i].Count = i * 7;
    }
}

BENCH(template_parse_large) {
    Arena text_ar;
    arena_init(&text_ar, NULL, 0);
    Str text = strings_repeat(arena_allocator(&text_ar), BURROW_S("{{1234}}\n"), 10000);
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Error err = BURROW_NO_ERROR;
        TextTemplate *t =
            text_template_parse(text_template_new(a, BURROW_S("bench")), text, &err);
        if (BURROW_FAILED(err))
            abort();
        bench_keep(t);
        arena_reset(&ar);
    }
    arena_free(&ar);
    arena_free(&text_ar);
}

BENCH(template_execute_range) {
    build_items();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Error err = BURROW_NO_ERROR;
    TextTemplate *t = text_template_parse(
        text_template_new(arena_allocator(&ar), BURROW_S("bench")),
        BURROW_S("{{range .}}<li>{{.Name}}: {{.Count}}</li>\n{{end}}"), &err);
    if (BURROW_FAILED(err))
        abort();
    Any data = BURROW_ANY(TYPE_OF(TbItems), &items);
    BENCH_LOOP(b) {
        err = text_template_execute(t, io_discard, data);
        if (BURROW_FAILED(err))
            abort();
    }
    arena_free(&ar);
}

void register_template_benchmarks(void);

void register_template_benchmarks(void) {
    BENCH_RUN(template_parse_large);
    BENCH_RUN(template_execute_range);
}
