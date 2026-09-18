/* Function values against a direct call and against Go's closures.
 *
 * A function value here is a function pointer and an environment pointer, so
 * calling one is a load and an indirect call, which is what calling a Go
 * closure is. These rows are that claim with numbers under it, and the row to
 * read first is the gap between func_call and func_call_direct, because that
 * gap is the entire cost of the design.
 *
 * Every dispatch row hides the function pointer from the optimiser first, and
 * without that they measure nothing. A function value built from a named
 * function in the same file is a constant the compiler can see, so it turns the
 * indirect call into a direct one and then inlines the body. Go's side hides
 * the same thing by reading the value out of a package level variable.
 *
 * Hiding it needs one more step than the interface benchmarks needed. bench_hide
 * takes and returns a void pointer, and converting a function pointer to a void
 * pointer is not something C guarantees, so what goes through it is the address
 * of a slot holding the whole value. Same effect, and it stays portable.
 *
 * One row is uneven and it is the interesting one. func_make builds a value and
 * lets it escape, which on this side is two stores and on Go's side is an
 * allocation, because a Go closure that captures anything and outlives its
 * frame goes on the heap. Read the allocation columns on that row rather than
 * the time column. It is the one place where writing the environment struct out
 * by hand, which is the part of this design that costs you, buys something
 * back.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/func.h"

/* ------------------------------------------------------- the types and targets
 *
 * One argument and one word back, which is the smallest shape that still
 * measures a real call, and the same shape the interface benchmarks use so that
 * the two files can be read against each other. */

BURROW_FUNC(Filter, Int, Int d);
BURROW_FUNC0(Tick, void);

static Int add_one(void *env, Int d) {
    (void)env;
    return d + 1;
}

/* A second target, so that a call site can be made to see more than one of
 * them. Doubling rather than adding, so the two are not the same function under
 * two names and the branch predictor has something to be wrong about. */
static Int double_it(void *env, Int d) {
    (void)env;
    return d * 2;
}

/* The shape a Go closure capturing a variable comes out as. The environment is
 * read on every call, which is the load Go's closure does out of its own
 * capture. */
typedef struct ScaleEnv {
    Int by;
} ScaleEnv;

static Int scale(void *env, Int d) {
    ScaleEnv *e = (ScaleEnv *)env;
    return d * e->by;
}

/* Capture by reference, which is what a Go closure that assigns to a captured
 * variable does. The store is the point of the row. */
typedef struct CountEnv {
    Int n;
} CountEnv;

static Int bump(void *env, Int d) {
    CountEnv *e = (CountEnv *)env;
    e->n += d;
    return e->n;
}

static void tick(void *env) {
    CountEnv *e = (CountEnv *)env;
    e->n++;
}

/* Puts a value somewhere the optimiser cannot follow and hands it back. See the
 * note at the top: the address goes through bench_hide rather than the function
 * pointer itself, because C does not promise that a function pointer survives a
 * trip through void *. */
static Filter hide_filter(Filter f) {
    static Filter slot;
    slot = f;
    return *(const Filter *)bench_hide(&slot);
}

static Tick hide_tick(Tick f) {
    static Tick slot;
    slot = f;
    return *(const Tick *)bench_hide(&slot);
}

/* ------------------------------------------------------------- the dispatch */

/* The call the design is about. A load of the function pointer and an indirect
 * call, once per iteration, with the environment going in first even though
 * this target ignores it. */
BENCH(func_call) {
    Filter f = hide_filter(BURROW_FN(Filter, add_one, NULL));

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)BURROW_CALLF(f, (Int)bench_i_));
    }
}

/* The same work with the function named at the call site, which both compilers
 * inline. The gap against func_call is what a function value costs, and it is
 * the only row here that needs no explanation. */
BENCH(func_call_direct) {
    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)add_one(NULL, (Int)bench_i_));
    }
}

/* A target that reads its environment, which is a Go closure reading a captured
 * variable. One more load than func_call and it should cost about that. */
