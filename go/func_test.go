// The Go side of the function value benchmarks.
//
// A Go closure is a function pointer and a pointer to the captured variables,
// which is what a burrow function value is, so most of these rows are an even
// comparison and are meant to come out level. The row to read first is the gap
// between FuncCall and FuncCallDirect, since that gap is what a function value
// costs in a language that can inline through a direct call.
//
// Every dispatch row reads its function out of a package level variable, for
// the same reason the C side puts its function pointer through bench_hide: a
// closure the compiler can trace back to a named function is one it will call
// directly and then inline, and the row would measure an empty loop.
//
// BenchmarkFuncMake is the uneven row and the interesting one. The closure
// captures a variable and escapes, so Go puts it on the heap and the allocation
// columns say so. The C side builds a pair of words next to an environment
// struct that was already there. Read the allocation columns on that row.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import "testing"

// sinkInt is declared by the slice benchmarks, which is why it is missing here.
var sinkFilter func(int) int

func addOne(d int) int { return d + 1 }

// A second target, so a call site can be made to see more than one of them.
// Doubling rather than adding, so the two are not the same function under two
// names.
func doubleIt(d int) int { return d * 2 }

// Read out of a package level variable so the compiler cannot prove which
// function this is. See the note at the top.
var globalFilter func(int) int = addOne

var globalDouble func(int) int = doubleIt

// The closures the rows below call, built by a function that captures its
// argument and stored in package level variables for the reason at the top.
// A closure literal written inside a benchmark is one the compiler will inline
// straight through, and the first version of this file did exactly that: the
// environment row measured a multiply and reported 0.93 ns/op against a real
// call's 2.5, which is not a faster call, it is no call at all.
func makeScale(by int) func(int) int {
	return func(d int) int { return d * by }
}

func makeBump() func(int) int {
	n := 0
	return func(d int) int {
		n += d
		return n
	}
}

func makeTick() func() {
	n := 0
	return func() { n++ }
}

var (
	globalScale = makeScale(3)
	globalBump  = makeBump()
	globalTick  = makeTick()
)

// The call the design is about: a load and an indirect call.
func BenchmarkFuncCall(b *testing.B) {
	f := globalFilter
	for i := 0; i < b.N; i++ {
		sinkInt = f(i)
	}
}

// The same work with the function named at the call site, which both compilers
// inline. The gap against FuncCall is what a function value costs.
func BenchmarkFuncCallDirect(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkInt = addOne(i)
	}
}

// A closure reading a captured variable, which is the C side's target reading
// its environment. One more load than FuncCall and it should cost about that.
func BenchmarkFuncCallEnv(b *testing.B) {
	f := globalScale
	for i := 0; i < b.N; i++ {
		sinkInt = f(i)
	}
}

// A closure assigning to a captured variable, which is capture by reference and
// is the C side writing through its environment pointer.
func BenchmarkFuncCallWriteEnv(b *testing.B) {
	f := globalBump
	for i := 0; i < b.N; i++ {
		sinkInt = f(1)
	}
}

// Two targets at one call site, alternating every iteration, which is what a
// real program's callbacks look like and what FuncCall above deliberately is
// not. It measured about half a nanosecond over FuncCall on both sides, and
// that is the extra load out of the array rather than a mispredict, since an
// alternation of two targets is something any indirect branch predictor gets
// right every time.
func BenchmarkFuncCallTwoTargets(b *testing.B) {
	fs := [2]func(int) int{globalFilter, globalDouble}
	for i := 0; i < b.N; i++ {
		sinkInt = fs[i&1](i)
	}
}

// A function value taking nothing, which is what most of the library's own
// callbacks are: a deferred call, what a goroutine starts with, what
// sync.Once.Do runs.
func BenchmarkFuncCall0(b *testing.B) {
	f := globalTick
	for i := 0; i < b.N; i++ {
		f()
	}
}

// Building one and letting it escape, which is a heap allocation in Go and two
// stores in C. The capture varies per iteration on both sides, because a
// capture that never changes is one the compiler can build once outside the
// loop, and then the row measures nothing.
func BenchmarkFuncMake(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		by := i
		sinkFilter = func(d int) int { return d * by }
	}
}

const funcLines = 64

// Passing a function value to something that runs it in a loop, which is what
// every sort, filter and walk looks like. This is the row that says what a
// function value costs where people actually use one.
func countOver(xs []int, keep func(int) int) int {
	total := 0
	for _, x := range xs {
		total += keep(x)
	}
	return total
}

func BenchmarkFuncHigherOrder(b *testing.B) {
	xs := make([]int, funcLines)
	for i := range xs {
		xs[i] = i
	}
	f := globalScale
	for i := 0; i < b.N; i++ {
		sinkInt = countOver(xs, f)
	}
}
