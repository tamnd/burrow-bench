// The Go side of the strconv benchmarks.
//
// The inputs are Go's own, from BenchmarkAppendFloat, BenchmarkAtof64 and the
// quoting benchmarks in strconv, and every formatting row appends into a
// buffer that already has room, which is how Go's own benchmarks are written.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"strconv"
	"testing"
)

var (
	strconvBuf  [128]byte
	sinkBytes   []byte
	sinkFloat   float64
	sinkStrconv string
)

// ------------------------------------------------------------------ integers

func BenchmarkStrconvAtoi(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkInt, _ = strconv.Atoi("12345678")
	}
}

func BenchmarkStrconvAtoiNeg(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkInt, _ = strconv.Atoi("-12345678")
	}
}

func BenchmarkStrconvParseIntHex(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkInt64, _ = strconv.ParseInt("7fffffffffffffff", 16, 64)
	}
}

func BenchmarkStrconvAppendInt(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkBytes = strconv.AppendInt(strconvBuf[:0], -1234567890123456789, 10)
	}
}

func BenchmarkStrconvAppendIntSmall(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkBytes = strconv.AppendInt(strconvBuf[:0], int64(i&63), 10)
	}
}

func BenchmarkStrconvAppendUintHex(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkBytes = strconv.AppendUint(strconvBuf[:0], 0xdeadbeefcafef00d, 16)
	}
}

// ---------------------------------------------------------------- floats in

func atof64(b *testing.B, s string) {
	for i := 0; i < b.N; i++ {
		sinkFloat, _ = strconv.ParseFloat(s, 64)
	}
}

func BenchmarkStrconvAtof64Decimal(b *testing.B)  { atof64(b, "33909") }
func BenchmarkStrconvAtof64Float(b *testing.B)    { atof64(b, "339.7784") }
func BenchmarkStrconvAtof64Exp(b *testing.B)      { atof64(b, "-5.09e75") }
func BenchmarkStrconvAtof64Big(b *testing.B)      { atof64(b, "123456789123456789123456789") }
func BenchmarkStrconvAtof64Shortest(b *testing.B) { atof64(b, "2.2250738585072014e-308") }
func BenchmarkStrconvAtof64Hard(b *testing.B)     { atof64(b, "622666234635.321003e-320") }

func BenchmarkStrconvAtof32Float(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkFloat, _ = strconv.ParseFloat("339.7784", 32)
	}
}

// --------------------------------------------------------------- floats out

func ftoa(b *testing.B, f float64, fmt byte, prec, bits int) {
	for i := 0; i < b.N; i++ {
		sinkBytes = strconv.AppendFloat(strconvBuf[:0], f, fmt, prec, bits)
	}
}

func BenchmarkStrconvFtoaDecimal(b *testing.B)   { ftoa(b, 33909, 'g', -1, 64) }
func BenchmarkStrconvFtoaFloat(b *testing.B)     { ftoa(b, 339.7784, 'g', -1, 64) }
func BenchmarkStrconvFtoaExp(b *testing.B)       { ftoa(b, -5.09e75, 'g', -1, 64) }
func BenchmarkStrconvFtoaNegExp(b *testing.B)    { ftoa(b, -5.11e-95, 'g', -1, 64) }
func BenchmarkStrconvFtoaLongExp(b *testing.B)   { ftoa(b, 1.234567890123456e-78, 'g', -1, 64) }
func BenchmarkStrconvFtoaBig(b *testing.B)       { ftoa(b, 123456789123456789123456789, 'g', -1, 64) }
func BenchmarkStrconvFtoaBinaryExp(b *testing.B) { ftoa(b, -1, 'b', -1, 64) }
func BenchmarkStrconvFtoa32Float(b *testing.B) {
	ftoa(b, float64(float32(339.7784)), 'g', -1, 32)
}
func BenchmarkStrconvFtoa32Shortest(b *testing.B) { ftoa(b, float64(float32(1.6)), 'g', -1, 32) }
func BenchmarkStrconvFtoa64Fixed1(b *testing.B)   { ftoa(b, 123456, 'e', 3, 64) }
func BenchmarkStrconvFtoa64Fixed3(b *testing.B)   { ftoa(b, 1.23456e+78, 'e', 3, 64) }
func BenchmarkStrconvFtoa64Fixed12(b *testing.B)  { ftoa(b, 1.23456789e-78, 'e', 12, 64) }
func BenchmarkStrconvFtoa64Fixed17(b *testing.B)  { ftoa(b, 1.2345678901234567e-78, 'e', 17, 64) }
func BenchmarkStrconvFtoa64F(b *testing.B)        { ftoa(b, 339.7784, 'f', 6, 64) }
func BenchmarkStrconvFtoa64SlowPath(b *testing.B) { ftoa(b, 622666234635.3213e-320, 'e', -1, 64) }

// ------------------------------------------------------------------- quoting

func BenchmarkStrconvAppendQuote(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkBytes = strconv.AppendQuote(strconvBuf[:0], "\a\b\f\r\n\t\v\a\b\f\r\n\t\v\a\b\f\r\n\t\v")
	}
}

func BenchmarkStrconvAppendQuoteRune(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkBytes = strconv.AppendQuoteRune(strconvBuf[:0], '\a')
	}
}

func BenchmarkStrconvUnquoteEasy(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStrconv, _ = strconv.Unquote(`"Give me a rock, paper and scissors and I will move the world."`)
	}
}

func BenchmarkStrconvUnquoteHard(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStrconv, _ = strconv.Unquote(`"\x47ive me a \x72ock, \x70aper and \x73cissors and \x49 will move the world."`)
	}
}
