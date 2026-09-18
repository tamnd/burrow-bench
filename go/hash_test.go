// The Go side of the hash benchmarks.
//
// hash/maphash is the runtime hash the map uses, reached through a public door.
// maphash.String and maphash.Bytes are the runtime's string hash, and
// maphash.Comparable is the runtime's hash for any comparable type, which for
// an int is the eight byte path. So the pairing is honest, with one caveat:
// maphash.Comparable goes through generic machinery that a map lookup does not,
// so the int number here slightly overstates what Go's map pays.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"fmt"
	"hash/maphash"
	"testing"
)

// The same key count and the same shapes as the C side.
const hashKeys = 256

const (
	hashLongLen = 64
	hashHugeLen = 1024
)

var (
	hashIntKeys   [hashKeys]int
	hashFloatKeys [hashKeys]float64
	hashShortKeys [hashKeys]string
	hashLongKeys  [hashKeys]string
	hashHugeKey   string

	// A fixed seed, because a hash of a fixed seed is what a map does after
	// make and because a number that moves every run is not a benchmark. Go's
	// Seed is opaque, so this is MakeSeed once rather than a constant.
	hashSeed = maphash.MakeSeed()

	sinkU64 uint64
)

func init() {
	for i := range hashIntKeys {
		hashIntKeys[i] = i*7 + 1
		hashFloatKeys[i] = float64(i)*1.5 + 0.25
		hashShortKeys[i] = fmt.Sprintf("key%08d", i)
		hashLongKeys[i] = fmt.Sprintf("/some/reasonably/long/path/that/a/real/program/would/use/%05d", i)
		if len(hashLongKeys[i]) != hashLongLen {
			panic("long key is not the length the C side uses")
		}
	}

	b := make([]byte, hashHugeLen)
	for i := range b {
		b[i] = byte('a' + i%26)
	}
	hashHugeKey = string(b)
}

func BenchmarkHashInt(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = maphash.Comparable(hashSeed, hashIntKeys[i&(hashKeys-1)])
	}
}

func BenchmarkHashFloat64(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = maphash.Comparable(hashSeed, hashFloatKeys[i&(hashKeys-1)])
	}
}

func BenchmarkHashStrShort(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = maphash.String(hashSeed, hashShortKeys[i&(hashKeys-1)])
	}
}

func BenchmarkHashStrLong(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = maphash.String(hashSeed, hashLongKeys[i&(hashKeys-1)])
	}
}

func BenchmarkHashStrHuge(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = maphash.String(hashSeed, hashHugeKey)
	}
}
