/* encoding/json/jsontext and encoding/json/v2, on one document both sides
 * build the same way: an array of 100 small objects, about 8KB.
 *
 * The jsontext rows check it, compact an indented copy of it, and read it a
 * token at a time. The v2 rows marshal the same data from a slice of structs
 * and unmarshal it back into one, and into an Any the way Go does into an
 * any. Everything a row makes comes from an arena that is reset every time
 * round.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/bytes.h"
#include "burrow/core.h"
#include "burrow/declare.h"
#include "burrow/encoding/json/jsontext.h"
#include "burrow/encoding/json/v2.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"

#include <stdio.h>
#include <string.h>

BURROW_SLICE_TYPE(JbTags, Str);

#define JB_ITEM_FIELDS(F, T)                                                           \
    F(T, Int, ID, "json:\"id\"")                                                       \
    F(T, Str, Name, "json:\"name\"")                                                   \
    F(T, double, Price, "json:\"price\"")                                              \
    F(T, JbTags, Tags, "json:\"tags\"")                                                \
    F(T, bool, OK, "json:\"ok\"")
BURROW_STRUCT(JbItem, JB_ITEM_FIELDS);

BURROW_SLICE_TYPE(JbItems, JbItem);

#define JB_N 100

static char doc_buf[16 << 10];
static Slice doc;

/* The same text json_test.go builds. Prices are quarters so that printing
 * them is exact on both sides. */
static void build_doc(void) {
    if (doc.len > 0)
        return;
    int n = 0;
    n += snprintf(doc_buf + n, sizeof doc_buf - (size_t)n, "[");
    for (int i = 0; i < JB_N; i++) {
        n += snprintf(doc_buf + n, sizeof doc_buf - (size_t)n,
                      "%s{\"id\":%d,\"name\":\"item %d\",\"price\":%d.%02d,"
                      "\"tags\":[\"tea\",\"green\",\"loose\"],\"ok\":%s}",
                      i > 0 ? "," : "", i, i, i / 4, (i % 4) * 25,
                      i % 3 == 0 ? "true" : "false");
    }
    n += snprintf(doc_buf + n, sizeof doc_buf - (size_t)n, "]");
    doc = (Slice){doc_buf, n, n, NULL};
}

BENCH(json_valid) {
    build_doc();
    BENCH_LOOP(b) {
        bench_keep_u64(jsontext_value_is_valid(doc, (Slice){0}));
    }
}

BENCH(json_compact) {
    build_doc();
    Alloc *h = heap_allocator();
    JsontextValue ind = jsontext_value_clone(doc, h);
    (void)jsontext_value_indent_v(&ind, h, 0);
    Byte *buf = mem_alloc(h, (size_t)ind.len, 1);
    BENCH_LOOP(b) {
        memcpy(buf, ind.p, (size_t)ind.len);
        JsontextValue v = {buf, ind.len, ind.len, NULL};
        (void)jsontext_value_compact_v(&v, h, 0);
        bench_keep(v.p);
    }
    mem_free(h, buf, (size_t)ind.len, 1);
}

BENCH(json_read_tokens) {
    build_doc();
    BytesReader r;
    bytes_reader_reset(&r, doc);
    JsontextDecoder *d = jsontext_new_decoder_v(heap_allocator(),
                                                bytes_reader_as_io_reader(&r), 0);
    BENCH_LOOP(b) {
        bytes_reader_reset(&r, doc);
        jsontext_decoder_reset_v(d, bytes_reader_as_io_reader(&r), 0);
        Error err = BURROW_NO_ERROR;
        uint64_t n = 0;
        for (;;) {
            JsontextToken t = jsontext_decoder_read_token(d, &err);
            if (BURROW_FAILED(err))
                break;
            n += (uint64_t)jsontext_token_kind(t);
        }
        bench_keep_u64(n);
    }
    jsontext_decoder_free(d);
}

BENCH(json_marshal_struct) {
    build_doc();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    JbItems items = {0};
    Error err = jsonv2_unmarshal_v(heap_allocator(), doc,
                                   BURROW_ANY(TYPE_OF(JbItems), &items), 0);
    if (BURROW_FAILED(err))
        return;
    BENCH_LOOP(b) {
        Slice out = jsonv2_marshal_v(a, BURROW_ANY(TYPE_OF(JbItems), &items), NULL, 0);
        bench_keep(out.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(json_unmarshal_struct) {
    build_doc();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        JbItems items = {0};
        Error err = jsonv2_unmarshal_v(a, doc, BURROW_ANY(TYPE_OF(JbItems), &items), 0);
        bench_keep_u64((uint64_t)BURROW_FAILED(err));
        bench_keep(items.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(json_unmarshal_any) {
    build_doc();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Any v = {NULL, NULL};
        Error err = jsonv2_unmarshal_v(a, doc, BURROW_ANY(TYPE_ANY, &v), 0);
        bench_keep_u64((uint64_t)BURROW_FAILED(err));
        bench_keep(v.data);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

void register_json_benchmarks(void);

void register_json_benchmarks(void) {
    BENCH_RUN(json_valid);
    BENCH_RUN(json_compact);
    BENCH_RUN(json_read_tokens);
    BENCH_RUN(json_marshal_struct);
    BENCH_RUN(json_unmarshal_struct);
    BENCH_RUN(json_unmarshal_any);
}
