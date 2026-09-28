// The Go side of the compress/lzw benchmarks.
//
// These are Go's BenchmarkDecoder and BenchmarkEncoder from reader_test.go and
// writer_test.go. Go runs each size as a sub-benchmark named like 1e4 or
// 1e-Reuse4, which pairs.txt cannot name, so each one is a function of its own
// here, LzwDecode1e4 and LzwDecodeReuse1e4, with the bodies copied from Go's.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"compress/lzw"
	"io"
	"os"
	"runtime"
	"testing"
)

func lzwDigits(b *testing.B) []byte {
	buf, err := os.ReadFile("../testdata/e.txt")
	if err != nil {
		b.Fatal(err)
	}
	return buf
}

func lzwInputBuf(buf []byte, n int) []byte {
	compressed := new(bytes.Buffer)
	w := lzw.NewWriter(compressed, lzw.LSB, 8)
	for i := 0; i < n; i += len(buf) {
		if len(buf) > n-i {
			buf = buf[:n-i]
		}
		w.Write(buf)
	}
	w.Close()
	return compressed.Bytes()
}

func lzwSized(buf0 []byte, n int) []byte {
	buf1 := make([]byte, n)
	for i := 0; i < n; i += len(buf0) {
		if len(buf0) > n-i {
			buf0 = buf0[:n-i]
		}
		copy(buf1[i:], buf0)
	}
	return buf1
}

func lzwDecode(b *testing.B, n int) {
	buf := lzwDigits(b)
	b.StopTimer()
	b.SetBytes(int64(n))
	buf1 := lzwInputBuf(buf, n)
	runtime.GC()
	b.StartTimer()
	for i := 0; i < b.N; i++ {
		io.Copy(io.Discard, lzw.NewReader(bytes.NewReader(buf1), lzw.LSB, 8))
	}
}

func lzwDecodeReuse(b *testing.B, n int) {
	buf := lzwDigits(b)
	b.StopTimer()
	b.SetBytes(int64(n))
	buf1 := lzwInputBuf(buf, n)
	runtime.GC()
	b.StartTimer()
	r := lzw.NewReader(bytes.NewReader(buf1), lzw.LSB, 8)
	for i := 0; i < b.N; i++ {
		io.Copy(io.Discard, r)
		r.Close()
		r.(*lzw.Reader).Reset(bytes.NewReader(buf1), lzw.LSB, 8)
	}
}

func lzwEncode(b *testing.B, n int) {
	buf1 := lzwSized(lzwDigits(b), n)
	runtime.GC()
	b.ResetTimer()
	b.SetBytes(int64(n))
	for i := 0; i < b.N; i++ {
		w := lzw.NewWriter(io.Discard, lzw.LSB, 8)
		w.Write(buf1)
		w.Close()
	}
}

func lzwEncodeReuse(b *testing.B, n int) {
	buf1 := lzwSized(lzwDigits(b), n)
	runtime.GC()
	b.ResetTimer()
	b.SetBytes(int64(n))
	w := lzw.NewWriter(io.Discard, lzw.LSB, 8)
	for i := 0; i < b.N; i++ {
		w.Write(buf1)
		w.Close()
		w.(*lzw.Writer).Reset(io.Discard, lzw.LSB, 8)
	}
}

func BenchmarkLzwDecode1e4(b *testing.B)      { lzwDecode(b, 10000) }
func BenchmarkLzwDecode1e5(b *testing.B)      { lzwDecode(b, 100000) }
func BenchmarkLzwDecode1e6(b *testing.B)      { lzwDecode(b, 1000000) }
func BenchmarkLzwDecodeReuse1e4(b *testing.B) { lzwDecodeReuse(b, 10000) }
func BenchmarkLzwDecodeReuse1e5(b *testing.B) { lzwDecodeReuse(b, 100000) }
func BenchmarkLzwDecodeReuse1e6(b *testing.B) { lzwDecodeReuse(b, 1000000) }
func BenchmarkLzwEncode1e4(b *testing.B)      { lzwEncode(b, 10000) }
func BenchmarkLzwEncode1e5(b *testing.B)      { lzwEncode(b, 100000) }
func BenchmarkLzwEncode1e6(b *testing.B)      { lzwEncode(b, 1000000) }
func BenchmarkLzwEncodeReuse1e4(b *testing.B) { lzwEncodeReuse(b, 10000) }
func BenchmarkLzwEncodeReuse1e5(b *testing.B) { lzwEncodeReuse(b, 100000) }
func BenchmarkLzwEncodeReuse1e6(b *testing.B) { lzwEncodeReuse(b, 1000000) }
