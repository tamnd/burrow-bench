// The Go side of the strings benchmarks.
//
// The inputs are the ones Go's own strings benchmarks use, except the hard
// input, which Go builds with math/rand. This file builds it with the same
// xorshift as bench/strings_bench.c, so both searches read the same megabyte.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"strings"
	"testing"
)

var (
	sinkStrings    []string
	sinkStringsInt int
)

var benchHard = func() string {
	tokens := [...]string{
		"<a>", "<p>", "<b>", "<strong>",
		"</a>", "</p>", "</b>", "</strong>",
		"hello", "world",
	}
	x := make([]byte, 0, 1<<20)
	r := uint64(99)
	for {
		r ^= r << 13
		r ^= r >> 7
		r ^= r << 17
		t := tokens[r%10]
		if len(x)+len(t) >= 1<<20 {
			break
		}
		x = append(x, t...)
	}
	return string(x)
}()

// ------------------------------------------------------------------ searching

func benchmarkStringsIndexHard(b *testing.B, sep string) {
	for i := 0; i < b.N; i++ {
		sinkStringsInt = strings.Index(benchHard, sep)
	}
}

func BenchmarkStringsIndexHard1(b *testing.B) { benchmarkStringsIndexHard(b, "<>") }
func BenchmarkStringsIndexHard2(b *testing.B) { benchmarkStringsIndexHard(b, "</pre>") }
func BenchmarkStringsIndexHard3(b *testing.B) {
	benchmarkStringsIndexHard(b, "<b>hello world</b>")
}

func BenchmarkStringsLastIndexHard2(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStringsInt = strings.LastIndex(benchHard, "</pre>")
	}
}

func BenchmarkStringsCountHard2(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStringsInt = strings.Count(benchHard, "</pre>")
	}
}

func BenchmarkStringsIndex(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStringsInt = strings.Index("some_text=some☺value", "v")
	}
}

func BenchmarkStringsIndexRune(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStringsInt = strings.IndexRune("some_text=some☺value", '☺')
	}
}

func BenchmarkStringsIndexAnyASCII(b *testing.B) {
	s := strings.Repeat("x", 63) + "."
	for i := 0; i < b.N; i++ {
		sinkStringsInt = strings.IndexAny(s, ",.;:")
	}
}

func BenchmarkStringsEqualFold(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkBool = strings.EqualFold("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890",
			"abcdefghijklmnopqrstuvwxyz1234567890")
	}
}

func BenchmarkStringsTrimSpace(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStr = strings.TrimSpace("  \t\n  some text with spaces around it  \t\n  ")
	}
}

// ------------------------------------------------------------------- building

const fieldsInput = "the quick brown fox jumps over the lazy dog, and then again, the quick brown " +
	"fox jumps over the lazy dog once more before going home for the night"

func BenchmarkStringsFields(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStrings = strings.Fields(fieldsInput)
	}
}

func BenchmarkStringsSplitSingleByte(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStrings = strings.Split(fieldsInput, " ")
	}
}

func BenchmarkStringsSplitSeqSingleByte(b *testing.B) {
	for i := 0; i < b.N; i++ {
		n := 0
		for range strings.SplitSeq(fieldsInput, " ") {
			n++
		}
		sinkStringsInt = n
	}
}

func BenchmarkStringsJoin(b *testing.B) {
	parts := make([]string, 16)
	for i := range parts {
		parts[i] = "element"
	}
	for i := 0; i < b.N; i++ {
		sinkStr = strings.Join(parts, ", ")
	}
}

func BenchmarkStringsRepeat(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStr = strings.Repeat("-", 80)
	}
}

func BenchmarkStringsToUpper(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStr = strings.ToUpper("the quick brown fox jumps over the lazy dog")
	}
}

func BenchmarkStringsToUpperUnchanged(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStr = strings.ToUpper("THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG")
	}
}

func BenchmarkStringsReplaceAll(b *testing.B) {
	s := "banana banana banana banana banana banana banana banana"
	for i := 0; i < b.N; i++ {
		sinkStr = strings.ReplaceAll(s, "a", "<>")
	}
}

func BenchmarkStringsBuilder(b *testing.B) {
	for i := 0; i < b.N; i++ {
		var sb strings.Builder
		for j := 0; j < 16; j++ {
			sb.WriteString("some string ")
		}
		sinkStr = sb.String()
	}
}

// ------------------------------------------------------------------ replacers

var (
	stringsHTMLEscaper = strings.NewReplacer(
		"&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;", "'", "&apos;")
	stringsHTMLUnescaper = strings.NewReplacer(
		"&amp;", "&", "&lt;", "<", "&gt;", ">", "&quot;", `"`, "&apos;", "'")
)

func BenchmarkStringsHTMLEscape(b *testing.B) {
	for i := 0; i < b.N; i++ {
		sinkStr = stringsHTMLEscaper.Replace("I <3 to escape HTML & other text too.")
	}
}

func BenchmarkStringsGenericMatch2(b *testing.B) {
	s := strings.Repeat("It&apos;s &lt;b&gt;HTML&lt;/b&gt;!", 100)
	for i := 0; i < b.N; i++ {
		sinkStr = stringsHTMLUnescaper.Replace(s)
	}
}

func BenchmarkStringsSingleMatch(b *testing.B) {
	r := strings.NewReplacer("abcdef", "[match]")
	s := strings.Repeat("abcdefghijklmno", 1000)
	for i := 0; i < b.N; i++ {
		sinkStr = r.Replace(s)
	}
}