BENCH(func_call_env) {
    ScaleEnv e = {3};
    Filter f = hide_filter(BURROW_FN(Filter, scale, &e));

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)BURROW_CALLF(f, (Int)bench_i_));
    }
}

/* A target that writes to its environment, which is a Go closure assigning to a
 * captured variable. The store cannot be hoisted out of either loop, because
 * neither compiler can see that nothing else reads it. */
BENCH(func_call_write_env) {
    CountEnv e = {0};
    Filter f = hide_filter(BURROW_FN(Filter, bump, &e));

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)BURROW_CALLF(f, 1));
    }

    bench_keep_u64((uint64_t)e.n);
}

/* Two targets at one call site, alternating every iteration, which is what a
 * real program's callbacks look like and what func_call above deliberately is
 * not.
 *
 * It measured about half a nanosecond over func_call on both sides, and that is
 * the extra load of the pair out of the array rather than a mispredict. An
 * alternation of two targets is something any indirect branch predictor gets
 * right every time. A call site that sees a dozen targets in no particular
 * order would cost more, and neither language has a way to make that cheap. */
BENCH(func_call_two_targets) {
    Filter fs[2];

    fs[0] = hide_filter(BURROW_FN(Filter, add_one, NULL));
    fs[1] = hide_filter(BURROW_FN(Filter, double_it, NULL));

    BENCH_LOOP(b) {
        Filter f = fs[bench_i_ & 1];
        bench_keep_u64((uint64_t)BURROW_CALLF(f, (Int)bench_i_));
    }
}

/* A function value taking no arguments, which is what BURROW_CALLF0 exists for
 * and what most of the library's own callbacks will be: a deferred call, what a
 * goroutine starts with, what sync.Once.Do runs. */
BENCH(func_call0) {
    CountEnv e = {0};
    Tick f = hide_tick(BURROW_FN(Tick, tick, &e));

    BENCH_LOOP(b) {
        BURROW_CALLF0(f);
    }

    bench_keep_u64((uint64_t)e.n);
}

/* ------------------------------------------------------------ making one */

/* Building a value and letting it escape. Two stores here. In Go the closure
 * captures a variable and outlives the iteration, so it goes on the heap and
 * the allocation columns say so. This is the row where the explicit environment
 * struct pays for itself.
 *
 * The environment varies per iteration on both sides, because a capture that
 * never changes is one the compiler can build once outside the loop, and then
 * the row measures nothing. */
BENCH(func_make) {
    static ScaleEnv e;

    BENCH_LOOP(b) {
        Filter f;
        e.by = (Int)bench_i_;
        f = BURROW_FN(Filter, scale, &e);
        bench_keep(&f);
    }
}

/* ------------------------------------------------------- the realistic shape */

enum { LINES = 64 };

/* Passing a function value to something that runs it in a loop, which is what
 * every sort, filter and walk in the library will look like. The call is not
 * the only thing being measured here and that is deliberate: this is the row
 * that says what a function value costs in the place people actually use one. */
static Int count_over(const Int *xs, Int n, Filter keep) {
    Int total = 0, i;
    for (i = 0; i < n; i++)
        total += BURROW_CALLF(keep, xs[i]);
    return total;
}

BENCH(func_higher_order) {
    static Int xs[LINES];
    ScaleEnv e = {3};
    Filter f = hide_filter(BURROW_FN(Filter, scale, &e));
    Int i;

    for (i = 0; i < (Int)LINES; i++)
        xs[i] = i;

    BENCH_LOOP(b) {
        bench_keep_u64((uint64_t)count_over(xs, (Int)LINES, f));
    }
}

void register_func_benchmarks(void);

void register_func_benchmarks(void) {
    BENCH_RUN(func_call);
    BENCH_RUN(func_call_direct);
    BENCH_RUN(func_call_env);
    BENCH_RUN(func_call_write_env);
    BENCH_RUN(func_call_two_targets);
    BENCH_RUN(func_call0);
    BENCH_RUN(func_make);
    BENCH_RUN(func_higher_order);
}
