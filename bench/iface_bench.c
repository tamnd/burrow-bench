/* Interface dispatch against a direct call and against Go's.
 *
 * The claim burrow makes about interfaces is that they cost what Go's cost: a
 * load and an indirect call, no lookup, no allocation. This file is that claim
 * with numbers under it.
 *
 * The one place the two languages genuinely differ is inlining. Go's compiler
 * will sometimes prove the dynamic type at a call site and inline straight
 * through the interface, and C's cannot, so a dispatch here is always indirect.
 * That is why the direct call is measured next to the interface call rather
 * than only against Go. The gap between those two rows is the whole cost of
 * the design, and it is the number to watch when somebody proposes changing it.
 *
 * The Any benchmarks are a less even comparison and the uneven part is worth
 * saying out loud. Go boxes a small value into the interface word or onto the
 * heap depending on what it is, invisibly, and caches the first few hundred
 * small integers so that some of those boxes are free. Any always points at
 * something, so making one from a pointer you already have is two moves and
 * making one that outlives the block is an arena allocation. Read the
 * allocation columns rather than the time columns on those rows.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/error.h"
#include "burrow/iface.h"
#include "burrow/io.h"
#include "burrow/mem.h"
#include "burrow/mem/arena.h"
#include "burrow/slice.h"
#include "burrow/type.h"

static void report_arena(Bench *b, Arena *ar) {
    AllocStats st = mem_stats(arena_allocator(ar));
    bench_report_allocs(b, st.bytes_total, st.allocs);
}

/* ------------------------------------------------------- an interface to call
 *
 * One method taking one argument and returning one word, which is the smallest
 * thing that still measures a real call. A Reader would work too and is what
 * the library ships, but its argument is a slice and its body has bounds
 * checks in it, and then the number is about those instead. */

typedef struct AdderVT {
    const Type *self_type;
    Int (*add)(void *self, Int d);
} AdderVT;

typedef struct Adder {
    const AdderVT *vt;
    void *data;
} Adder;

typedef struct Counter {
    Int n;
} Counter;

static const Type counter_type = {
    {(const Byte *)"Counter", 7},
    {(const Byte *)"ifacebench", 10},
    KIND_STRUCT,
    (uint32_t)sizeof(Counter),
    (uint16_t)_Alignof(Counter),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x636e7472u,
    NULL,
};

static Int counter_add(void *self, Int d) {
    Counter *c = (Counter *)self;
    c->n += d;
    return c->n;
}

static const AdderVT counter_adder_vt = {&counter_type, counter_add};

static Adder counter_as_adder(Counter *c) {
    Adder v = {&counter_adder_vt, c};
    return v;
}

/* A second implementation, so that a call site can be made to see more than one
 * dynamic type. Doubling rather than adding, so that the two are not the same
 * function with two names and the branch predictor has something to be wrong
 * about. */

typedef struct Doubler {
    Int n;
} Doubler;

static const Type doubler_type = {
    {(const Byte *)"Doubler", 7},
    {(const Byte *)"ifacebench", 10},
    KIND_STRUCT,
    (uint32_t)sizeof(Doubler),
    (uint16_t)_Alignof(Doubler),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x64626c72u,
    NULL,
};

static Int doubler_add(void *self, Int d) {
    Doubler *p = (Doubler *)self;
    p->n += d * 2;
    return p->n;
}

static const AdderVT doubler_adder_vt = {&doubler_type, doubler_add};

static Adder doubler_as_adder(Doubler *p) {
    Adder v = {&doubler_adder_vt, p};
    return v;
}

/* ------------------------------------------------------------- the dispatch */

/* The call the whole design is about. A load of the function pointer and an
 * indirect call, once per iteration.
 *
 * The vtable goes through bench_hide first, and without that this benchmark
 * measures nothing at all. clang can see the vtable here is a constant, so it
 * turns the indirect call into a direct one, inlines counter_add, and leaves a
 * loop that adds one to a register. Go's side hides the same thing by reading
 * the value out of a package level variable. */
BENCH(iface_call) {
    Counter c = {0};
    Adder v = counter_as_adder(&c);

    v.vt = (const AdderVT *)bench_hide(v.vt);

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)BURROW_CALL(v, add, 1));
    }
}

/* The same work without the interface, which on both sides is a call the
 * compiler can see through and inline. The gap against iface_call is what
 * dispatch costs, and it is the only row here that needs no explanation. */
