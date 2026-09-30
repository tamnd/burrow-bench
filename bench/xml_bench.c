/* encoding/xml's token layer, on one document both sides build the same way:
 * a feed of 100 entries, each with attributes, a namespaced element, text with
 * an entity in it and an empty element, about 18KB.
 *
 * xml_raw_tokens reads it with RawToken, which neither checks the nesting nor
 * resolves namespaces. xml_tokens reads it with Token, which does both.
 * xml_encode_tokens writes the tokens Token gave back out through an Encoder,
 * from a list read once before the loop. burrow's tokens point into the
 * decoder, where Go's names are strings of their own, so the list is copied
 * into an arena on the C side the way Go keeps them without asking.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/bytes.h"
#include "burrow/core.h"
#include "burrow/encoding/xml.h"
#include "burrow/io.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"

#include <stdio.h>

#define XB_N 100

static char doc_buf[32 << 10];
static Slice doc;

/* The same text xml_test.go builds. */
static void build_doc(void) {
    if (doc.len > 0)
        return;
    int n = 0;
    n += snprintf(doc_buf + n, sizeof doc_buf - (size_t)n,
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                  "xmlns:media=\"http://search.yahoo.com/mrss/\">\n");
    for (int i = 0; i < XB_N; i++) {
        n += snprintf(doc_buf + n, sizeof doc_buf - (size_t)n,
                      "  <entry id=\"%d\" lang=\"en\">\n"
                      "    <title>Item %d &amp; friends</title>\n"
                      "    <media:thumbnail url=\"https://example.com/%d.jpg\" "
                      "width=\"120\"/>\n"
                      "    <summary>Green tea, loose leaf, %d grams.</summary>\n"
                      "  </entry>\n",
                      i, i, i, 50 + i);
    }
    n += snprintf(doc_buf + n, sizeof doc_buf - (size_t)n, "</feed>\n");
    doc = (Slice){doc_buf, n, n, NULL};
}

BENCH(xml_raw_tokens) {
    build_doc();
    BytesReader r;
    BENCH_LOOP(b) {
        bytes_reader_reset(&r, doc);
        XmlDecoder *d = xml_new_decoder(heap_allocator(), bytes_reader_as_io_reader(&r));
        Error err = BURROW_NO_ERROR;
        uint64_t n = 0;
        for (;;) {
            XmlToken t = xml_decoder_raw_token(d, &err);
            if (BURROW_FAILED(err))
                break;
            n += (uint64_t)t.kind;
        }
        bench_keep_u64(n);
        xml_decoder_free(d);
    }
}

BENCH(xml_tokens) {
    build_doc();
    BytesReader r;
    BENCH_LOOP(b) {
        bytes_reader_reset(&r, doc);
        XmlDecoder *d = xml_new_decoder(heap_allocator(), bytes_reader_as_io_reader(&r));
        Error err = BURROW_NO_ERROR;
        uint64_t n = 0;
        for (;;) {
            XmlToken t = xml_decoder_token(d, &err);
            if (BURROW_FAILED(err))
                break;
            n += (uint64_t)t.kind;
        }
        bench_keep_u64(n);
        xml_decoder_free(d);
    }
}

/* Writes and forgets, so the rows measure the encoder and not a buffer. */
static Int discard_write(void *self, Slice p, Error *err) {
    (void)self;
    *err = BURROW_NO_ERROR;
    return p.len;
}

static const IoWriterVT discard_vt = {NULL, discard_write};

BENCH(xml_encode_tokens) {
    build_doc();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    static XmlToken toks[2048];
    Int ntoks = 0;
    BytesReader r;
    bytes_reader_reset(&r, doc);
    XmlDecoder *d = xml_new_decoder(heap_allocator(), bytes_reader_as_io_reader(&r));
    for (;;) {
        Error err = BURROW_NO_ERROR;
        XmlToken t = xml_decoder_token(d, &err);
        if (BURROW_FAILED(err) || ntoks == 2048)
            break;
        toks[ntoks++] = xml_copy_token(a, t);
    }
    xml_decoder_free(d);
    BENCH_LOOP(b) {
        XmlEncoder *e = xml_new_encoder(heap_allocator(), (IoWriter){&discard_vt, NULL});
        for (Int i = 0; i < ntoks; i++)
            xml_encoder_encode_token(e, toks[i]);
        bench_keep_u64((uint64_t)BURROW_FAILED(xml_encoder_flush(e)));
        xml_encoder_free(e);
    }
    arena_free(&ar);
}

void register_xml_benchmarks(void);

void register_xml_benchmarks(void) {
    BENCH_RUN(xml_raw_tokens);
    BENCH_RUN(xml_tokens);
    BENCH_RUN(xml_encode_tokens);
}
