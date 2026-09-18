// The Go side of the arithmetic benchmarks.
//
// There is nothing to port on this side, which is the point. Every rule that
// burrow/num.h spells out in C is the language's job here: a signed add wraps,
// a shift past the width of the type is zero, a divide by zero is a run time
// error with a message. So these rows are the baseline the C ones are trying to
// match, and a C row that is level with its Go row is a rule that cost nothing
// to port.
//
// The two rows that should not come out level are the shift and the divide.
// Go's compiler emits its own check in both places, so the gap between the two
// languages there is small, but it is also the honest place to look for one.
//
// Every operand comes out of a package level variable for the same reason the C
// side puts its operands through bench_hide: an expression the compiler can
// trace back to two constants is one it computes at compile time, and then the
// row measures an empty loop.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import "testing"

// sinkInt is declared by the slice benchmarks, which is why it is missing here.

var (
	numA        = 1234567
	numPrime    = 2654435761
	numDivisor  = 7
	numShiftee  = 1
	numNegative = -123456789
	numFloat    = 1234.5

	// A variable rather than a constant, because the FNV offset basis does not
	// fit in an int and a constant conversion that overflows is a compile
	// error. The C side casts the same unsigned literal.
	numFNVOffset = uint64(14695981039346656037)
)

// ---------------------------------------------------------- the arithmetic

func BenchmarkNumAdd(b *testing.B) {
	a := numA
	for i := 0; i < b.N; i++ {
		sinkInt = a + i
	}
}

func BenchmarkNumMul(b *testing.B) {
	a := numPrime
	for i := 0; i < b.N; i++ {
		sinkInt = a * i
	}
}

// ------------------------------------------------------------- the divides
//
// The divisor comes out of a variable, because a division by a constant is not
// a division: the compiler turns it into a multiply and a shift on both sides.

func BenchmarkNumDiv(b *testing.B) {
	d := numDivisor
	for i := 0; i < b.N; i++ {
		sinkInt = i / d
	}
}

func BenchmarkNumMod(b *testing.B) {
	d := numDivisor
	for i := 0; i < b.N; i++ {
		sinkInt = i % d
	}
}

// -------------------------------------------------------------- the shifts
//
// The count goes past the width of the type, which is defined in Go and is the
// case burrow/num.h exists for. Go's answer is zero for the left shift and a
// sign extension for the right one.

func BenchmarkNumShl(b *testing.B) {
	x := numShiftee
	for i := 0; i < b.N; i++ {
		sinkInt = x << uint(i&127)
	}
}

func BenchmarkNumShr(b *testing.B) {
	x := numNegative
	for i := 0; i < b.N; i++ {
		sinkInt = x >> uint(i&127)
	}
}

// ---------------------------------------------------------- float to int
//
// The input stays in range, so this measures the convert instruction rather
// than the disagreement between architectures that the C side documents.

func BenchmarkNumFromFloat64(b *testing.B) {
	f := numFloat
	for i := 0; i < b.N; i++ {
		sinkInt = int(f + float64(i))
	}
}

// ----------------------------------------------------- the realistic shape

const numHashBytes = 64

// A hash round, which is where wrapping arithmetic actually gets used. Every
// multiply in here overflows on almost every input, so this is the row that
// says what Go's rule costs in the place it is unavoidable.
func BenchmarkNumHashRound(b *testing.B) {
	var buf [numHashBytes]byte
	for i := range buf {
		buf[i] = byte(i*7 + 1)
	}
	prime := 1099511628211
	for i := 0; i < b.N; i++ {
		h := int(numFNVOffset)
		for _, c := range buf {
			h *= prime
			h ^= int(c)
		}
		sinkInt = h
	}
}

const numSumLen = 256

// Summing a slice, which is an accumulator a long enough input will overflow,
// in a loop the compiler would like to unroll.
func BenchmarkNumSum(b *testing.B) {
	xs := make([]int, numSumLen)
	for i := range xs {
		xs[i] = i * 3
	}
	for i := 0; i < b.N; i++ {
		total := 0
		for _, x := range xs {
			total += x
		}
		sinkInt = total
	}
}
