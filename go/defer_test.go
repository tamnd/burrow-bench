// The Go side of the defer benchmarks.
//
// Read DeferOne against DeferDirect first. DeferDirect calls the cleanup at the
// bottom of the function by hand, which is what the code says without the
// feature, so the gap between the two rows is what a defer costs in Go. The C
// side asks the same question about BURROW_SCOPE and the same pair of rows is
// there to answer it.
//
// One asymmetry worth knowing before reading the ratios, and it is a real
// difference between the two rather than a flaw in the measurement. Go's
// compiler open-codes a defer into the frame when it can see which function
// runs, and here it always can, so these rows are a direct call at a known
// address. burrow's defer is a function value, since everything in burrow that
// takes a callback is, so its call is indirect. Both sides are the feature as
// the language actually offers it.
//
// DeferEight is the row where Go stops open-coding. The compiler gives up past
// eight defers in a function or any defer in a loop, and the fallback puts a
// record on the heap per deferred call.
//
// The deferred function increments a package level counter that the benchmark
// reads at the end, so neither compiler is free to decide the cleanup does
// nothing and delete it.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import "testing"

var deferTicks uint64

func tick() { deferTicks++ }

// One defer per call, which is the shape almost every defer in a real program
// has. The work is in a function of its own because a defer runs at function
// return and a benchmark loop body is not a function.
func deferOne() { defer tick() }

func deferDirect() { tick() }

func deferFour() {
	defer tick()
	defer tick()
	defer tick()
	defer tick()
}

func deferEight() {
	defer tick()
	defer tick()
	defer tick()
	defer tick()
	defer tick()
	defer tick()
	defer tick()
	defer tick()
}

// Ten defers in a loop, which Go holds until the function returns. The C side
// puts the scope inside the loop and runs each one on its turn, which is the
// deliberate difference between the two and the reason this row exists.
func deferLoopTen() {
	for i := 0; i < 10; i++ {
		defer tick()
	}
}

func BenchmarkDeferOne(b *testing.B) {
	for i := 0; i < b.N; i++ {
		deferOne()
	}

	sinkU64 = deferTicks
}

func BenchmarkDeferDirect(b *testing.B) {
	for i := 0; i < b.N; i++ {
		deferDirect()
	}

	sinkU64 = deferTicks
}

func BenchmarkDeferFour(b *testing.B) {
	for i := 0; i < b.N; i++ {
		deferFour()
	}

	sinkU64 = deferTicks
}

func BenchmarkDeferEight(b *testing.B) {
	for i := 0; i < b.N; i++ {
		deferEight()
	}

	sinkU64 = deferTicks
}

func BenchmarkDeferLoopTen(b *testing.B) {
	for i := 0; i < b.N; i++ {
		deferLoopTen()
	}

	sinkU64 = deferTicks
}
