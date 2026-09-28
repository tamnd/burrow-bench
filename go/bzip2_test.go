// The Go side of the compress/bzip2 benchmarks.
//
// These are Go's BenchmarkDecodeDigits, BenchmarkDecodeNewton and
// BenchmarkDecodeRand from bzip2_test.go, with the bodies copied from Go's.
// The .bz2 files in ../testdata are the ones in Go's compress/bzip2/testdata.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"compress/bzip2"
	"io"
	"os"
	"testing"
)

func bzip2Decode(b *testing.B, file string) {
	compressed, err := os.ReadFile(file)
	if err != nil {
		b.Fatal(err)
	}
	uncompressedSize, err := io.Copy(io.Discard, bzip2.NewReader(bytes.NewReader(compressed)))
	if err != nil {
		b.Fatal(err)
	}

	b.SetBytes(uncompressedSize)
	b.ReportAllocs()
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		r := bytes.NewReader(compressed)
		io.Copy(io.Discard, bzip2.NewReader(r))
	}
}

func BenchmarkBzip2DecodeDigits(b *testing.B) { bzip2Decode(b, "../testdata/e.txt.bz2") }

func BenchmarkBzip2DecodeNewton(b *testing.B) {
	bzip2Decode(b, "../testdata/Isaac.Newton-Opticks.txt.bz2")
}

func BenchmarkBzip2DecodeRand(b *testing.B) { bzip2Decode(b, "../testdata/random.data.bz2") }
