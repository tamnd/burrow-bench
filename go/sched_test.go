// The Go side of the scheduler benchmarks.
//
// These are the rows where the two sides finally do the same thing. Starting a
// goroutine, yielding, and handing a turn to another goroutine and blocking
// until it hands it back are things both languages have, so unlike the runtime
// rows next door there is nothing here that had to be left unpaired.
//
// GOMAXPROCS is 1 in every one of them, which is what the C side does with
// runtime_gomaxprocs. With more than one P the two goroutines in a handoff run
// on two cores and the benchmark turns into a measurement of the cache line
// they share. That is a fair thing to measure and it is a different benchmark.
//
// One difference is written down rather than hidden: the C handoff goes through
// park and ready under a one waiter gate, and this one goes through a channel,
// because burrow has no channels yet. Go's row therefore has strictly more in
// it, and the C row being quicker is not a win, it is the room the channel has
// to fit into when it is written.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"sync/atomic"
	"testing"
)

// A launch and the switch into it and out of it again. The Gosched is what
// makes the child run now instead of piling up behind the loop, and the C side
// calls runtime_gosched in the same place for the same reason.
func BenchmarkGoroutineStart(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	var ran uint32

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		go func() {
			atomic.AddUint32(&ran, 1)
		}()
		runtime.Gosched()
	}

	b.StopTimer()
	_ = atomic.LoadUint32(&ran)
}

// Sixty four launched at once and then waited for. Divide by 64 for one.
//
// The wait is a spin on the counter with a Gosched in it rather than a
// WaitGroup, because burrow has no WaitGroup yet and the C side has to spin.
// Both sides therefore pay for the same spin and the row stays a comparison.
func BenchmarkGoroutineStartBatch(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	const batch = 64

	var done uint32

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		atomic.StoreUint32(&done, 0)

		for j := 0; j < batch; j++ {
			go func() {
				atomic.AddUint32(&done, 1)
			}()
		}

		for atomic.LoadUint32(&done) < batch {
			runtime.Gosched()
		}
	}
}

// Gosched with nothing else runnable, which is the scheduler looking and
// finding nothing and coming back. No switch happens, so this is the cost of
// asking, and it is the floor under the rows below.
func BenchmarkGoroutineYieldAlone(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		runtime.Gosched()
	}
}

// Two goroutines yielding to each other, so each call is a real switch. One
// iteration is two of them, there and back.
func BenchmarkGoroutineYieldPair(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	var stop uint32
	gone := make(chan struct{})

	go func() {
		for atomic.LoadUint32(&stop) == 0 {
			runtime.Gosched()
		}
		close(gone)
	}()

	runtime.Gosched()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		runtime.Gosched()
	}

	b.StopTimer()
	atomic.StoreUint32(&stop, 1)
	<-gone
}

// A blocking volley. This goroutine wakes the other and blocks, the other wakes
// it back and blocks again, so one iteration is two switches and four blocking
// operations.
//
// Unbuffered channels, which is how a Go program writes this. The C side writes
// it with park and ready directly, and the comment at the top says why.
func BenchmarkGoroutineHandoff(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	there := make(chan struct{})
	back := make(chan struct{})
	done := make(chan struct{})

	go func() {
		for {
			select {
			case <-there:
				back <- struct{}{}
			case <-done:
				return
			}
		}
	}()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		there <- struct{}{}
		<-back
	}

	b.StopTimer()
	close(done)
}
