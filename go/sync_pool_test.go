// The Go side of the SyncPool benchmarks.
//
// Both sides are the same structure. A private slot and a ring buffer chain per
// P, a steal from the far end of somebody else's ring when your own is empty,
// and a victim generation behind the live one. burrow's is a port of Go's, so
// these rows are two implementations of one design.
//
// What differs is underneath, and none of it is on a path measured here. Go
// drops an object and the collector takes it, where burrow calls a free
// function, and Go empties a pool at a garbage collection where burrow does it
// on a timer. The rows below are the Get and the Put on one P, which is the
// same handful of loads and stores on both sides.
//
// New hands back a pointer to a preallocated object rather than making one, so
// that the miss row is about the walk that happens before New is reached and
// not about the allocator.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"sync"
	"testing"
)

// What the pool holds. The same 64 bytes the C side uses.
type poolBuf struct {
	data [64]byte
}

// The objects New hands out, and where it is up to. A ring of them rather than
// one, so that a row holding several at once holds several different ones.
const poolObjs = 64

var (
	poolBufs [poolObjs]poolBuf
	nextBuf  int
)

func newPool() *sync.Pool {
	nextBuf = 0
	return &sync.Pool{New: func() any {
		b := &poolBufs[nextBuf]
		nextBuf = (nextBuf + 1) & (poolObjs - 1)
		return b
	}}
}

// A Get and a Put back to back on one P. The Put fills the private slot and the
// Get takes it out again, so there is no atomic anywhere and no memory another
// core has ever seen. It is the row a pool exists for.
func BenchmarkSyncPoolGetPut(b *testing.B) {
	p := newPool()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		v := p.Get()
		sinkAny = v
		p.Put(v)
	}
}

// Two out and two back. The private slot holds one, so the second of each pair
// goes through the ring at the head of this P's chain. This row against the one
// above is what the ring costs over the private slot.
func BenchmarkSyncPoolGetPutPair(b *testing.B) {
	p := newPool()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		v1 := p.Get()
		v2 := p.Get()
		sinkAny = v1
		sinkAny = v2
		p.Put(v1)
		p.Put(v2)
	}
}

// A Get on a pool with nothing in it. Nothing goes back, so every iteration
// walks an empty private slot, an empty ring of its own, a scan of every other
// P's, the victim generation behind all of it, and then calls New. The worst
// case, and also the first call any program makes.
func BenchmarkSyncPoolGetNew(b *testing.B) {
	p := newPool()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkAny = p.Get()
	}
}
