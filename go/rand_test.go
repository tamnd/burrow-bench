// The Go side of the math/rand and math/rand/v2 benchmarks.
//
// These are Go's own benchmarks from both packages, copied out with a prefix
// so the names do not collide: Rand2 for v2 and Rand for v1. PCG_DXSM,
// ChaCha8 and ChaCha8Read are from the v2 package's pcg_test.go and
// chacha8_test.go, and the rest are from each package's rand_test.go.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"math/rand"
	randv2 "math/rand/v2"
	"testing"
)

var (
	randSink        uint64
	randAlwaysFalse = false
)

func randKeep[T int | int32 | int64](x T) T {
	if randAlwaysFalse {
		return -x
	}
	return x
}

func rand2Test() *randv2.Rand {
	return randv2.New(randv2.NewPCG(1, 2))
}

// ---------------------------------------------------------------------- v2

func BenchmarkRand2PCG_DXSM(b *testing.B) {
	var p randv2.PCG
	var t uint64
	for n := b.N; n > 0; n-- {
		t += p.Uint64()
	}
	randSink = t
}

func BenchmarkRand2ChaCha8(b *testing.B) {
	p := randv2.NewChaCha8([32]byte{1, 2, 3, 4, 5})
	var t uint64
	for n := b.N; n > 0; n-- {
		t += p.Uint64()
	}
	randSink = t
}

func BenchmarkRand2ChaCha8Read(b *testing.B) {
	p := randv2.NewChaCha8([32]byte{1, 2, 3, 4, 5})
	buf := make([]byte, 32)
	b.SetBytes(32)
	var t uint8
	for n := b.N; n > 0; n-- {
		p.Read(buf)
		t += buf[0]
	}
	randSink = uint64(t)
}

func BenchmarkRand2SourceUint64(b *testing.B) {
	s := randv2.NewPCG(1, 2)
	var t uint64
	for n := b.N; n > 0; n-- {
		t += s.Uint64()
	}
	randSink = t
}

func BenchmarkRand2GlobalInt64(b *testing.B) {
	var t int64
	for n := b.N; n > 0; n-- {
		t += randv2.Int64()
	}
	randSink = uint64(t)
}

func BenchmarkRand2GlobalUint64(b *testing.B) {
	var t uint64
	for n := b.N; n > 0; n-- {
		t += randv2.Uint64()
	}
	randSink = t
}

func BenchmarkRand2Int64(b *testing.B) {
	r := rand2Test()
	var t int64
	for n := b.N; n > 0; n-- {
		t += r.Int64()
	}
	randSink = uint64(t)
}

func BenchmarkRand2Uint64(b *testing.B) {
	r := rand2Test()
	var t uint64
	for n := b.N; n > 0; n-- {
		t += r.Uint64()
	}
	randSink = t
}

func BenchmarkRand2GlobalIntN1000(b *testing.B) {
	var t int
	arg := randKeep(1000)
	for n := b.N; n > 0; n-- {
		t += randv2.IntN(arg)
	}
	randSink = uint64(t)
}

func BenchmarkRand2IntN1000(b *testing.B) {
	r := rand2Test()
	var t int
	arg := randKeep(1000)
	for n := b.N; n > 0; n-- {
		t += r.IntN(arg)
	}
	randSink = uint64(t)
}

func rand2Int64N(b *testing.B, v int64) {
	r := rand2Test()
	var t int64
	arg := randKeep(v)
	for n := b.N; n > 0; n-- {
		t += r.Int64N(arg)
	}
	randSink = uint64(t)
}

func BenchmarkRand2Int64N1000(b *testing.B) { rand2Int64N(b, 1000) }
func BenchmarkRand2Int64N1e9(b *testing.B)  { rand2Int64N(b, 1e9) }
func BenchmarkRand2Int64N1e18(b *testing.B) { rand2Int64N(b, 1e18) }
func BenchmarkRand2Int64N4e18(b *testing.B) { rand2Int64N(b, 4e18) }

func rand2Int32N(b *testing.B, v int32) {
	r := rand2Test()
	var t int32
	arg := randKeep(v)
	for n := b.N; n > 0; n-- {
		t += r.Int32N(arg)
	}
	randSink = uint64(t)
}

func BenchmarkRand2Int32N1000(b *testing.B) { rand2Int32N(b, 1000) }
func BenchmarkRand2Int32N2e9(b *testing.B)  { rand2Int32N(b, 2e9) }

func BenchmarkRand2Float32(b *testing.B) {
	r := rand2Test()
	var t float32
	for n := b.N; n > 0; n-- {
		t += r.Float32()
	}
	randSink = uint64(t)
}

func BenchmarkRand2Float64(b *testing.B) {
	r := rand2Test()
	var t float64
	for n := b.N; n > 0; n-- {
		t += r.Float64()
	}
	randSink = uint64(t)
}

func BenchmarkRand2ExpFloat64(b *testing.B) {
	r := rand2Test()
	var t float64
	for n := b.N; n > 0; n-- {
		t += r.ExpFloat64()
	}
	randSink = uint64(t)
}

func BenchmarkRand2NormFloat64(b *testing.B) {
	r := rand2Test()
	var t float64
	for n := b.N; n > 0; n-- {
		t += r.NormFloat64()
	}
	randSink = uint64(t)
}

func BenchmarkRand2Perm3(b *testing.B) {
	r := rand2Test()
	var t int
	for n := b.N; n > 0; n-- {
		t += r.Perm(3)[0]
	}
	randSink = uint64(t)
}

// ---------------------------------------------------------------------- v1

func BenchmarkRandInt63Threadsafe(b *testing.B) {
	for n := b.N; n > 0; n-- {
		rand.Int63()
	}
}

func BenchmarkRandInt63Unthreadsafe(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Int63()
	}
}

func BenchmarkRandIntn1000(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Intn(1000)
	}
}

func BenchmarkRandInt63n1000(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Int63n(1000)
	}
}

func BenchmarkRandInt31n1000(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Int31n(1000)
	}
}

func BenchmarkRandFloat64(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Float64()
	}
}

func BenchmarkRandPerm30(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Perm(30)
	}
}

func BenchmarkRandShuffleOverhead(b *testing.B) {
	r := rand.New(rand.NewSource(1))
	for n := b.N; n > 0; n-- {
		r.Shuffle(52, func(i, j int) {
			if i < 0 || i >= 52 || j < 0 || j >= 52 {
				b.Fatalf("bad swap(%d, %d)", i, j)
			}
		})
	}
}

func randRead(b *testing.B, size int) {
	r := rand.New(rand.NewSource(1))
	buf := make([]byte, size)
	b.ResetTimer()
	for n := b.N; n > 0; n-- {
		r.Read(buf)
	}
}

func BenchmarkRandRead3(b *testing.B)    { randRead(b, 3) }
func BenchmarkRandRead64(b *testing.B)   { randRead(b, 64) }
func BenchmarkRandRead1000(b *testing.B) { randRead(b, 1000) }
