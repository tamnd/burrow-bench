// The Go side of the Map benchmarks.
//
// Both sides are Swiss tables, Go's since 1.24, so the ratios here compare two
// implementations of one idea. The two places they differ are the hash, where
// Go has an AES backed one and burrow has FNV-1a a byte at a time, and the
// growth, where Go splits one of a directory of tables and burrow doubles a
// single one. The string benchmarks are the hash comparison and MapSetGrow is
// the growth comparison.
//
// The construction benchmarks put a burrow arena that resets once per iteration
// against Go's heap and its collector, so those time columns are two different
// experiments. The allocation counts are the same experiment.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"fmt"
	"testing"
)

// A thousand and twenty four entries, the same as the C side, and a power of
// two so the index into the key slices is a mask rather than a division.
const mapN = 1024

var (
	intKeys   [mapN]int
	intMisses [mapN]int
	strKeys   [mapN]string
	strMisses [mapN]string

	// sinkInt and sinkBool are declared by the slice and error benchmarks,
	// since these all live in one package. This is the one this file needs
	// that nothing else has.
	sinkSize int
)

func init() {
	for i := 0; i < mapN; i++ {
		// Not 0..n, so the low bits of the key are not the index.
		intKeys[i] = i*7 + 1
		intMisses[i] = -(i*7 + 1)
		strKeys[i] = fmt.Sprintf("key%08d", i)
		strMisses[i] = fmt.Sprintf("nyk%08d", i)
	}
}

func filledIntMap() map[int]int {
	m := make(map[int]int, mapN)
	for i := 0; i < mapN; i++ {
		m[intKeys[i]] = intKeys[i]
	}
	return m
}

func filledStrMap() map[string]int {
	m := make(map[string]int, mapN)
	for i := 0; i < mapN; i++ {
		m[strKeys[i]] = i
	}
	return m
}

// The lookup that happens most. A key that is there, so the probe stops in the
// first group nearly every time.
func BenchmarkMapGetHitInt(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkInt = m[intKeys[i&(mapN-1)]]
	}
}

// The miss, which is the answer a cache gets on the request that matters. It
// has to prove absence, so it probes until it finds an empty slot.
func BenchmarkMapGetMissInt(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkInt = m[intMisses[i&(mapN-1)]]
	}
}

// The two return form, which is what burrow's map_get2 is. Go's compiler emits
// a different runtime call for it, so it gets its own number on both sides.
func BenchmarkMapGetOkHitInt(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		v, ok := m[intKeys[i&(mapN-1)]]
		sinkInt, sinkBool = v, ok
	}
}

// String keys, eleven bytes each, which is the AES hash against FNV-1a and is
// the number this file exists to produce.
func BenchmarkMapGetHitStr(b *testing.B) {
	m := filledStrMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkInt = m[strKeys[i&(mapN-1)]]
	}
}

// The string miss. The keys differ in their first three bytes, so any
// comparison that does happen ends immediately.
func BenchmarkMapGetMissStr(b *testing.B) {
	m := filledStrMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkInt = m[strMisses[i&(mapN-1)]]
	}
}

// Writing to a key that is already there, which never grows the table. This is
// where counters and caches spend their lives.
func BenchmarkMapSetUpdate(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := intKeys[i&(mapN-1)]
		m[k] = k
	}
	sinkSize = len(m)
}

// A thousand entry map from nothing with no hint, so the table grows eleven
// times on the way. Go splits one table per growth and burrow rehashes the
// whole thing, so the totals should be close and the worst single insert
// should not be.
func BenchmarkMapSetGrow(b *testing.B) {
	for i := 0; i < b.N; i++ {
		m := make(map[int]int)
		for j := 0; j < mapN; j++ {
			m[intKeys[j]] = intKeys[j]
		}
		sinkSize = len(m)
	}
}

// The same thousand inserts with the size given up front, which is what code
// that knows its own size should be doing. The gap against MapSetGrow is what
// the growth costs.
func BenchmarkMapSetPrealloc(b *testing.B) {
	for i := 0; i < b.N; i++ {
		m := make(map[int]int, mapN)
		for j := 0; j < mapN; j++ {
			m[intKeys[j]] = intKeys[j]
		}
		sinkSize = len(m)
	}
}

// String keys, built the same way, because the hash shows up in construction
// as well as in lookup and the two costs are not the same shape.
func BenchmarkMapSetPreallocStr(b *testing.B) {
	for i := 0; i < b.N; i++ {
		m := make(map[string]int, mapN)
		for j := 0; j < mapN; j++ {
			m[strKeys[j]] = j
		}
		sinkSize = len(m)
	}
}

// Deleting a key that is not there, which is the probe and nothing else, and is
// the floor for the pair below.
func BenchmarkMapDeleteMiss(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		delete(m, intMisses[i&(mapN-1)])
	}
	sinkSize = len(m)
}

// Delete a key that is there and put it straight back. Measuring the delete
// alone would empty the table on the first pass through the keys and then
// measure an empty one, and the pair is a real workload anyway, since an LRU
// moves an entry by removing it and reinserting it.
func BenchmarkMapSetDeletePair(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := intKeys[i&(mapN-1)]
		delete(m, k)
		m[k] = k
	}
	sinkSize = len(m)
}

// Walking the whole table. Both sides walk storage order from a random start,
// so this is a scan over the control bytes with a skip for every slot that is
// not full.
func BenchmarkMapIter(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sum := 0
		for _, v := range m {
			sum += v
		}
		sinkInt = sum
	}
}

// Fill a thousand, delete a thousand, forever, in one map that is never
// remade. This is what a cache does, and it is the benchmark that says whether
// the tombstones get reclaimed instead of the table growing without bound
// while it holds nothing.
func BenchmarkMapChurn(b *testing.B) {
	m := make(map[int]int)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		for j := 0; j < mapN; j++ {
			m[intKeys[j]] = intKeys[j]
		}
		for j := 0; j < mapN; j++ {
			delete(m, intKeys[j])
		}
		sinkSize = len(m)
	}
}

// clear then refill, which is the other way to reuse a map and is the one that
// keeps the memory. Go's clear is a builtin that walks the table and so is
// burrow's map_clear.
func BenchmarkMapClearRefill(b *testing.B) {
	m := filledIntMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		clear(m)
		for j := 0; j < mapN; j++ {
			m[intKeys[j]] = intKeys[j]
		}
		sinkSize = len(m)
	}
}
