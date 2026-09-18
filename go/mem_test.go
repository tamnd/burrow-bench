// The Go side of the allocator benchmarks.
//
// Names match the C ones with the Benchmark prefix Go requires, so that
// tools/run.sh can pair them up by stripping it. If you add one here, add the
// matching one in ../bench/mem_bench.c, and make sure both do the same work.
//
// Go has no arena, so there is no Go counterpart for arena_alloc as such. What
// there is instead is the thing a Go programmer would actually write, which is
// a make that the collector cleans up later, and that is the honest comparison:
// not arena against arena, but what each language does when you need 64 bytes.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"testing"
)

const small = 64

const large = 1 << 20

var sink any

// Go's allocator, escaping so the compiler cannot put it on the stack. Without
// the sink this measures nothing, the same way the C side needs bench_keep.
func BenchmarkAllocSmall(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		p := make([]byte, small)
		sink = p
	}
}

// Go zeroes everything it allocates, so the line above is already the zeroed
// case. This one exists so the C side has a name to line up against and so the
// two numbers being identical is visible rather than assumed.
func BenchmarkAllocSmallZeroed(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		p := make([]byte, small)
		sink = p
	}
}

func BenchmarkAllocLarge(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		p := make([]byte, large)
		sink = p
	}
}

// append doubling from 16 bytes to 4096, which is what arena_append_grow does
// on the C side. Go's append is the thing burrow's realloc path has to be as
// fast as, because it is the operation every program does constantly.
func BenchmarkAppendGrow(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		s := make([]byte, 0, 16)
		for len(s) < 4096 {
			s = append(s, make([]byte, len(s)+16)...)
		}
		sink = s
	}
}

// The collector doing the work an arena reset does in one instruction. This is
// not a fair per operation comparison and it is not meant to be read as one,
// which is why it is a separate name. It is here because somebody always asks
// what the collector costs, and the answer should be a number.
func BenchmarkGC(b *testing.B) {
	for i := 0; i < b.N; i++ {
		runtime.GC()
	}
}
