// The Go side of the runtime benchmarks.
//
// There is one row here and there are six on the C side, which is not an
// oversight. The other five measure the pieces a goroutine is assembled from,
// and Go does not let a program near any of them. There is no way to allocate a
// goroutine stack, no way to build a context over one and no way to switch to
// it, because in Go those are things the runtime does and not things a user
// asks for. Pairing them with something Go does export would be making up a
// comparison, so they are left unpaired and the C file says why.
//
// What can be compared is the shape: two pieces of work passing a turn back and
// forth on one thread. Go writes that with two goroutines and an unbuffered
// channel, and burrow writes it today with two contexts and a switch. Go's
// version does strictly more, since it goes through the scheduler and a channel
// and burrow's saves registers and changes a stack pointer, so the gap is not a
// score. It is the budget the burrow scheduler has to fit inside to end up
// where Go already is.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"runtime"
	"testing"
)

// Two goroutines and one turn between them, pinned to a single P so that the
// handoff is a switch rather than two processors running at once. One volley
// per iteration, which is two goroutine switches, the same count as the two
// context switches the C row does.
func BenchmarkGoroutineSwitch(b *testing.B) {
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