BENCH(direct_call) {
    Counter c = {0};

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)counter_add(&c, 1));
    }
}

/* Two dynamic types at one call site, alternating every iteration, which is
 * what a real program's interface calls look like and what iface_call above
 * deliberately is not.
 *
 * It comes out level with iface_call on both sides, and that is the finding
 * rather than a problem with the benchmark. An alternation of two targets is
 * something any indirect branch predictor built in the last fifteen years gets
 * right every time. A call site that sees a dozen types in no particular order
 * would cost more, and neither language has a way to make that cheap. */
BENCH(iface_call_two_types) {
    Counter c = {0};
    Doubler d = {0};
    Adder vs[2];

    vs[0] = counter_as_adder(&c);
    vs[1] = doubler_as_adder(&d);
    vs[0].vt = (const AdderVT *)bench_hide(vs[0].vt);
    vs[1].vt = (const AdderVT *)bench_hide(vs[1].vt);

    BENCH_LOOP(b) {
        Adder v = vs[bench_i_ & 1];
        bench_keep_u64((uint64_t)BURROW_CALL(v, add, 1));
    }
}

/* Making an interface value out of a concrete pointer, which is two moves here
 * and two moves in Go whenever the compiler knows both types, which is almost
 * always.
 *
 * The pointer alternates between two receivers so that the pair has to be built
 * every iteration rather than hoisted out of the loop, which is what happens to
 * a conversion with nothing changing in it. Both sides do the same. */
BENCH(iface_convert) {
    static Counter cs[2];
    Counter *base = (Counter *)bench_hide(cs);

    BENCH_LOOP(b) {
        Adder v = counter_as_adder(&base[bench_i_ & 1]);
        bench_keep(v.data);
    }
}

/* ------------------------------------------------------------ the embedding */

/* A type that is both halves of io.ReadWriter, so that narrowing has something
 * real to narrow. Neither method runs in the benchmarks below, they are here
 * because a vtable needs filling in. */

typedef struct Pipe {
    Int pos;
} Pipe;

static const Type pipe_type = {
    {(const Byte *)"Pipe", 4},
    {(const Byte *)"ifacebench", 10},
    KIND_STRUCT,
    (uint32_t)sizeof(Pipe),
    (uint16_t)_Alignof(Pipe),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x70697065u,
    NULL,
};

static Int pipe_read(void *self, Slice p, Error *err) {
    (void)self;
    (void)p;
    *err = io_eof;
    return 0;
}

static Int pipe_write(void *self, Slice p, Error *err) {
    Pipe *q = (Pipe *)self;
    (void)err;
    q->pos += p.len;
    return p.len;
}

static const IoReadWriterVT pipe_read_writer_vt = {
    {&pipe_type, pipe_read},
    {&pipe_type, pipe_write},
};

static IoReadWriter pipe_as_io_read_writer(Pipe *q) {
    IoReadWriter rw = {&pipe_read_writer_vt, q};
    return rw;
}

/* Narrowing an embedded interface, which is the address of a member and a
 * pointer copy.
 *
 * This is the row where burrow should win and the reason is worth knowing. Go
 * converting an io.ReadWriter to an io.Reader goes through runtime.convI2I,
 * which looks the method set up in a cache, because the itab for the narrower
 * interface is a different object and has to be found. Here the vtable for the
 * narrower interface is already inside the wider one, so there is nothing to
 * look up. */
BENCH(iface_narrow) {
    Pipe q = {0};
    IoReadWriter rw = pipe_as_io_read_writer(&q);

    rw.vt = (const IoReadWriterVT *)bench_hide(rw.vt);

    BENCH_LOOP(b) {
        IoReader r = io_read_writer_as_io_reader(rw);
        bench_keep(r.vt);
    }
}

/* ------------------------------------------------------------ the assertions */

/* Go's v.(*T) when the answer is yes. One load of the descriptor out of the
 * vtable and one pointer comparison. */
BENCH(iface_assert_hit) {
    Counter c = {0};
    Adder v = counter_as_adder(&c);

    v.vt = (const AdderVT *)bench_hide(v.vt);

    BENCH_LOOP(b) {
        bench_keep(iface_assert(BURROW_IFACE(v), &counter_type));
    }
}

/* The same when the answer is no, which is the answer a type switch gets on
 * every arm but one. Identical work, other branch. */
