// The Go side of the sync.Cond benchmarks.
//
// A true pair. Both sides are the same notifyList with the same ticket scheme,
// and both spell the waiter's lock as an interface, so a gap in either row is a
// gap in the port rather than a difference in what is being measured.
//
// The one thing that is not identical is where the Cond lives. Go's NewCond
// allocates and burrow's is a struct the caller declares, which is a difference
// in setup and not in the rows below, since setup happens once.
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

// ------------------------------------------------------- nobody is waiting

var (
	emptyMu   sync.Mutex
	emptyCond = sync.NewCond(&emptyMu)
)

func BenchmarkCondSignalEmpty(b *testing.B) {
	for i := 0; i < b.N; i++ {
		emptyCond.Signal()
	}
}

func BenchmarkCondBroadcastEmpty(b *testing.B) {
	for i := 0; i < b.N; i++ {
		emptyCond.Broadcast()
	}
}

// ------------------------------------------------------------- ping pong

func BenchmarkCondPingPong(b *testing.B) {
	old := runtime.GOMAXPROCS(2)
	defer runtime.GOMAXPROCS(old)

	var mu sync.Mutex
	c := sync.NewCond(&mu)

	turn := 0
	stop := false
	done := make(chan struct{})

	go func() {
		for {
			mu.Lock()
			for turn != 1 {
				c.Wait()
			}
			s := stop
			turn = 0
			mu.Unlock()
			c.Signal()

			if s {
				break
			}
		}
		close(done)
	}()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		mu.Lock()
		turn = 1
		mu.Unlock()
		c.Signal()

		mu.Lock()
		for turn != 0 {
			c.Wait()
		}
		mu.Unlock()
	}

	b.StopTimer()

	mu.Lock()
	stop = true
	turn = 1
	mu.Unlock()
	c.Broadcast()
	<-done
}
