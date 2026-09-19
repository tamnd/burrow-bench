// The Go side of the timer benchmarks.
//
// Four of the five C rows pair cleanly, because AfterFunc, Stop, Reset and
// Sleep are the calls both sides have and they mean the same thing on both
// sides. The fifth, the burst, pairs with a caveat that is written down at the
// bottom rather than hidden.
//
// GOMAXPROCS is 1 everywhere, which is what the C side does with
// runtime_gomaxprocs and for the same reason: timers live in a heap per P, so
// with more than one P the work spreads out and the benchmark measures the
// machine instead of the timer.
//
// One difference is structural and worth knowing before reading the numbers.
// Go allocates a timer and burrow is handed an allocator, so the arm and stop
// row is Go's allocator against an arena bump. That is a fair comparison of the
// two programs somebody would actually write, and it is not a fair comparison
// of the two timer implementations. The reset and stop row is the one with no
// allocator in it at all, and it is the row to read.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"sync/atomic"
	"testing"
	"time"
)

// An hour, so nothing here ever fires except where a row means it to.
const never = time.Hour

var fired uint32

func onFire() {
	atomic.AddUint32(&fired, 1)
}

// A whole timer, from nothing to stopped. Go allocates it here, which the C
// side also does, out of an arena rather than out of the heap.
func BenchmarkTimerArmStop(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		t := time.AfterFunc(never, onFire)
		t.Stop()
	}
}

// One timer, armed and stopped over and over, which is a connection with a read
// deadline on it. This is the row that matters and it is the one with no
// allocator in it.
func BenchmarkTimerResetStop(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	t := time.AfterFunc(never, onFire)
	t.Stop()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		t.Reset(never)
		t.Stop()
	}
}

// The same with a thousand other timers already in the heap, which is what a
// server holding a thousand connections looks like. The question both sides are
// being asked is whether the cost of a deadline grows with the number of them,
// and on both sides the answer should be no.
func BenchmarkTimerResetStopDeep(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	deep := make([]*time.Timer, 1000)
	for i := range deep {
		deep[i] = time.AfterFunc(never, onFire)
	}
	defer func() {
		for _, t := range deep {
			t.Stop()
		}
	}()

	t := time.AfterFunc(never, onFire)
	t.Stop()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		t.Reset(never)
		t.Stop()
	}
}

// A one millisecond sleep, which does not come back in one millisecond. The
// number worth reading is the difference between this and 1000000 nanoseconds,
// because that difference is the wakeup path on both sides.
func BenchmarkTimerSleep1ms(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		time.Sleep(time.Millisecond)
	}
}

// A thousand timers all due at once and the wait until every callback has run.
// Divide by a thousand for one.
//
// The caveat on this row: the spin on the counter with a Gosched in it is what
// the C side does, and it is there because burrow has no wait group yet. A Go
// program would use one. So both sides are paying for the spin and the row is
// still a fair comparison, but neither number is what this costs in a program
// that waits properly.
func BenchmarkTimerFireBurst(b *testing.B) {
	old := runtime.GOMAXPROCS(1)
	defer runtime.GOMAXPROCS(old)

	const burst = 1000

	timers := make([]*time.Timer, burst)
	for i := range timers {
		timers[i] = time.AfterFunc(never, onFire)
		timers[i].Stop()
	}

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		atomic.StoreUint32(&fired, 0)

		for _, t := range timers {
			t.Reset(0)
		}

		for atomic.LoadUint32(&fired) < burst {
			runtime.Gosched()
		}
	}
}
