// The Go side of the WaitGroup and Once benchmarks.
//
// The counter rows are a true pair: burrow's WaitGroup is a port of Go's, with
// the same state word and the same misuse checks, so a gap there is a gap in
// the port.
//
// The Once rows are less of a pair than they look. Go's Once.Do takes a func
// value and burrow's takes a Func, which is the same two words, but Go's
// OnceFunc and OnceValue return closures the collector owns while burrow's are
// structs the caller declares. So the fast path is comparable and the setup is
// not, and the setup is not measured here because it happens once.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"sync"
	"testing"
)

// ---------------------------------------------------------------- WaitGroup

var benchWG sync.WaitGroup

func BenchmarkWaitGroupAddDone(b *testing.B) {
	for i := 0; i < b.N; i++ {
		benchWG.Add(1)
		benchWG.Done()
	}
}

func BenchmarkWaitGroupWaitEmpty(b *testing.B) {
	for i := 0; i < b.N; i++ {
		benchWG.Wait()
	}
}

// ----------------------------------------------------------- Go, then Wait

const wgWorkers = 4

func BenchmarkWaitGroupGoWait(b *testing.B) {
	old := runtime.GOMAXPROCS(wgWorkers)
	defer runtime.GOMAXPROCS(old)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		var wg sync.WaitGroup
		for j := 0; j < wgWorkers; j++ {
			wg.Go(func() {})
		}
		wg.Wait()
	}
}

// --------------------------------------------------------------------- Once

var (
	onceCounter int64
	benchOnce   sync.Once
)

func bump() { onceCounter++ }

func BenchmarkOnceDo(b *testing.B) {
	benchOnce.Do(bump)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		benchOnce.Do(bump)
	}
}

var benchOnceFunc = sync.OnceFunc(bump)

func BenchmarkOnceFuncCall(b *testing.B) {
	benchOnceFunc()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		benchOnceFunc()
	}
}

// Typed rather than the shared any sink, because assigning an int64 to an
// interface is a boxing that has nothing to do with what this row measures.
var sinkInt64 int64

var benchOnceValue = sync.OnceValue(func() int64 { return 42 })

func BenchmarkOnceValueGet(b *testing.B) {
	benchOnceValue()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		sinkInt64 = benchOnceValue()
	}
}
