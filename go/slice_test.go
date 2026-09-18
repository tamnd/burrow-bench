// The Go side of the Slice benchmarks.
//
// Read the ratios here with one thing in mind. The C side allocates into an
// arena and resets it once per iteration, which costs a pointer rewind. The Go
// side allocates into the heap and hands the garbage to a collector that runs
// later, on another thread, and whose cost lands somewhere outside b.N. Neither
// number is wrong and neither is the whole story. The allocation counts in the
// third and fourth columns are the honest comparison, because both sides count
// the same thing.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import "testing"

const n = 1024

const chunkLen = 64

var (
	chunk    [chunkLen]int
	sinkInts []int
	sinkByte []byte
	sinkInt  int
)

func init() {
	for i := range chunk {
		chunk[i] = i
	}
}

// Build a slice of a thousand ints from nothing, one element at a time, letting
// append work out the capacity. This is the shape of every parse loop there is.
func BenchmarkSliceAppendGrow(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		var s []int
		for j := 0; j < n; j++ {
			s = append(s, j)
		}
		sinkInts = s
	}
}

// The same with the capacity known up front. The gap between this and the one
// above is what the growth costs.
func BenchmarkSliceAppendPrealloc(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		s := make([]int, 0, n)
		for j := 0; j < n; j++ {
			s = append(s, j)
		}
		sinkInts = s
	}
}

// Sixteen bulk appends of sixty four instead of a thousand and twenty four
// single ones. Same elements, one sixteenth of the calls.
func BenchmarkSliceAppendBulk(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		var s []int
		for j := 0; j < n/chunkLen; j++ {
			s = append(s, chunk[:]...)
		}
		sinkInts = s
	}
}

func BenchmarkSliceMake(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkInts = make([]int, n, n)
	}
}

// Sixty four bounds checked reads. Go's compiler inlines the indexing and can
// often prove the check away, and burrow's cannot, so this pair measures what
// burrow's check costs today rather than what it has to cost.
func BenchmarkSliceIndex(b *testing.B) {
	s := make([]int, n)
	for i := range s {
		s[i] = i
	}
	for i := 0; i < b.N; i++ {
		var sum uint64
		for j := 0; j < chunkLen; j++ {
			sum += uint64(s[j])
		}
		sinkU = sum
	}
}

// Reslicing, which should be arithmetic and nothing else on both sides.
func BenchmarkSliceSub(b *testing.B) {
	s := make([]int, n)
	for i := 0; i < b.N; i++ {
		m := s[1 : n-1]
		sinkInt = len(m)
		sinkInts = m
	}
}

func BenchmarkSliceCopy(b *testing.B) {
	src := make([]int, n)
	dst := make([]int, n)
	for i := 0; i < b.N; i++ {
		sinkInt = copy(dst, src)
	}
}

// []byte(s), which allocates and copies in Go and does the same in burrow.
func BenchmarkSliceBytesFromString(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkByte = []byte(mediumStr)
	}
}
