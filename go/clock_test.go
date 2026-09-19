// The Go side of the clock benchmarks.
//
// Only one of the four C rows has a pair here, and it is worth saying why the
// other three do not.
//
// Go's nanotime is not exported and there is no way to call it on its own from
// outside the runtime. time.Now is the closest exported thing and it is not the
// same work: it reads the wall clock and the monotonic clock together, which is
// one vdso call on Linux but more of it than burrow__nanotime does. time.Since
// is the honest pair, because it reads only the monotonic clock and subtracts,
// and nanotime_elapsed on the C side is written to be that and nothing else.
// The bare reading and the two reading interval are both on the C side too, and
// they are left unpaired rather than lined up against this one, because a
// benchmark that reads the clock twice is not the same work as one that reads
// it once however close the names are.
//
// The two timed sleep rows have no pair at all. The nearest Go program is a
// select with a time.After in it, which allocates a timer and a channel every
// time round and measures those instead, and the WaitGroup row in sync_test.go
// already covers the untimed version of the same gate.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"testing"
	"time"
)

var sinkDuration time.Duration

// One monotonic reading and the subtraction that goes with it, which is what
// time.Since is. The start is taken before the timer is reset so that it is not
// in the measurement, and the C side does the same, so both rows are one
// reading and one subtraction and the ratio means what it says.
func BenchmarkNanotimeElapsed(b *testing.B) {
	start := time.Now()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		sinkDuration = time.Since(start)
	}
}
