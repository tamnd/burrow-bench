// The Go side of the SyncMap benchmarks.
//
// Both sides are a hash trie. Go's sync.Map has been one since 1.24, when the
// read map and dirty map pair was replaced, and burrow's is a port of it. So
// the ratios here are two implementations of one design. What differs is how an
// unlinked node goes away: Go leaves it to the collector and burrow hands it to
// an epoch reclaimer, which is why the write rows move further apart than the
// read rows.
//
// The keys and values are boxed once here and reused. A Go caller with an int
// key pays an interface conversion on every call into sync.Map, and for a value
// outside the small integer cache that conversion allocates, which would be
// most of the time in these loops. Taking it out of the loop is what makes the
// column the map. It also means a real Go program keyed by an int pays more
// than the number here.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"sync"
	"testing"
)

// The same thousand and twenty four entries as the C side and as map_test.go,
// so these rows can be read against the plain map rows. A power of two, so the
// index is a mask.
const syncMapN = 1024

// Boxed once. intKeys and intMisses are the same numbers map_test.go uses,
// built by its init. sinkAny, sinkInt and sinkBool are declared by the iface,
// slice and error benchmarks, since these all live in one package.
var (
	anyKeys   [syncMapN]any
	anyMisses [syncMapN]any
)

func init() {
	for i := 0; i < syncMapN; i++ {
		anyKeys[i] = intKeys[i]
		anyMisses[i] = intMisses[i]
	}
}

func filledSyncMap() *sync.Map {
	var m sync.Map
	for i := 0; i < syncMapN; i++ {
		m.Store(anyKeys[i], anyKeys[i])
	}
	return &m
}

// A hit walks the trie to an entry and compares the key. A thousand entries is
// two or three levels of a sixteen way trie, so this is a couple of dependent
// loads and nothing is locked.
func BenchmarkSyncMapLoadHit(b *testing.B) {
	m := filledSyncMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		v, _ := m.Load(anyKeys[i&(syncMapN-1)])
		sinkAny = v
	}
}

// The miss stops at the first empty slot or at an entry whose key does not
// match, so it is usually shorter than a hit and never longer.
func BenchmarkSyncMapLoadMiss(b *testing.B) {
	m := filledSyncMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, ok := m.Load(anyMisses[i&(syncMapN-1)])
		sinkBool = ok
	}
}

// LoadOrStore on a key that is already there. It looks the key up without
// taking a lock first, so this should land near the load row rather than near
// the store row.
func BenchmarkSyncMapLoadOrStoreHit(b *testing.B) {
	m := filledSyncMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := anyKeys[i&(syncMapN-1)]
		v, _ := m.LoadOrStore(k, k)
		sinkAny = v
	}
}

// Storing over a key that is already there. An entry is immutable once it is
// published on both sides, so this builds a replacement, swaps it in, and drops
// the old one. Go drops it on the floor for the collector and burrow hands it
// to the reclaimer, so the allocation counts are the same experiment and the
// times are not quite.
func BenchmarkSyncMapStoreUpdate(b *testing.B) {
	m := filledSyncMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := anyKeys[i&(syncMapN-1)]
		m.Store(k, k)
	}
}

// One insert and one delete of a key that was not there, which is the churn a
// cache keyed by something short lived does all day. The insert can split a
// node and the delete can prune one back, so this row carries the structural
// work the update row does not.
func BenchmarkSyncMapStoreDeletePair(b *testing.B) {
	m := filledSyncMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := anyMisses[i&(syncMapN-1)]
		m.Store(k, k)
		m.Delete(k)
	}
}

// A whole walk of a thousand entries per iteration, so divide by a thousand for
// the per entry cost. Go's walk takes nothing, no lock and no bookkeeping,
// because the collector keeps a node alive for as long as the walk is holding
// it. burrow has to say so itself, and holds one reclamation pin across the
// whole walk rather than one per entry, which is why its callback is not
// allowed to block. This is the row where that decision is visible.
func BenchmarkSyncMapRange(b *testing.B) {
	m := filledSyncMap()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		n := 0
		m.Range(func(k, v any) bool {
			sinkAny = v
			n++
			return true
		})
		sinkInt = n
	}
}
