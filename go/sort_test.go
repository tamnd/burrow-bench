// The Go side of the sort benchmarks.
//
// These are Go's own sort benchmarks with the same inputs, so that each row
// here does what the row of the same name in bench/sort_bench.c does.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"sort"
	"strconv"
	"testing"
)

func sortStrings1K() []string {
	s := make([]string, 1<<10)
	for i := range s {
		s[i] = strconv.Itoa(i ^ 0x2cc)
	}
	return s
}

func BenchmarkSortString1K(b *testing.B) {
	b.StopTimer()
	unsorted := sortStrings1K()
	data := make([]string, len(unsorted))
	for i := 0; i < b.N; i++ {
		copy(data, unsorted)
		b.StartTimer()
		sort.Strings(data)
		b.StopTimer()
	}
}

func BenchmarkSortString1K_Slice(b *testing.B) {
	b.StopTimer()
	unsorted := sortStrings1K()
	data := make([]string, len(unsorted))
	for i := 0; i < b.N; i++ {
		copy(data, unsorted)
		b.StartTimer()
		sort.Slice(data, func(i, j int) bool { return data[i] < data[j] })
		b.StopTimer()
	}
}

func BenchmarkStableString1K(b *testing.B) {
	b.StopTimer()
	unsorted := sortStrings1K()
	data := make([]string, len(unsorted))
	for i := 0; i < b.N; i++ {
		copy(data, unsorted)
		b.StartTimer()
		sort.Stable(sort.StringSlice(data))
		b.StopTimer()
	}
}

func benchSortInts(b *testing.B, n int, fill func(i, n int) int) {
	b.StopTimer()
	data := make([]int, n)
	for i := 0; i < b.N; i++ {
		for j := range data {
			data[j] = fill(j, n)
		}
		b.StartTimer()
		sort.Ints(data)
		b.StopTimer()
	}
}

func BenchmarkSortInt1K(b *testing.B) {
	benchSortInts(b, 1<<10, func(i, n int) int { return i ^ 0x2cc })
}

func BenchmarkSortInt1K_Sorted(b *testing.B) {
	benchSortInts(b, 1<<10, func(i, n int) int { return i })
}

func BenchmarkSortInt1K_Reversed(b *testing.B) {
	benchSortInts(b, 1<<10, func(i, n int) int { return n - i })
}

func BenchmarkSortInt1K_Mod8(b *testing.B) {
	benchSortInts(b, 1<<10, func(i, n int) int { return i % 8 })
}

func BenchmarkSortInt64K(b *testing.B) {
	benchSortInts(b, 1<<16, func(i, n int) int { return i ^ 0xcccc })
}

func BenchmarkStableInt1K(b *testing.B) {
	b.StopTimer()
	unsorted := make([]int, 1<<10)
	for i := range unsorted {
		unsorted[i] = i ^ 0x2cc
	}
	data := make([]int, len(unsorted))
	for i := 0; i < b.N; i++ {
		copy(data, unsorted)
		b.StartTimer()
		sort.Stable(sort.IntSlice(data))
		b.StopTimer()
	}
}

func BenchmarkStableInt1K_Slice(b *testing.B) {
	b.StopTimer()
	unsorted := make([]int, 1<<10)
	for i := range unsorted {
		unsorted[i] = i ^ 0x2cc
	}
	data := make([]int, len(unsorted))
	for i := 0; i < b.N; i++ {
		copy(data, unsorted)
		b.StartTimer()
		sort.SliceStable(data, func(i, j int) bool { return data[i] < data[j] })
		b.StopTimer()
	}
}

func BenchmarkSortInt64K_Slice(b *testing.B) {
	b.StopTimer()
	data := make([]int, 1<<16)
	for i := 0; i < b.N; i++ {
		for j := range data {
			data[j] = j ^ 0xcccc
		}
		b.StartTimer()
		sort.Slice(data, func(i, j int) bool { return data[i] < data[j] })
		b.StopTimer()
	}
}