BENCH(iface_assert_miss) {
    Counter c = {0};
    Adder v = counter_as_adder(&c);

    v.vt = (const AdderVT *)bench_hide(v.vt);

    BENCH_LOOP(b) {
        bench_keep(iface_assert(BURROW_IFACE(v), &doubler_type));
    }
}

/* Reading the dynamic type without asserting anything, which is the first step
 * of a type switch and the whole of reflect.TypeOf. */
BENCH(iface_type) {
    Counter c = {0};
    Adder v = counter_as_adder(&c);

    v.vt = (const AdderVT *)bench_hide(v.vt);

    BENCH_LOOP(b) {
        bench_keep(iface_type(BURROW_IFACE(v)));
    }
}

/* -------------------------------------------------------------------- Any */

/* Making an Any out of a pointer that already exists, which is what every
 * argument to a print does. Two moves, no allocation, and Go's is the same
 * when the value is already a pointer. */
BENCH(any_make) {
    static Int ns[2];
    Int *base = (Int *)bench_hide(ns);

    BENCH_LOOP(b) {
        Any v = BURROW_ANY(TYPE_INT, &base[bench_i_ & 1]);
        bench_keep(v.data);
    }
}

/* The one that allocates, because the value has to outlive the block it was
 * made in. Go's equivalent is assigning an int to an any and letting it escape,
 * which allocates eight bytes on the heap unless the value is small enough to
 * hit the runtime's cache of little integers, so both sides here use a number
 * past that cache.
 *
 * The value changes every iteration on both sides, because a value that does
 * not lets Go's compiler hoist the box out of the loop and then that row
 * reports zero allocations and means nothing.
 *
 * The arena resets once per iteration, and Go hands its garbage to a collector
 * that runs later on another thread, so the time columns are two different
 * experiments. The allocation columns are the same experiment. */
BENCH(any_box) {
    Arena ar;
    Alloc *a;
    Int n = 1234;
    Any src;

    arena_init(&ar, NULL, 0);
    a = arena_allocator(&ar);
    src = BURROW_ANY(TYPE_INT, &n);

    BENCH_LOOP(b) {
        Any v;
        n = 1234 + (Int)bench_i_;
        v = any_box(a, src);
        bench_keep(v.data);
        arena_reset(&ar);
    }

    report_arena(b, &ar);
    arena_free(&ar);
}

/* Getting the value back out, which is the same load and compare as
 * iface_assert with a descriptor in place of a vtable. */
BENCH(any_assert_hit) {
    Int n = 1234;
    Any v = BURROW_ANY(TYPE_INT, &n);

    BENCH_LOOP(b) {
        bench_keep(any_assert(v, TYPE_INT));
    }
}

/* The miss, which is what the arms of a type switch cost before the right one
 * matches. */
BENCH(any_assert_miss) {
    Int n = 1234;
    Any v = BURROW_ANY(TYPE_INT, &n);

    BENCH_LOOP(b) {
        bench_keep(any_assert(v, TYPE_INT64));
    }
}

/* Go's == on two interface values holding the same type, which is a descriptor
 * comparison and then a comparison through the descriptor. This is what a
 * map[any]V pays on every lookup once the hash has found the slot. */
BENCH(any_equal_hit) {
    Int x = 1234;
    Int y = 1234;
    Any a1 = BURROW_ANY(TYPE_INT, &x);
    Any a2 = BURROW_ANY(TYPE_INT, &y);

    BENCH_LOOP(b) {
        bench_keep_u64(any_equal(a1, a2) ? 1u : 0u);
    }
}

/* The same number stored as two different types, which Go says is not equal
 * and answers from the descriptors without looking at either value. The gap
 * against any_equal_hit is what the value comparison costs. */
BENCH(any_equal_type_miss) {
    Int x = 1234;
    int64_t y = 1234;
    Any a1 = BURROW_ANY(TYPE_INT, &x);
    Any a2 = BURROW_ANY(TYPE_INT64, &y);

    BENCH_LOOP(b) {
        bench_keep_u64(any_equal(a1, a2) ? 1u : 0u);
    }
}

