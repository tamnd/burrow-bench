// The Go side of the Context benchmarks.
//
// Both sides are the same tree with the same three operations on it: make a
// node under a parent, ask a node a question, and run a cancellation down from
// a node. burrow's is a port of Go's, so these rows are two implementations of
// one design.
//
// Two things differ and both show up in the numbers. burrow makes the done
// channel when the context is made, because context_done there has no allocator
// and no way to report a failure, where Go makes it the first time somebody
// asks. And burrow keeps the children in an intrusive doubly linked list where
// Go keeps a map[canceler]struct{}, which Go cannot avoid because it has
// nowhere to put the links.
//
// The C side frees every node inside the timed loop, since there is no
// collector. Go hands the node to the collector instead and the cost lands in
// the allocs per operation this harness reports. Neither side is being
// flattered.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"context"
	"testing"
	"time"
)

// A key type private to this file, which is what both sides do. In Go it is an
// unexported named type and in C it is a Type descriptor declared static.
type benchKey int

const (
	keyOne benchKey = 1
	keyTwo benchKey = 2
	// Never put into a context, and what the miss row looks for.
	keyThree benchKey = 3
)

const payload = 42

// How deep the deep lookup goes, and how many children the cancel row puts
// under one parent. The same numbers the C side uses.
const (
	ctxDepth  = 8
	ctxFanout = 64
)

var (
	sinkCtx      context.Context
	sinkCancel   context.CancelFunc
	sinkCtxDone  <-chan struct{}
	sinkCtxError error
)

// What it costs to ask for the done channel, which is the one every select on a
// cancellation goes through. Go's is an atomic load and a branch into the slow
// path that makes the channel, since Go makes it lazily. burrow's is a load of a
// field written once before the node was visible to anybody.
func BenchmarkContextDone(b *testing.B) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkCtxDone = ctx.Done()
	}
}

// Err on a context nobody has cancelled. Both sides take the node's mutex,
// because the error is written under it at the moment the channel closes, so
// this is an uncontended lock and unlock plus two loads.
func BenchmarkContextErrLive(b *testing.B) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkCtxError = ctx.Err()
	}
}

// The value sitting on the context being asked, so one comparison.
func BenchmarkContextValueShallow(b *testing.B) {
	ctx := context.WithValue(context.Background(), keyOne, payload)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkAny = ctx.Value(keyOne)
	}
}

// Builds the chain the two deep rows walk. The value goes in at the bottom, so
// the lookup has to cross every level above it.
func buildCtxChain() context.Context {
	ctx := context.WithValue(context.Background(), keyOne, payload)
	for i := 1; i < ctxDepth; i++ {
		ctx = context.WithValue(ctx, keyTwo, payload)
	}
	return ctx
}

// Eight contexts deep, so eight comparisons. Read against the shallow row for
// the per level cost.
func BenchmarkContextValueDeep(b *testing.B) {
	ctx := buildCtxChain()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkAny = ctx.Value(keyOne)
	}
}

// The same chain and a key of the right type that nobody put in, so every level
// compares and fails and then the root answers nothing. That is what a lookup
// for a key another package owns costs.
func BenchmarkContextValueMiss(b *testing.B) {
	ctx := buildCtxChain()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkAny = ctx.Value(keyThree)
	}
}

// The whole life of a context in one iteration, which is what a request handler
// does. On the C side that is a node, a channel, four pointer writes, a close
// and two frees. Here it is a node, a map insert on a cancellable parent, a
// close, and the collector taking the rest.
func BenchmarkContextWithCancel(b *testing.B) {
	root := context.Background()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx, cancel := context.WithCancel(root)
		sinkCtx = ctx
		cancel()
	}
}

// The same life with a deadline on it, which is what a request handler that
// talks to anything over a network actually writes.
//
// An hour out, so the timer never fires and what is timed is arming it and
// stopping it again, a heap insert and a heap removal on both sides. The
// difference between this row and the one above is what a deadline costs.
//
// Go arms a runtime timer directly. burrow goes through time_after_func, which
// is a TimeTimer holding a runtime timer, so there is an allocation there that
// Go does not have.
func BenchmarkContextWithTimeout(b *testing.B) {
	root := context.Background()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx, cancel := context.WithTimeout(root, time.Hour)
		sinkCtx = ctx
		cancel()
	}
}

// A deadline that has already gone by, which is what a request arriving with no
// time left on its budget looks like.
//
// No timer is armed on either side. The context comes back with its done
// channel already closed, so this row is the constructor plus a cancellation
// that reaches one node, and it should come out near the WithCancel row rather
// than near the one above.
func BenchmarkContextWithTimeoutExpired(b *testing.B) {
	root := context.Background()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx, cancel := context.WithTimeout(root, 0)
		sinkCtx = ctx
		cancel()
	}
}

// A child under a cancellable parent rather than under Background, which is the
// case that has a list or a map to join and a cancel to detach from. The row
// above has neither, so the difference is what attaching and detaching cost.
func BenchmarkContextWithCancelNested(b *testing.B) {
	parent, parentCancel := context.WithCancel(context.Background())
	defer parentCancel()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx, cancel := context.WithCancel(parent)
		sinkCtx = ctx
		cancel()
	}
}

// A four word node and no lock at all on either side.
func BenchmarkContextWithValue(b *testing.B) {
	root := context.Background()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sinkCtx = context.WithValue(root, keyOne, payload)
	}
}

// One parent, sixty four children, and a cancel that has to reach all of them,
// which is the shape a server has when one request fans out.
//
// Building the tree is inside the timed loop on both sides, because the
// alternative is timing a cancel on a tree that was already cancelled and a
// cancelled node is detached with nothing left to walk. So this row is sixty
// four WithCancels plus the cancel, and it should be read against the
// WithCancel row rather than on its own.
func BenchmarkContextCancelTree(b *testing.B) {
	var kids [ctxFanout]context.CancelFunc

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		parent, cancel := context.WithCancel(context.Background())
		for j := 0; j < ctxFanout; j++ {
			ctx, kidCancel := context.WithCancel(parent)
			sinkCtx = ctx
			kids[j] = kidCancel
		}
		cancel()
		for j := 0; j < ctxFanout; j++ {
			kids[j]()
		}
	}
	sinkCancel = kids[0]
}
