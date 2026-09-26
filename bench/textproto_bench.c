/* net/textproto, on Go's own BenchmarkReadMIMEHeader and BenchmarkUncommon.
 *
 * As in Go, one reader sits on a buffer that gets the headers written into it
 * again every time round, so the bufio reader and its buffer are reused and
 * what is measured is the header parse. The header comes from an arena that is
 * reset every time round, as the strings rows do.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/bufio.h"
#include "burrow/bytes.h"
#include "burrow/core.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/net/textproto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char client_headers[] =
    "Host: golang.org\r\n"
    "Connection: keep-alive\r\n"
    "Cache-Control: max-age=0\r\n"
    "Accept: application/xml,application/xhtml+xml,text/html;q=0.9,text/plain;q=0.8,"
    "image/png,*/*;q=0.5\r\n"
    "User-Agent: Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.3 (KHTML, "
    "like Gecko) Chrome/6.0.472.63 Safari/534.3\r\n"
    "Accept-Encoding: gzip,deflate,sdch\r\n"
    "Accept-Language: en-US,en;q=0.8,fr-CH;q=0.6\r\n"
    "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.3\r\n"
    "COOKIE: __utma=000000000.0000000000.0000000000.0000000000.0000000000.00; "
    "__utmb=000000000.0.00.0000000000; __utmc=000000000; "
    "__utmz=000000000.0000000000.00.0.utmcsr=code.google.com|utmccn=(referral)|utmcmd="
    "referral|utmcct=/p/go/issues/detail\r\n"
    "Non-Interned: test\r\n"
    "\r\n";

static const char server_headers[] = "Content-Type: text/html; charset=utf-8\r\n"
                                      "Content-Encoding: gzip\r\n"
                                      "Date: Thu, 27 Sep 2012 09:03:33 GMT\r\n"
                                      "Server: Google Frontend\r\n"
                                      "Cache-Control: private\r\n"
                                      "Content-Length: 2298\r\n"
                                      "VIA: 1.1 proxy.example.com:80 (XXX/n.n.n-nnn)\r\n"
                                      "Connection: Close\r\n"
                                      "Non-Interned: test\r\n"
                                      "\r\n";

static void read_headers(Bench *b, const char *text, size_t len) {
    Str s = {(const Byte *)text, (Int)len};
    Alloc *heap = heap_allocator();
    BytesBuffer buf = BYTES_BUFFER(heap);
    BufioReader *br = bufio_new_reader(heap, bytes_buffer_as_io_reader(&buf));
    TextprotoReader *r = textproto_new_reader(heap, br);
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        Error err;
        bytes_buffer_write_string(&buf, s, NULL);
        TextprotoMIMEHeader h = textproto_reader_read_mime_header(r, a, &err);
        if (BURROW_FAILED(err)) {
            fprintf(stderr, "textproto: ReadMIMEHeader failed\n");
            abort();
        }
        bench_keep(h);
        arena_reset(&ar);
    }
    arena_free(&ar);
    textproto_reader_free(r);
    bufio_reader_free(br);
    bytes_buffer_free(&buf);
}

BENCH(textproto_read_mime_header_client) {
    read_headers(b, client_headers, sizeof client_headers - 1);
}

BENCH(textproto_read_mime_header_server) {
    read_headers(b, server_headers, sizeof server_headers - 1);
}

BENCH(textproto_uncommon) {
    static const char uncommon[] = "uncommon-header-for-benchmark: foo\r\n\r\n";
    read_headers(b, uncommon, sizeof uncommon - 1);
}

BENCH(textproto_canonical_key) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    Str key = BURROW_S("x-forwarded-for");
    BENCH_LOOP(b) {
        Str s = textproto_canonical_mime_header_key(a, key);
        bench_keep(s.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

void register_textproto_benchmarks(void);

void register_textproto_benchmarks(void) {
    BENCH_RUN(textproto_read_mime_header_client);
    BENCH_RUN(textproto_read_mime_header_server);
    BENCH_RUN(textproto_uncommon);
    BENCH_RUN(textproto_canonical_key);
}
