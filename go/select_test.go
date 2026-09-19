// The Go side of the select benchmarks.
//
// Read these against go/chan_test.go the way bench/select_bench.c says to read
// its rows against bench/chan_bench.c. The question is the same on both sides:
// a select over one ready case does the same copy a bare receive does, so
// whatever it costs on top of ChanUncontended is the price of the decision.
//
// One thing about writing these that is worth knowing before reading the
// numbers. Go's compiler rewrites a select with a single case and no default
// into the bare channel operation, so a one arm row here would be measuring a
// receive and would quietly beat the C side by the whole cost of select. Every
// select below has at least two arms for that reason, and the second arm on the
// ping pong rows is a quit channel nobody ever sends on, which is how the loops
// in real programs are written anyway.
//
// GOMAXPROCS is 1 everywhere, same as go/chan_test.go and same as the C side.
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

// One value bouncing between two buffered channels, so exactly one of the two
// arms can run on every pass and neither of them ever waits. This is Go's own
// BenchmarkSelectUncontended without the parallel wrapper.
func BenchmarkSelectUncontended(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	c1 := make(chan int, 1)
	c2 := make(chan int, 1)
	c1 <- 1

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		select {
		case v := <-c1:
			c2 <- v
		case v := <-c2:
			c1 <- v
		}
	}
}

// Two empty channels and a default, which is the poll loop: a select that
// answers rather than waiting, every time. ChanNonblocking is the same question
// asked of one channel.
func BenchmarkSelectDefault(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	c1 := make(chan int, 1)
	c2 := make(chan int, 1)
	n := 0

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		select {
		case <-c1:
			n++
		case <-c2:
			n++
		default:
		}
	}

	_ = n
}

// The same one ready arm as SelectUncontended with six more that are not, and
// the ready one moves around the ring so that no ordering of the case list is
// being flattered. Divide the difference between this and SelectUncontended by
// six and that is what one more arm costs.
//
// This is the row the C side cares most about, because burrow takes the channel
// locks by walking the case list and Go sorts a scratch array first. Eight arms
// against two is where that choice starts to show.
func BenchmarkSelectEightArms(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	var c [8]chan int
	for i := range c {
		c[i] = make(chan int, 1)
	}
	c[0] <- 1

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		select {
		case v := <-c[0]:
			c[1] <- v
		case v := <-c[1]:
			c[2] <- v
		case v := <-c[2]:
			c[3] <- v
		case v := <-c[3]:
			c[4] <- v
		case v := <-c[4]:
			c[5] <- v
		case v := <-c[5]:
			c[6] <- v
		case v := <-c[6]:
			c[7] <- v
		case v := <-c[7]:
			c[0] <- v
		}
	}
}

// A value going there and back between two goroutines, where all four
// operations in a volley are selects with a quit arm rather than bare channel
// operations. ChanPingpongUnbuffered is the same volley without the selects, so
// the distance between the two rows is what it costs to wait on more than one
// thing at a time.
//
// The child is stopped with one more volley rather than by closing anything, so
// that no iteration takes a different path through select than the others, and
// the C side stops its child the same way.
func BenchmarkSelectPingpong(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	there := make(chan int)
	back := make(chan int)
	quit := make(chan int)
	gone := make(chan struct{})

	var stop uint32

	go func() {
		for {
			var v int
			select {
			case v = <-there:
			case <-quit:
				close(gone)
				return
			}

			if atomic.LoadUint32(&stop) != 0 {
				break
			}

			select {
			case back <- v:
			case <-quit:
				close(gone)
				return
			}
		}
		close(gone)
	}()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		select {
		case there <- 1:
		case <-quit:
		}

		select {
		case <-back:
		case <-quit:
		}
	}

	b.StopTimer()

	atomic.StoreUint32(&stop, 1)
	there <- 0
	<-gone
}
