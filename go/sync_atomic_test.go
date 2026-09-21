// The Go side of the public sync/atomic rows.
//
// sync_test.go already measures the plain functions against burrow's internal
// atomics. These measure the same package against burrow's public one, which is
// the port of this package, so every row here has a counterpart that ought to
// produce the same number. A gap is a wrapper that did not inline.
//
// The Value rows store a pointer rather than an int. burrow's Any always holds
// a pointer to the value, so storing a pointer is what the C side is doing, and
// an interface holding a pointer is what Go does with no allocation either.
// Storing an int would measure Go's boxing rules against burrow's lack of them,
// which is a real difference and belongs in the iface rows where it already is.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"sync/atomic"
	"testing"
)

var (
	pubCounter int64
	pubTyped   atomic.Int64
	pubFlag    atomic.Bool
	pubPointer atomic.Pointer[int64]
	pubValue   atomic.Value
)

// ------------------------------------------------------------ plain functions

func BenchmarkAtomicAddInt64(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = uint64(atomic.AddInt64(&pubCounter, 1))
	}
}

func BenchmarkAtomicLoadInt64(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = uint64(atomic.LoadInt64(&pubCounter))
	}
}

// ------------------------------------------------------------------ the types

func BenchmarkAtomicInt64Add(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = uint64(pubTyped.Add(1))
	}
}

func BenchmarkAtomicInt64Load(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = uint64(pubTyped.Load())
	}
}

// Always succeeds, the same as the C row, because a failing compare and swap
// measures the retry rather than the instruction.
func BenchmarkAtomicInt64CAS(b *testing.B) {
	pubTyped.Store(0)

	for i := 0; i < b.N; i++ {
		if pubTyped.CompareAndSwap(int64(i), int64(i)+1) {
			sinkU64 = 1
		}
	}
}

func BenchmarkAtomicBoolLoad(b *testing.B) {
	pubFlag.Store(true)

	for i := 0; i < b.N; i++ {
		if pubFlag.Load() {
			sinkU64 = 1
		}
	}
}

func BenchmarkAtomicPointerLoad(b *testing.B) {
	pubPointer.Store(&pubCounter)

	for i := 0; i < b.N; i++ {
		sinkAny = pubPointer.Load()
	}
}

func BenchmarkAtomicPointerStore(b *testing.B) {
	for i := 0; i < b.N; i++ {
		pubPointer.Store(&pubCounter)
	}
}

// ---------------------------------------------------------------------- Value

func BenchmarkAtomicValueLoad(b *testing.B) {
	pubValue.Store(&pubCounter)

	for i := 0; i < b.N; i++ {
		sinkAny = pubValue.Load()
	}
}

func BenchmarkAtomicValueStore(b *testing.B) {
	pubValue.Store(&pubCounter)

	for i := 0; i < b.N; i++ {
		pubValue.Store(&pubCounter)
	}
}
