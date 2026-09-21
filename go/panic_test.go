// The Go side of the panic benchmarks.
//
// Read PanicTryEmpty first. It is a function with a deferred recover that has
// nothing to recover, which is what a handler that guards against panics costs
// on every call that goes fine, and it is the number that decides whether
// guarding is affordable. PanicCaught is the same function with a panic in it,
// so the gap between the two is what going wrong costs.
//
// PanicCaughtScopes puts three frames with three deferred calls in the way,
// which is the shape a real panic has, since the reason to unwind rather than
// exit is that there is cleanup to run on the way out.
//
// These are not two spellings of one implementation and should not be read as
// if they were. Go recovers inside a deferred closure, which is the only way Go
// spells it, and burrow recovers in a catch block, which is the only way burrow
// spells it. Go pays in the defer and burrow pays in the block. The rows are
// each language's idiom for the same job.
//
// The counter is a package level variable the benchmark reads at the end, so
// neither compiler is free to decide the recovery did nothing.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import "testing"

var panicTicks uint64

func panicTick() { panicTicks++ }

// A guarded call that does not panic, which is every request that goes fine.
func panicTryEmpty() {
	defer func() {
		if r := recover(); r != nil {
			panicTicks++
		}
	}()
}

func panicCaught() {
	defer func() {
		if r := recover(); r != nil {
			panicTicks++
		}
	}()

	panic("bench")
}

// Three frames deep with a deferred call in each. The innermost panics and the
// three deferred calls run on the way out.
func panicLevelThree() {
	defer panicTick()
	panic("bench")
}

func panicLevelTwo() {
	defer panicTick()
	panicLevelThree()
}

func panicLevelOne() {
	defer panicTick()
	panicLevelTwo()
}

func panicCaughtScopes() {
	defer func() {
		if r := recover(); r != nil {
			panicTicks++
		}
	}()

	panicLevelOne()
}

func BenchmarkPanicTryEmpty(b *testing.B) {
	for i := 0; i < b.N; i++ {
		panicTryEmpty()
	}

	sinkU64 = panicTicks
}

func BenchmarkPanicCaught(b *testing.B) {
	for i := 0; i < b.N; i++ {
		panicCaught()
	}

	sinkU64 = panicTicks
}

func BenchmarkPanicCaughtScopes(b *testing.B) {
	for i := 0; i < b.N; i++ {
		panicCaughtScopes()
	}

	sinkU64 = panicTicks
}
