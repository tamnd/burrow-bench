// The Go side of the mutex benchmarks.
//
// These are a true like for like pair, which most of the files in here are not.
// burrow's sync.Mutex is a port of Go's rather than a lock that behaves
// similarly: the same state word, the same spin budget, the same one
// millisecond starvation threshold and the same handoff. So a gap on any of
// these rows is a gap in the port and nothing else, and that makes this file
// the most useful comparison in the repository.
//
// The contended rows start four goroutines on four Ps and hand each of them a
// quarter of the iteration count, and the C side does exactly that. RunParallel
// would have been the idiomatic way to write it and it is not used here,
// because it starts one goroutine per P and the C side would then have been
// counting a different size of crowd on every machine.
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

var (
	benchMu sync.Mutex
	benchRW sync.RWMutex
)

// ------------------------------------------------------------- uncontended

func BenchmarkMutexLockUnlock(b *testing.B) {
	for i := 0; i < b.N; i++ {
		benchMu.Lock()
		benchMu.Unlock()
	}
}

func BenchmarkMutexTryLockUnlock(b *testing.B) {
	for i := 0; i < b.N; i++ {
		if benchMu.TryLock() {
			benchMu.Unlock()
		}
	}
}

// Through the Locker interface, which costs an interface call each way. Go
// hides the conversion behind the fact that a *Mutex already satisfies Locker,
// but the call it makes afterwards is the same indirect call the C side makes.
func BenchmarkMutexLockerLockUnlock(b *testing.B) {
	var l sync.Locker = &benchMu

	for i := 0; i < b.N; i++ {
		l.Lock()
		l.Unlock()
	}
}

func BenchmarkRWMutexLockUnlock(b *testing.B) {
	for i := 0; i < b.N; i++ {
		benchRW.Lock()
		benchRW.Unlock()
	}
}

func BenchmarkRWMutexRLockRUnlock(b *testing.B) {
	for i := 0; i < b.N; i++ {
		benchRW.RLock()
		benchRW.RUnlock()
	}
}

// --------------------------------------------------------------- contended

// An empty critical section on purpose. Work inside it would be the
// measurement, and an empty one is the case that made starvation mode
// necessary, since a goroutine looping on lock and unlock is the one that can
// hold a queue out forever.
func BenchmarkMutexContended(b *testing.B) {
	var mu sync.Mutex

	runContended(b, func(each int) {
		for i := 0; i < each; i++ {
			mu.Lock()
			mu.Unlock()
		}
	})
}

// Everybody reading, which is what an RWMutex is bought for. Nobody waits and
// the number is still not small, because every reader writes the same counter
// and the cache line has to move between the cores to let it.
func BenchmarkRWMutexContendedRead(b *testing.B) {
	var rw sync.RWMutex

	runContended(b, func(each int) {
		for i := 0; i < each; i++ {
			rw.RLock()
			rw.RUnlock()
		}
	})
}

const mutexWorkers = 4

// Four goroutines on four Ps, a quarter of the iteration count each. The C side
// has the same helper with the same two numbers in it.
func runContended(b *testing.B, work func(each int)) {
	each := b.N / mutexWorkers
	if each < 1 {
		each = 1
	}

	old := runtime.GOMAXPROCS(mutexWorkers)
	defer runtime.GOMAXPROCS(old)

	var wg sync.WaitGroup
	for i := 0; i < mutexWorkers; i++ {
		wg.Add(1)
		go func() {
			work(each)
			wg.Done()
		}()
	}
	wg.Wait()
}
