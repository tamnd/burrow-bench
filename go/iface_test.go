// The Go side of the interface dispatch benchmarks.
//
// The rows here are the ones burrow's design document makes a claim about: a
// call through an interface, the same call without one, a call site that sees
// more than one dynamic type, converting to an interface, narrowing to an
// embedded one, and asserting back out.
//
// One row is uneven on purpose and it is the interesting one. BenchmarkIfaceNarrow
// assigns an io.ReadWriter to an io.Reader, which Go does through
// runtime.convI2I and a cache lookup, because the itab for the narrower
// interface is a separate object that has to be found. burrow keeps the
// narrower vtable inside the wider one, so its version is a member address.
// Different work, and the comment on the C side says so too, because the point
// of the row is the difference.
//
// The Any rows are the other uneven pair. Go boxes small values into the
// interface word or onto the heap depending on what they are, and caches the
// first few hundred small integers, so BenchmarkAnyBox uses a number past that
// cache to make sure it is measuring an allocation. The C side allocates from
// an arena that resets once per iteration. Read the allocation columns.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"io"
	"testing"
)

// adder is the smallest interface that still measures a real call: one method,
// one argument, one word back.
type adder interface {
	add(d int) int
}

type counter struct{ n int }

func (c *counter) add(d int) int {
	c.n += d
	return c.n
}

// doubler is a second implementation so that a call site can be made to see
// more than one dynamic type. Doubling rather than adding, so the two are not
// the same function under two names.
type doubler struct{ n int }

func (p *doubler) add(d int) int {
	p.n += d * 2
	return p.n
}

// sinkInt is declared by the slice benchmarks, which is why it is missing here.
var (
	sinkAny   any
	sinkIface adder
	sinkRdr   io.Reader
	sinkOK    bool
)

// The call the whole design is about. The value is stored in a package level
// variable so the compiler cannot prove the dynamic type and inline through it,
// which is the thing Go can do and C cannot and which would otherwise turn this
// row into a copy of the one below it.
var globalAdder adder = &counter{}

func BenchmarkIfaceCall(b *testing.B) {
	v := globalAdder
	for i := 0; i < b.N; i++ {
		sinkInt = v.add(1)
	}
}

// The same work without the interface, which both compilers inline. The gap
// against IfaceCall is what dispatch costs.
func BenchmarkDirectCall(b *testing.B) {
	c := &counter{}
	for i := 0; i < b.N; i++ {
		sinkInt = c.add(1)
	}
}

// Two dynamic types at one call site, alternating every iteration, which is
// what a real program's interface calls look like and what IfaceCall above
// deliberately is not. It comes out level with IfaceCall on both sides, because
// an alternation of two targets is something any indirect branch predictor
// built in the last fifteen years gets right every time.
func BenchmarkIfaceCallTwoTypes(b *testing.B) {
	vs := [2]adder{&counter{}, &doubler{}}
	for i := 0; i < b.N; i++ {
		sinkInt = vs[i&1].add(1)
	}
}

// Making an interface value out of a concrete pointer, which is two moves
// whenever the compiler knows both types. The pointer alternates between two
// receivers so the pair has to be built every iteration rather than hoisted out
// of the loop, and the C side does the same.
func BenchmarkIfaceConvert(b *testing.B) {
	cs := [2]*counter{{}, {}}
	for i := 0; i < b.N; i++ {
		sinkIface = cs[i&1]
	}
}

// pipe is both halves of io.ReadWriter, so that narrowing has something real
// to narrow. Neither method runs here.
type pipe struct{ pos int }

func (q *pipe) Read(p []byte) (int, error) { return 0, io.EOF }

func (q *pipe) Write(p []byte) (int, error) {
	q.pos += len(p)
	return len(p), nil
}

var globalRW io.ReadWriter = &pipe{}

// Narrowing an embedded interface. Go goes through runtime.convI2I and its
// cache, burrow takes the address of a member. See the note at the top.
func BenchmarkIfaceNarrow(b *testing.B) {
	rw := globalRW
	for i := 0; i < b.N; i++ {
		sinkRdr = rw
	}
}

// v.(*T) when the answer is yes.
func BenchmarkIfaceAssertHit(b *testing.B) {
	v := globalAdder
	for i := 0; i < b.N; i++ {
		_, sinkOK = v.(*counter)
	}
}

