// The Go side of the math/big benchmarks.
//
// The operands come from the same splitmix64 stream with the same seeds as
// bench/big_bench.c, so both sides work on exactly the same numbers, and the
// receiver is reused across iterations as on the C side.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"math/big"
	"testing"
)

func splitmix(s *uint64) uint64 {
	*s += 0x9e3779b97f4a7c15
	z := *s
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb
	return z ^ (z >> 31)
}

func bigRand(n int, seed uint64) *big.Int {
	w := make([]big.Word, n)
	for i := range w {
		w[i] = big.Word(splitmix(&seed))
	}
	w[n-1] |= 1 << 63
	return new(big.Int).SetBits(w)
}

var bigSink int

func bigBinop(b *testing.B, op func(z, x, y *big.Int) *big.Int, nx, ny int) {
	x, y, z := bigRand(nx, 1), bigRand(ny, 2), new(big.Int)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		op(z, x, y)
	}
	bigSink = z.BitLen()
}

func add(z, x, y *big.Int) *big.Int        { return z.Add(x, y) }
func mul(z, x, y *big.Int) *big.Int        { return z.Mul(x, y) }
func quo(z, x, y *big.Int) *big.Int        { return z.Quo(x, y) }
func sqr(z, x, _ *big.Int) *big.Int        { return z.Mul(x, x) }
func sqrt(z, x, _ *big.Int) *big.Int       { return z.Sqrt(x) }
func gcd(z, x, y *big.Int) *big.Int        { return z.GCD(nil, nil, x, y) }
func modInverse(z, x, y *big.Int) *big.Int { return z.ModInverse(x, y) }

func BenchmarkBigAdd10(b *testing.B)        { bigBinop(b, add, 10, 10) }
func BenchmarkBigAdd1000(b *testing.B)      { bigBinop(b, add, 1000, 1000) }
func BenchmarkBigMul10(b *testing.B)        { bigBinop(b, mul, 10, 10) }
func BenchmarkBigMul100(b *testing.B)       { bigBinop(b, mul, 100, 100) }
func BenchmarkBigMul1000(b *testing.B)      { bigBinop(b, mul, 1000, 1000) }
func BenchmarkBigQuo10(b *testing.B)        { bigBinop(b, quo, 20, 10) }
func BenchmarkBigQuo100(b *testing.B)       { bigBinop(b, quo, 200, 100) }
func BenchmarkBigQuo1000(b *testing.B)      { bigBinop(b, quo, 2000, 1000) }
func BenchmarkBigSqr100(b *testing.B)       { bigBinop(b, sqr, 100, 1) }
func BenchmarkBigSqr1000(b *testing.B)      { bigBinop(b, sqr, 1000, 1) }
func BenchmarkBigSqrt100(b *testing.B)      { bigBinop(b, sqrt, 100, 1) }
func BenchmarkBigGCD10(b *testing.B)        { bigBinop(b, gcd, 10, 10) }
func BenchmarkBigGCD100(b *testing.B)       { bigBinop(b, gcd, 100, 100) }
func BenchmarkBigModInverse32(b *testing.B) { bigBinop(b, modInverse, 32, 32) }

func BenchmarkBigExpMod32(b *testing.B) {
	x, y, m, z := bigRand(32, 1), bigRand(32, 2), bigRand(32, 3), new(big.Int)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		z.Exp(x, y, m)
	}
	bigSink = z.BitLen()
}

func bigString(b *testing.B, n int) {
	x := bigRand(n, 1)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		bigSink += len(x.String())
	}
}

func BenchmarkBigString10(b *testing.B)   { bigString(b, 10) }
func BenchmarkBigString100(b *testing.B)  { bigString(b, 100) }
func BenchmarkBigString1000(b *testing.B) { bigString(b, 1000) }

func bigSetString(b *testing.B, n int) {
	s := bigRand(n, 1).String()
	z := new(big.Int)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, ok := z.SetString(s, 10); !ok {
			b.Fatal("bad number")
		}
	}
}

func BenchmarkBigSetString10(b *testing.B)   { bigSetString(b, 10) }
func BenchmarkBigSetString1000(b *testing.B) { bigSetString(b, 1000) }

func BenchmarkBigProbablyPrime521(b *testing.B) {
	p := new(big.Int).Lsh(big.NewInt(1), 521)
	p.Sub(p, big.NewInt(1))
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if !p.ProbablyPrime(20) {
			b.Fatal("not prime")
		}
	}
}