/* ------------------------------------------------------------------- io
 *
 * Dispatch in the place it actually happens, which is a loop that calls through
 * an interface once per chunk of a stream.
 *
 * io_copy_buffer and Go's io.CopyBuffer are used rather than the plain Copy on
 * either side, so that neither is measuring an allocation the other does not
 * make. The reader hands out a fixed chunk at a time and the writer counts,
 * so both sides do one read call and one write call per chunk and nothing
 * else. Neither implements WriterTo or ReaderFrom, which matters because Go's
 * Copy takes a completely different path when either is present. */

enum { STREAM_BYTES = 64 * 1024, STREAM_CHUNK = 512, COPY_BUF = 32 * 1024 };

typedef struct Source {
    Int pos;
    Int len;
} Source;

static const Type source_type = {
    {(const Byte *)"Source", 6},
    {(const Byte *)"ifacebench", 10},
    KIND_STRUCT,
    (uint32_t)sizeof(Source),
    (uint16_t)_Alignof(Source),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x73726373u,
    NULL,
};

static Int source_read(void *self, Slice p, Error *err) {
    Source *s = (Source *)self;
    Int left = s->len - s->pos;
    Int n;

    if (left <= 0) {
        *err = io_eof;
        return 0;
    }
    n = left < p.len ? left : p.len;
    if (n > STREAM_CHUNK)
        n = STREAM_CHUNK;
    s->pos += n;
    return n;
}

static const IoReaderVT source_reader_vt = {&source_type, source_read};

typedef struct Sink {
    Int total;
} Sink;

static const Type sink_type = {
    {(const Byte *)"Sink", 4},
    {(const Byte *)"ifacebench", 10},
    KIND_STRUCT,
    (uint32_t)sizeof(Sink),
    (uint16_t)_Alignof(Sink),
    0,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0x73696e6bu,
    NULL,
};

static Int sink_write(void *self, Slice p, Error *err) {
    Sink *s = (Sink *)self;
    (void)err;
    s->total += p.len;
    return p.len;
}

static const IoWriterVT sink_writer_vt = {&sink_type, sink_write};

/* Sixty four kilobytes through a five hundred and twelve byte reader, so a
 * hundred and twenty eight trips round the loop and two hundred and fifty six
 * interface calls per iteration. Neither side touches the bytes, because
 * copying them is a memcpy benchmark and there is already one of those. */
BENCH(io_copy_buffer_64k) {
    static Byte buf[COPY_BUF];
    Source src = {0, STREAM_BYTES};
    Sink dst = {0};
    IoReader r;
    IoWriter w;
    Slice p;

    r.vt = &source_reader_vt;
    r.data = &src;
    w.vt = &sink_writer_vt;
    w.data = &dst;
    p = slice_from(buf, (Int)COPY_BUF, (Int)COPY_BUF, TYPE_BYTE);

    BENCH_LOOP(b) {
        Error err = BURROW_NO_ERROR;
        src.pos = 0;
        bench_keep_u64((uint64_t)io_copy_buffer(w, r, p, &err));
    }

    bench_keep_u64((uint64_t)dst.total);
}

/* io.ReadFull into a buffer the reader cannot fill in one call, which is the
 * shape of every header parse there has ever been. Four kilobytes at five
 * hundred and twelve bytes a call is eight reads and one loop. */
BENCH(io_read_full_4k) {
    static Byte buf[4096];
    Source src = {0, STREAM_BYTES};
    IoReader r;
    Slice p;

    r.vt = &source_reader_vt;
    r.data = &src;
    p = slice_from(buf, 4096, 4096, TYPE_BYTE);

    BENCH_LOOP(b) {
        Error err = BURROW_NO_ERROR;
        src.pos = 0;
        bench_keep_u64((uint64_t)io_read_full(r, p, &err));
    }
}

void register_iface_benchmarks(void);

void register_iface_benchmarks(void) {
    BENCH_RUN(iface_call);
    BENCH_RUN(direct_call);
    BENCH_RUN(iface_call_two_types);
    BENCH_RUN(iface_convert);
    BENCH_RUN(iface_narrow);
    BENCH_RUN(iface_assert_hit);
    BENCH_RUN(iface_assert_miss);
    BENCH_RUN(iface_type);
    BENCH_RUN(any_make);
    BENCH_RUN(any_box);
    BENCH_RUN(any_assert_hit);
    BENCH_RUN(any_assert_miss);
    BENCH_RUN(any_equal_hit);
    BENCH_RUN(any_equal_type_miss);
    BENCH_RUN(io_copy_buffer_64k);
    BENCH_RUN(io_read_full_4k);
}
