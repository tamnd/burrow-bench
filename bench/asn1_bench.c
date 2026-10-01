/* encoding/asn1 on one value both sides build the same way: a list of 100
 * records shaped like a small certificate, each with a serial number, an
 * algorithm OID, a UTF8String name, a validity period and 64 bytes of key.
 *
 * asn1_marshal writes the list to DER every round, into a heap buffer that is
 * freed straight after. asn1_unmarshal reads the DER asn1_marshal writes back
 * into a fresh value, from an arena on the C side that is reset every round.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/declare.h"
#include "burrow/encoding/asn1.h"
#include "burrow/fmt.h"
#include "burrow/mem/arena.h"
#include "burrow/mem/heap.h"
#include "burrow/slice.h"
#include "burrow/time.h"

#include <stdlib.h>

#define AS_N 100

#define AS_VALIDITY_FIELDS(F, T)                                                       \
    F(T, Time, NotBefore, "")                                                          \
    F(T, Time, NotAfter, "")
BURROW_STRUCT(AsValidity, AS_VALIDITY_FIELDS);

#define AS_RECORD_FIELDS(F, T)                                                         \
    F(T, Int, Serial, "")                                                              \
    F(T, Asn1ObjectIdentifier, Algorithm, "")                                          \
    F(T, Str, Name, "asn1:\"utf8\"")                                                   \
    F(T, AsValidity, Validity, "")                                                     \
    F(T, Bytes, Key, "")
BURROW_STRUCT(AsRecord, AS_RECORD_FIELDS);

BURROW_SLICE_TYPE(AsRecords, AsRecord);

static Arena list_arena;
static AsRecords list;
static Slice der;

/* The same list asn1_test.go builds. */
static void build_list(void) {
    if (list.len > 0)
        return;
    arena_init(&list_arena, NULL, 0);
    Alloc *a = arena_allocator(&list_arena);
    list = slice_make(a, TYPE_OF(AsRecord), AS_N, AS_N);
    AsRecord *rs = list.p;
    for (int i = 0; i < AS_N; i++) {
        rs[i].Serial = 1000003 * (Int)i;
        rs[i].Algorithm = slice_make(a, TYPE_INT, 7, 7);
        static const Int oid[7] = {1, 2, 840, 113549, 1, 1, 11};
        for (int j = 0; j < 7; j++)
            ((Int *)rs[i].Algorithm.p)[j] = oid[j];
        rs[i].Name = fmt_sprintf_v(a, "host-%d.example.com", i);
        rs[i].Validity.NotBefore = time_date(2026, TIME_JANUARY, 1 + i % 28, 0, 0, 0, 0, time_utc_loc);
        rs[i].Validity.NotAfter = time_date(2027, TIME_JANUARY, 1 + i % 28, 0, 0, 0, 0, time_utc_loc);
        rs[i].Key = slice_make(a, TYPE_BYTE, 64, 64);
        for (int j = 0; j < 64; j++)
            ((Byte *)rs[i].Key.p)[j] = (Byte)(i * 31 + j);
    }
    Error err = BURROW_NO_ERROR;
    der = asn1_marshal(a, BURROW_ANY(TYPE_OF(AsRecords), &list), &err);
    if (BURROW_FAILED(err))
        abort();
}

BENCH(asn1_marshal) {
    build_list();
    BENCH_LOOP(b) {
        Error err = BURROW_NO_ERROR;
        Slice out = asn1_marshal(heap_allocator(), BURROW_ANY(TYPE_OF(AsRecords), &list), &err);
        if (BURROW_FAILED(err))
            abort();
        bench_keep_u64((uint64_t)out.len);
        mem_free(heap_allocator(), out.p, (size_t)out.cap, 1);
    }
}

BENCH(asn1_unmarshal) {
    build_list();
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    BENCH_LOOP(b) {
        AsRecords out = slice_nil(TYPE_OF(AsRecord));
        Error err = BURROW_NO_ERROR;
        asn1_unmarshal(a, der, BURROW_ANY(TYPE_OF(AsRecords), &out), &err);
        if (BURROW_FAILED(err))
            abort();
        bench_keep_u64((uint64_t)out.len);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

void register_asn1_benchmarks(void);

void register_asn1_benchmarks(void) {
    BENCH_RUN(asn1_marshal);
    BENCH_RUN(asn1_unmarshal);
}
