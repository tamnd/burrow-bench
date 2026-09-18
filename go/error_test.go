// The Go side of the Error benchmarks.
//
// Two of these do not have an honest C counterpart yet and are here anyway.
// BenchmarkErrorfWrap measures fmt.Errorf with %w, which is how Go wraps in
// practice and which burrow does not have until fmt arrives. Having the Go
// number first means the C one lands next to a target instead of next to
// nothing.
//
// The construction benchmarks compare an arena that resets once per iteration
// against Go's heap and its collector, so those time columns are two different
// experiments. The allocation counts are the same experiment and are the
// column to read.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"errors"
	"fmt"
	"testing"
)

// Five deep, the same as the C side. About what a chain looks like once a
// request has been through a handler, a store and a driver.
const depth = 5

var (
	errClosed   = errors.New("file already closed")
	errNotExist = errors.New("file does not exist")

	sinkBool bool
	sinkErr  error
	sinkStr  string
)

// wrapped is the shape fmt.Errorf produces, written out so that the chain is
// built with the same number of layers as the C side and so that building it
// stays outside the timed loop.
type wrapped struct {
	text  string
	cause error
}

func (w *wrapped) Error() string { return w.text }
func (w *wrapped) Unwrap() error { return w.cause }

func buildChain() error {
	var e error = errClosed
	for i := 0; i < depth; i++ {
		e = &wrapped{text: "layer", cause: e}
	}
	return e
}

// The one that runs once per read in every loop that reads to the end.
func BenchmarkErrorIsSentinel(b *testing.B) {
	err := error(errClosed)
	for i := 0; i < b.N; i++ {
		sinkBool = errors.Is(err, errClosed)
	}
}

// The miss, which is the more common answer when the code is looking for one
// particular failure among several.
func BenchmarkErrorIsSentinelMiss(b *testing.B) {
	err := error(errClosed)
	for i := 0; i < b.N; i++ {
		sinkBool = errors.Is(err, errNotExist)
	}
}

func BenchmarkErrorIsChain(b *testing.B) {
	err := buildChain()
	for i := 0; i < b.N; i++ {
		sinkBool = errors.Is(err, errClosed)
	}
}

// Nothing to find, so the whole chain gets walked every time.
func BenchmarkErrorIsChainMiss(b *testing.B) {
	err := buildChain()
	for i := 0; i < b.N; i++ {
		sinkBool = errors.Is(err, errNotExist)
	}
}

func BenchmarkErrorAsChain(b *testing.B) {
	err := buildChain()
	var target *wrapped
	for i := 0; i < b.N; i++ {
		sinkBool = errors.As(err, &target)
	}
}

func BenchmarkErrorMessage(b *testing.B) {
	err := buildChain()
	for i := 0; i < b.N; i++ {
		sinkStr = err.Error()
	}
}

// Returning nil, which is what the overwhelming majority of calls in a working
// program do. The C side returns a two word struct through registers and this
// returns a nil interface, so this is the one pair where the shapes genuinely
// differ and the number is worth knowing for that reason.
func succeeds(i int) error {
	if i < 0 {
		return errClosed
	}
	return nil
}

func BenchmarkErrorReturnOk(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkErr = succeeds(i)
	}
}

func BenchmarkErrorNew(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkErr = errors.New("connection reset by peer")
	}
}

// A sentinel, which costs nothing to use on either side. Go pays for its
// package level vars once at init and nobody measures it there either.
func BenchmarkErrorSentinelUse(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkErr = errClosed
	}
}

func BenchmarkErrorJoinTwo(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkErr = errors.Join(errClosed, errNotExist)
	}
}

// The depth first walk over a tree rather than a chain. The join is built
// outside the loop, so this is the search and not the construction.
func BenchmarkErrorIsTree(b *testing.B) {
	inner := errors.Join(errNotExist, errClosed)
	err := errors.Join(errNotExist, inner)
	for i := 0; i < b.N; i++ {
		sinkBool = errors.Is(err, errClosed)
	}
}

// No C counterpart until fmt lands. Go's own wrapping, for the record, so that
// fmt_errorf has a number to be measured against when it exists. Note what it
// costs next to BenchmarkErrorNew: the format string is parsed every time.
func BenchmarkErrorfWrap(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkErr = fmt.Errorf("open config: %w", errNotExist)
	}
}
