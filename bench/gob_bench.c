/* encoding/gob on one value both sides build the same way: a feed of 100
 * entries, each with an id, a title, two tags and a score.
 *
 * gob_encode makes a new encoder each time round and writes the feed to a
 * discard writer, so every round sends the type descriptions as well as the
 * value. gob_encode_again keeps one encoder, so after the first round it is
 * the value alone. gob_decode reads the stream gob_encode writes with a new
 * decoder each time, into an arena on the C side that is reset every round.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/bytes.h"
#include "burrow/core.h"
#include "burrow/declare.h"
#include "burrow/encoding/gob.h"
#include "burrow/fmt.h"
#include "burrow/io.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/slice.h"

#include <stdlib.h>

#define GB_N 100

BURROW_SLICE_TYPE(GbTags, Str);

#define GB_ENTRY_FIELDS(F, T)                                                          \
    F(T, Int, ID, "")                                                                  \
    F(T, Str, Title, "")                                                               \
    F(T, GbTags, Tags, "")                                                             \
    F(T, double, Score, "")
BURROW_STRUCT(GbEntry, GB_ENTRY_FIELDS);

BURROW_SLICE_TYPE(GbEntries, GbEntry);

#define GB_FEED_FIELDS(F, T) F(T, GbEntries, Entries, "")
BURROW_STRUCT(GbFeed, GB_FEED_FIELDS);

static Arena feed_arena;
static GbFeed feed;
static Slice stream;

/* The same feed gob_test.go builds. */
static void build_feed(void) {
    if (feed.Entries.len > 0)
        return;
    arena_init(&feed_arena, NULL, 0);
    Alloc *a = arena_allocator(&feed_arena);
    feed.Entries = slice_make(a, TYPE_OF(GbEntry), GB_N, GB_N);
    GbEntry *es = feed.Entries.p;
    for (int i = 0; i < GB_N; i++) {
        es[i].ID = i;
        es[i].Title = fmt_sprintf_v(a, "Item %d and friends", i);
        es[i].Tags = slice_make(a, TYPE_STRING, 2, 2);
        Str *ts = es[i].Tags.p;
        ts[0] = BURROW_S("tea");
        ts[1] = fmt_sprintf_v(a, "batch-%d", i % 7);
        es[i].Score = (double)i * 1.25;
    }
    BytesBuffer buf = BYTES_BUFFER(a);
    GobEncoder *e = gob_new_encoder(a, bytes_buffer_as_io_writer(&buf));
    Error err = gob_encoder_encode(e, BURROW_ANY(TYPE_OF(GbFeed), &feed));
    gob_encoder_free(e);
    if (BURROW_FAILED(err))
        abort();
    stream = bytes_buffer_bytes(&buf);
}

/* Writes and forgets, so the rows measure the encoder and not a buffer. */
static Int gb_discard_write(void *self, Slice p, Error *err) {
    (void)self;
    *err = BURROW_NO_ERROR;
    return p.len;
}

static const IoWriterVT gb_discard_vt = {NULL, gb_discard_write};

BENCH(gob_encode) {
    build_feed();
    BENCH_LOOP(b) {
        GobEncoder *e = gob_new_encoder(heap_allocator(), (IoWriter){&gb_discard_vt, NULL});
        Error err = gob_encoder_encode(e, BURROW_ANY(TYPE_OF(GbFeed), &feed));
        bench_keep_u64((uint64_t)BURROW_FAILED(err));
        gob_encoder_free(e);
    }
}

BENCH(gob_encode_again) {
    build_feed();
    GobEncoder *e = gob_new_encoder(heap_allocator(), (IoWriter){&gb_discard_vt, NULL});
    BENCH_LOOP(b) {
        Error err = gob_encoder_encode(e, BURROW_ANY(TYPE_OF(GbFeed), &feed));
        bench_keep_u64((uint64_t)BURROW_FAILED(err));
    }
    gob_encoder_free(e);
}

BENCH(gob_decode) {
    build_feed();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BytesReader r;
    BENCH_LOOP(b) {
        bytes_reader_reset(&r, stream);
        GobDecoder *d = gob_new_decoder(a, bytes_reader_as_io_reader(&r));
        GbFeed out = {0};
        Error err = gob_decoder_decode(d, BURROW_ANY(TYPE_OF(GbFeed), &out));
        if (BURROW_FAILED(err))
            abort();
        bench_keep_u64((uint64_t)out.Entries.len);
        gob_decoder_free(d);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

void register_gob_benchmarks(void);

void register_gob_benchmarks(void) {
    BENCH_RUN(gob_encode);
    BENCH_RUN(gob_encode_again);
    BENCH_RUN(gob_decode);
}
