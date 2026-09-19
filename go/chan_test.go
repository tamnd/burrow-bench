// The Go side of the channel benchmarks.
//
// These are the rows the sync file has been pointing at. BenchmarkNotePingpong
// on the C side is a volley through the kernel, because before channels landed
// that was the only way burrow had to hand work between two threads. This file
// is the Go program that the C ping pong rows are now written to match, and the
// distance between them is what the whole scheduler was for.
//
// GOMAXPROCS is 1 everywhere, which is what the C side does with
// runtime_gomaxprocs, and for the reason go/sched_test.go gives: with more than
// one P a ping pong measures the cache line two cores are fighting over.
//
// Every channel is a chan int rather than a chan struct{}. Go's own channel
// benchmarks mostly use the empty struct, which is faster because there is
// nothing to copy, and using it here would have measured the parts of a channel
// that are not the copy. Eight bytes is what a real channel carries.
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

// A send and a receive on a buffered channel with nobody else in the program.
// Neither blocks and the buffer never holds more than one value, so this is the
// lock, the ring arithmetic and the copy, and nothing else.
func BenchmarkChanUncontended(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	c := make(chan int, 1)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		c <- 1
		<-c
	}
}

// A receive from an empty channel that answers instead of blocking, which is
// what the C side's chan_try_recv is and what a poll loop pays per turn.
func BenchmarkChanNonblocking(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	c := make(chan int, 1)
	n := 0

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		select {
		case <-c:
			n++
		default:
		}
	}

	_ = n
}

// A value going there and back between two goroutines. One iteration is a
// volley: two goroutine switches, and on the unbuffered pair four blocking
// operations.
//
// The child is stopped with one more volley rather than by closing the channel,
// so that no iteration takes a different path through the channel than the
// others, and the C side stops its child the same way.
func pingpong(b *testing.B, capacity int) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	there := make(chan int, capacity)
	back := make(chan int, capacity)
	gone := make(chan struct{})

	var stop uint32

	go func() {
		for {
			v := <-there
			if atomic.LoadUint32(&stop) != 0 {
				break
			}
			back <- v
		}
		close(gone)
	}()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		there <- 1
		<-back
	}

	b.StopTimer()

	atomic.StoreUint32(&stop, 1)
	there <- 0
	<-gone
}

func BenchmarkChanPingpongUnbuffered(b *testing.B) { pingpong(b, 0) }
func BenchmarkChanPingpongBuffered(b *testing.B)   { pingpong(b, 1) }

// One value moving one way down a buffered channel, which is the shape of every
// pipeline anybody has written. A capacity of a hundred, so that a value
// arriving while there is room costs a lock and a copy and no scheduling, and
// the pipeline pays the scheduler once per buffer full rather than once per
// value.
func BenchmarkChanProdCons(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	c := make(chan int, 100)

	go func() {
		for i := 0; i < b.N; i++ {
			c <- i
		}
		close(c)
	}()

	b.ResetTimer()

	sum := 0
	for v := range c {
		sum += v
	}

	b.StopTimer()

	_ = sum
}
