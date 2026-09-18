// The Go side of the Str benchmarks.
//
// Go's string is a pointer and a length, which is what burrow's Str is, so
// these should be the closest match in the whole suite. Where a number here and
// the matching C number differ by much, the difference is burrow's to explain.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"strings"
	"testing"
)

const shortStr = "burrow"

const mediumStr = "the quick brown fox jumps over the lazy dog, and then does it again"

const longLen = 4096

var (
	longA string
	longB string
	sinkU uint64
	sinkB bool
)

func init() {
	a := make([]byte, longLen)
	c := make([]byte, longLen)
	for i := range a {
		a[i] = byte('a' + i%26)
		c[i] = byte('a' + i%26)
	}
	c[longLen-1] = 'Z'
	longA = string(a)
	longB = string(c)
}

// Go knows a string's length without looking, so this is a field read. The C
// side has to call strlen, because it is handed a char star. That is the cost
// of the boundary and it only gets paid once, at the edge of the program.
func BenchmarkLenShort(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU = uint64(len(shortStr))
	}
}

func BenchmarkLenMedium(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU = uint64(len(mediumStr))
	}
}

func BenchmarkEqualShort(b *testing.B) {
	x, y := shortStr, shortStr
	for i := 0; i < b.N; i++ {
		sinkB = x == y
	}
}

// Same length, differing in the last byte, so the comparison reads all of it.
func BenchmarkEqualLong(b *testing.B) {
	x, y := longA, longB
	for i := 0; i < b.N; i++ {
		sinkB = x == y
	}
}

// Different lengths, which Go answers from the length words without touching
// either buffer. This is the case where both Go and burrow leave strcmp behind.
func BenchmarkEqualDifferentLengths(b *testing.B) {
	x, y := longA, longB[:longLen-1]
	for i := 0; i < b.N; i++ {
		sinkB = x == y
	}
}

func BenchmarkCompareLong(b *testing.B) {
	x, y := longA, longB
	for i := 0; i < b.N; i++ {
		sinkU = uint64(strings.Compare(x, y) + 1)
	}
}

func BenchmarkBytesEqualLong(b *testing.B) {
	x, y := []byte(longA), []byte(longB)
	for i := 0; i < b.N; i++ {
		sinkB = bytes.Equal(x, y)
	}
}

// Copying a string, which in Go means allocating and letting the collector deal
// with it. The C side does the same copy into an arena.
func BenchmarkCloneMedium(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sink = strings.Clone(mediumStr)
	}
}

// The bounds checked index, sixty four of them, which is what str_at does. Go's
// compiler can often prove the check away here, and burrow's cannot, so this
// pair is the honest measure of what burrow's check costs today.
func BenchmarkIndex(b *testing.B) {
	s := longA
	for i := 0; i < b.N; i++ {
		var sum uint64
		for j := 0; j < 64; j++ {
			sum += uint64(s[j])
		}
		sinkU = sum
	}
}