// The same when the answer is no, which is what every arm of a type switch but
// one gets.
func BenchmarkIfaceAssertMiss(b *testing.B) {
	v := globalAdder
	for i := 0; i < b.N; i++ {
		_, sinkOK = v.(*doubler)
	}
}

// Making an any out of a pointer that already exists, which is what every
// argument to a print does. No allocation on either side.
func BenchmarkAnyMake(b *testing.B) {
	ns := [2]int{1234, 1234}
	for i := 0; i < b.N; i++ {
		sinkAny = &ns[i&1]
	}
}

// The one that allocates. The value is past the runtime's cache of small
// integers, so this is a real eight byte heap allocation and not a pointer into
// a table, and it changes every iteration, because a value that does not lets
// the compiler hoist the box out of the loop and the row reports zero.
func BenchmarkAnyBox(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkAny = 1234 + i
	}
}

var globalAny any = 1234

// Getting the value back out.
func BenchmarkAnyAssertHit(b *testing.B) {
	v := globalAny
	for i := 0; i < b.N; i++ {
		_, sinkOK = v.(int)
	}
}

// The miss, which is what the arms of a type switch cost before the right one
// matches.
func BenchmarkAnyAssertMiss(b *testing.B) {
	v := globalAny
	for i := 0; i < b.N; i++ {
		_, sinkOK = v.(int64)
	}
}

var (
	anyIntX any = 1234
	anyIntY any = 1234
	anyI64  any = int64(1234)
)

// == on two interface values holding the same type, which is what a map[any]V
// pays once the hash has found the slot.
func BenchmarkAnyEqualHit(b *testing.B) {
	x, y := anyIntX, anyIntY
	for i := 0; i < b.N; i++ {
		sinkOK = x == y
	}
}

// The same number stored as two different types, which Go answers from the
// type words without looking at either value.
func BenchmarkAnyEqualTypeMiss(b *testing.B) {
	x, y := anyIntX, anyI64
	for i := 0; i < b.N; i++ {
		sinkOK = x == y
	}
}

// Dispatch where it actually happens, which is a loop calling through an
// interface once per chunk of a stream.

const (
	streamBytes = 64 * 1024
	streamChunk = 512
	copyBufSize = 32 * 1024
)

// source hands out at most streamChunk bytes at a time and never touches them,
// because copying bytes is a memcpy benchmark and there is already one of
// those. It deliberately does not implement WriterTo, since io.Copy takes a
// completely different path when it finds one.
type source struct {
	pos int
	len int
}

func (s *source) Read(p []byte) (int, error) {
	left := s.len - s.pos
	if left <= 0 {
		return 0, io.EOF
	}
	n := left
	if n > len(p) {
		n = len(p)
	}
	if n > streamChunk {
		n = streamChunk
	}
	s.pos += n
	return n, nil
}

// countingSink counts and does not implement ReaderFrom, for the same reason.
// The name is longer than it wants to be because the memory benchmarks already
// have a package level sink.
type countingSink struct{ total int }

func (s *countingSink) Write(p []byte) (int, error) {
	s.total += len(p)
	return len(p), nil
}

// CopyBuffer rather than Copy on both sides, so that neither is measuring an
// allocation the other does not make. Sixty four kilobytes through a five
// hundred and twelve byte reader is a hundred and twenty eight trips round the
// loop and two hundred and fifty six interface calls per iteration.
func BenchmarkCopyBuffer64K(b *testing.B) {
	buf := make([]byte, copyBufSize)
	src := &source{len: streamBytes}
	dst := &countingSink{}
	for i := 0; i < b.N; i++ {
		src.pos = 0
		n, _ := io.CopyBuffer(dst, src, buf)
		sinkInt = int(n)
	}
}

// io.ReadFull into a buffer the reader cannot fill in one call, which is the
// shape of every header parse there has ever been.
func BenchmarkReadFull4K(b *testing.B) {
	buf := make([]byte, 4096)
	src := &source{len: streamBytes}
	for i := 0; i < b.N; i++ {
		src.pos = 0
		n, _ := io.ReadFull(src, buf)
		sinkInt = n
	}
}
