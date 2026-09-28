// The Go side of the compress/flate benchmarks.
//
// These are Go's BenchmarkEncode and BenchmarkDecode from writer_test.go and
// reader_test.go. Go runs each file, level and size as a sub-benchmark named
// like Digits/Speed/1e5, which pairs.txt cannot name, so each one is a
// function of its own here, FlateEncodeDigitsSpeed1e5, with the bodies copied
// from Go's. The files are the ones in ../testdata, the same as Go's.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"compress/flate"
	"io"
	"os"
	"runtime"
	"testing"
)

func flateEncode(b *testing.B, file string, level, n int) {
	buf0, err := os.ReadFile(file)
	if err != nil {
		b.Fatal(err)
	}
	b.StopTimer()
	b.SetBytes(int64(n))

	buf1 := make([]byte, n)
	for i := 0; i < n; i += len(buf0) {
		if len(buf0) > n-i {
			buf0 = buf0[:n-i]
		}
		copy(buf1[i:], buf0)
	}
	buf0 = nil
	w, err := flate.NewWriter(io.Discard, level)
	if err != nil {
		b.Fatal(err)
	}
	runtime.GC()
	b.StartTimer()
	for i := 0; i < b.N; i++ {
		w.Reset(io.Discard)
		w.Write(buf1)
		w.Close()
	}
}

func flateDecode(b *testing.B, file string, level, n int) {
	buf0, err := os.ReadFile(file)
	if err != nil {
		b.Fatal(err)
	}
	b.ReportAllocs()
	b.StopTimer()
	b.SetBytes(int64(n))

	compressed := new(bytes.Buffer)
	w, err := flate.NewWriter(compressed, level)
	if err != nil {
		b.Fatal(err)
	}
	for i := 0; i < n; i += len(buf0) {
		if len(buf0) > n-i {
			buf0 = buf0[:n-i]
		}
		io.Copy(w, bytes.NewReader(buf0))
	}
	w.Close()
	buf1 := compressed.Bytes()
	buf0, compressed, w = nil, nil, nil
	runtime.GC()
	b.StartTimer()
	for i := 0; i < b.N; i++ {
		io.Copy(io.Discard, flate.NewReader(bytes.NewReader(buf1)))
	}
}

func BenchmarkFlateEncodeDigitsHuffman1e4(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.HuffmanOnly, 10000)
}

func BenchmarkFlateEncodeDigitsHuffman1e5(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.HuffmanOnly, 100000)
}

func BenchmarkFlateEncodeDigitsHuffman1e6(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.HuffmanOnly, 1000000)
}

func BenchmarkFlateEncodeDigitsSpeed1e4(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.BestSpeed, 10000)
}

func BenchmarkFlateEncodeDigitsSpeed1e5(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.BestSpeed, 100000)
}

func BenchmarkFlateEncodeDigitsSpeed1e6(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.BestSpeed, 1000000)
}

func BenchmarkFlateEncodeDigitsDefault1e4(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.DefaultCompression, 10000)
}

func BenchmarkFlateEncodeDigitsDefault1e5(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.DefaultCompression, 100000)
}

func BenchmarkFlateEncodeDigitsDefault1e6(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.DefaultCompression, 1000000)
}

func BenchmarkFlateEncodeDigitsCompression1e4(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.BestCompression, 10000)
}

func BenchmarkFlateEncodeDigitsCompression1e5(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.BestCompression, 100000)
}

func BenchmarkFlateEncodeDigitsCompression1e6(b *testing.B) {
	flateEncode(b, "../testdata/e.txt", flate.BestCompression, 1000000)
}

func BenchmarkFlateEncodeNewtonHuffman1e4(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.HuffmanOnly, 10000)
}

func BenchmarkFlateEncodeNewtonHuffman1e5(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.HuffmanOnly, 100000)
}

func BenchmarkFlateEncodeNewtonHuffman1e6(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.HuffmanOnly, 1000000)
}

func BenchmarkFlateEncodeNewtonSpeed1e4(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestSpeed, 10000)
}

func BenchmarkFlateEncodeNewtonSpeed1e5(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestSpeed, 100000)
}

func BenchmarkFlateEncodeNewtonSpeed1e6(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestSpeed, 1000000)
}

func BenchmarkFlateEncodeNewtonDefault1e4(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.DefaultCompression, 10000)
}

func BenchmarkFlateEncodeNewtonDefault1e5(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.DefaultCompression, 100000)
}

func BenchmarkFlateEncodeNewtonDefault1e6(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.DefaultCompression, 1000000)
}

func BenchmarkFlateEncodeNewtonCompression1e4(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestCompression, 10000)
}

func BenchmarkFlateEncodeNewtonCompression1e5(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestCompression, 100000)
}

func BenchmarkFlateEncodeNewtonCompression1e6(b *testing.B) {
	flateEncode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestCompression, 1000000)
}

func BenchmarkFlateDecodeDigitsHuffman1e4(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.HuffmanOnly, 10000)
}

func BenchmarkFlateDecodeDigitsHuffman1e5(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.HuffmanOnly, 100000)
}

func BenchmarkFlateDecodeDigitsHuffman1e6(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.HuffmanOnly, 1000000)
}

func BenchmarkFlateDecodeDigitsSpeed1e4(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.BestSpeed, 10000)
}

func BenchmarkFlateDecodeDigitsSpeed1e5(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.BestSpeed, 100000)
}

func BenchmarkFlateDecodeDigitsSpeed1e6(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.BestSpeed, 1000000)
}

func BenchmarkFlateDecodeDigitsDefault1e4(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.DefaultCompression, 10000)
}

func BenchmarkFlateDecodeDigitsDefault1e5(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.DefaultCompression, 100000)
}

func BenchmarkFlateDecodeDigitsDefault1e6(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.DefaultCompression, 1000000)
}

func BenchmarkFlateDecodeDigitsCompression1e4(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.BestCompression, 10000)
}

func BenchmarkFlateDecodeDigitsCompression1e5(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.BestCompression, 100000)
}

func BenchmarkFlateDecodeDigitsCompression1e6(b *testing.B) {
	flateDecode(b, "../testdata/e.txt", flate.BestCompression, 1000000)
}

func BenchmarkFlateDecodeNewtonHuffman1e4(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.HuffmanOnly, 10000)
}

func BenchmarkFlateDecodeNewtonHuffman1e5(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.HuffmanOnly, 100000)
}

func BenchmarkFlateDecodeNewtonHuffman1e6(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.HuffmanOnly, 1000000)
}

func BenchmarkFlateDecodeNewtonSpeed1e4(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestSpeed, 10000)
}

func BenchmarkFlateDecodeNewtonSpeed1e5(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestSpeed, 100000)
}

func BenchmarkFlateDecodeNewtonSpeed1e6(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestSpeed, 1000000)
}

func BenchmarkFlateDecodeNewtonDefault1e4(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.DefaultCompression, 10000)
}

func BenchmarkFlateDecodeNewtonDefault1e5(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.DefaultCompression, 100000)
}

func BenchmarkFlateDecodeNewtonDefault1e6(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.DefaultCompression, 1000000)
}

func BenchmarkFlateDecodeNewtonCompression1e4(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestCompression, 10000)
}

func BenchmarkFlateDecodeNewtonCompression1e5(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestCompression, 100000)
}

func BenchmarkFlateDecodeNewtonCompression1e6(b *testing.B) {
	flateDecode(b, "../testdata/Isaac.Newton-Opticks.txt", flate.BestCompression, 1000000)
}
