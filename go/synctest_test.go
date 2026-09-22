// The Go side of the bubble benchmarks.
//
// These are as close to a straight pair as the two packages allow. burrow's
// synctest is a port of this one, so the rows here and the rows in
// bench/synctest_bench.c are two implementations of one design rather than two
// designs that happen to have a similar name.
//
// Two things had to be worked around and both are worth writing down rather
// than leaving as puzzles for whoever reads this next.
//
// The first is getting a bubble open at all. Go 1.25 replaced synctest.Run
// with synctest.Test, which takes a *testing.T so that a deadlock inside a
// bubble fails the test instead of killing the process, and 1.27 removed Run
// entirely. A benchmark has a *testing.B and there is no way to get a
// *testing.T out of one. So these pass a zero value *testing.T, which is
// enough for the bubble to open and close and is the only thing these rows ask
// of it. Nothing in here asserts, so the parts of a T that a zero value does
// not have are parts nothing reaches. If a bubble in here ever did deadlock
// the failure would come out as a nil dereference rather than a test failure,
// which is ugly and is still a failure.
//
// The second is that time inside a bubble is not real, and the testing package
// tells the time with time.Now like everything else. So b.ResetTimer and
// b.StopTimer called from inside a bubble measure the fake clock, and the
// first version of this file reported the sleep row at exactly 1000000000
// ns/op and four other rows at nothing at all, because no fake time had
// passed. Every timer call here is therefore left to the framework, which
// makes them on the benchmark's own goroutine and that one is not in the
// bubble. The C side does not have this problem: bench_now_ns goes straight to
// CLOCK_MONOTONIC and has never heard of a bubble.
//
// One cost of that is the setup inside a bubble is counted here where the C
// side excludes it with bench_pause. It is one channel and one goroutine
// against a b.N in the millions, so it is below the noise, and the alternative
// was rows that measure the wrong clock.
//
// GOMAXPROCS is 1 in all of them, the same as the C side does with
// runtime_gomaxprocs and for the same reason the scheduler benchmarks give:
// with more than one P the ping pong row measures the cache line two cores are
// sharing, which is a real thing to measure and a different benchmark.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"testing"
	"testing/synctest"
	"time"
)

// The bubble opener. See the note at the top of the file for why the T is a
// zero value.
func bubble(f func()) {
	synctest.Test(new(testing.T), func(*testing.T) { f() })
}

// Opening and closing a bubble with nothing in it. This is the fixed cost of
// the package, paid once per test, and it is a thing to know rather than a
// thing to optimise against.
func BenchmarkSynctestRunEmpty(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	for b.Loop() {
		bubble(func() {})
	}
}

// The same with one goroutine in it that returns immediately, so the
// difference between the two rows is what tracking one extra member costs.
func BenchmarkSynctestRunOneGoroutine(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	ran := 0
	for b.Loop() {
		bubble(func() {
			go func() { ran++ }()
		})
	}
	sink = ran
}

// Wait in an empty bubble, where the answer is already true when the question
// is asked. The check on its own with no scheduling in it.
func BenchmarkSynctestWaitAlone(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	n := b.N
	bubble(func() {
		for i := 0; i < n; i++ {
			synctest.Wait()
		}
	})
}

// Wait with a goroutine parked on a channel made inside the bubble, so the
// wait has to see the park happen. One wait, one send and one park per
// iteration, which is what the inside of a real test loop looks like.
func BenchmarkSynctestWaitParked(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	n := b.N
	got := 0
	bubble(func() {
		c := make(chan int)
		go func() {
			for range c {
				got++
			}
		}()

		for i := 0; i < n; i++ {
			synctest.Wait()
			c <- 1
		}

		close(c)
		synctest.Wait()
	})
	sink = got
}

// The ping pong from chan_test.go with both channels made inside the bubble,
// which is what makes the waits durable and the bubble's counters actually do
// something.
func BenchmarkSynctestChanPingpong(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	n := b.N
	bubble(func() {
		c1 := make(chan int)
		c2 := make(chan int)
		done := make(chan struct{})

		go func() {
			for v := range c1 {
				if v == 0 {
					break
				}
				c2 <- v
			}
			close(done)
		}()

		for i := 0; i < n; i++ {
			c1 <- 1
			<-c2
		}

		// One more volley rather than a close, the same as the C side, so
		// that the last iteration takes the same path as every other one.
		c1 <- 0
		<-done
	})
}

// Launch, switch in, switch out, with the bubble's membership bookkeeping on
// top. Read it against BenchmarkGoroutineStart in sched_test.go.
func BenchmarkSynctestGo(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	n := b.N
	ran := 0
	bubble(func() {
		for i := 0; i < n; i++ {
			go func() { ran++ }()
			runtime.Gosched()
		}
	})
	sink = ran
}

// A sleep of a second that takes nanoseconds, because inside a bubble the
// clock moves when everybody is blocked rather than when the machine says so.
// There is nothing to read this against: the thing it replaces is a real sleep
// and a real sleep of a second costs a second.
func BenchmarkSynctestSleep(b *testing.B) {
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(1))

	n := b.N
	bubble(func() {
		for i := 0; i < n; i++ {
			time.Sleep(time.Second)
		}
	})
}
