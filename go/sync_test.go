// The Go side of the concurrency benchmarks.
//
// The atomic rows are sync/atomic and should be level with the C ones, since
// both compile to the same instruction.
//
// The rest are not a like for like port and cannot be. burrow's note is an
// operating system level gate that a thread blocks in, and Go has no exported
// equivalent because Go never asks a user to block a thread. So the pairs are
// chosen to be the thing a Go programmer writes when they want what the C side
// is doing: a WaitGroup cycle for a gate you open and pass through, an
// unbuffered channel for handing a turn to somebody else, and a goroutine plus
// a wait for starting a worker and waiting for it.
//
// Go wins the blocking rows by a wide margin and that is the whole point of
// recording them. Every one of those wins comes from the scheduler keeping the
// handoff in user space, which is exactly the thing burrow is being built to
// have. Read the gap as a target rather than as a defeat.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"sync"
	"sync/atomic"
	"testing"
)

var (
	syncCounter   uint64
	syncCounter32 uint32
)

// ----------------------------------------------------------------- atomics

func BenchmarkAtomicAddU64(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = atomic.AddUint64(&syncCounter, 1)
	}
}

func BenchmarkAtomicAddU32(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = uint64(atomic.AddUint32(&syncCounter32, 1))
	}
}

func BenchmarkAtomicLoadU64(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkU64 = atomic.LoadUint64(&syncCounter)
	}
}

func BenchmarkAtomicStoreU64(b *testing.B) {
	for i := 0; i < b.N; i++ {
		atomic.StoreUint64(&syncCounter, uint64(i))
	}
}

// Always succeeds, the same as the C row, because a failing compare and swap
// measures the retry rather than the instruction.
func BenchmarkAtomicCASU64(b *testing.B) {
	atomic.StoreUint64(&syncCounter, 0)

	for i := 0; i < b.N; i++ {
		if atomic.CompareAndSwapUint64(&syncCounter, uint64(i), uint64(i)+1) {
			sinkU64 = 1
		}
	}
}

// ------------------------------------------------------------------- notes

// A gate opened and walked through with nobody waiting on it, which is what a
// WaitGroup that is already done does. The C row is a note cleared, woken and
// slept on by one thread.
func BenchmarkNoteCycle(b *testing.B) {
	var wg sync.WaitGroup

	for i := 0; i < b.N; i++ {
		wg.Add(1)
		wg.Done()
		wg.Wait()
	}
}

// One volley per iteration over an unbuffered channel, which is Go's way of
// handing a turn to another goroutine and getting it back. The C row does the
// same thing with two notes and two threads, and goes into the kernel to do it.
func BenchmarkNotePingpong(b *testing.B) {
	toWorker := make(chan struct{})
	toMain := make(chan struct{})
	done := make(chan struct{})

	go func() {
		for range toWorker {
			toMain <- struct{}{}
		}
		close(done)
	}()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		toWorker <- struct{}{}
		<-toMain
	}
	b.StopTimer()

	close(toWorker)
	<-done
}

// ----------------------------------------------------------------- threads

var syncWorkerRan uint64

// Starting a worker and waiting for it. A goroutine and a WaitGroup here, a
// clone syscall and a join there.
func BenchmarkThreadStartJoin(b *testing.B) {
	var wg sync.WaitGroup

	for i := 0; i < b.N; i++ {
		wg.Add(1)
		go func() {
			syncWorkerRan++
			wg.Done()
		}()
		wg.Wait()
	}

	sinkU64 = syncWorkerRan
}

func BenchmarkThreadYield(b *testing.B) {
	for i := 0; i < b.N; i++ {
		runtime.Gosched()
	}
}

// There is no pair for the C side's thread_self. Go does not export a goroutine
// id on purpose, and pairing it with anything else here would be inventing a
// comparison rather than making one.
