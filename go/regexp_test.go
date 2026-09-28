// The Go side of the regexp benchmarks.
//
// These are the benchmarks from Go's regexp/all_test.go and exec_test.go,
// with the bodies copied from Go's and a Regexp prefix on the names.
// BenchmarkMatch and BenchmarkCompile run sub-benchmarks, which pairs.txt
// cannot name, so each one is a function of its own here. The 32M size and
// the two RunParallel benchmarks are left out, as on the C side.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"regexp"
	"strings"
	"testing"
)

func BenchmarkRegexpFind(b *testing.B) {
	re := regexp.MustCompile("a+b+")
	s := []byte("acbbaaabbdd")
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if string(re.Find(s)) != "aaabb" {
			b.Fatal("wrong match")
		}
	}
}

func BenchmarkRegexpFindAllNoMatches(b *testing.B) {
	re := regexp.MustCompile("a+b+")
	s := []byte("acddee")
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if re.FindAll(s, -1) != nil {
			b.Fatal("want nil")
		}
	}
}

func BenchmarkRegexpFindAllTenMatches(b *testing.B) {
	re := regexp.MustCompile("a+b+")
	s := bytes.Repeat([]byte("acddeeabbax"), 10)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if len(re.FindAll(s, -1)) != 10 {
			b.Fatal("want 10 matches")
		}
	}
}

func BenchmarkRegexpFindString(b *testing.B) {
	re := regexp.MustCompile("a+b+")
	s := "acbbaaabbdd"
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if re.FindString(s) != "aaabb" {
			b.Fatal("wrong match")
		}
	}
}

func BenchmarkRegexpFindSubmatch(b *testing.B) {
	re := regexp.MustCompile("a(a+b+)b")
	s := []byte("acbbaaabbdd")
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		subs := re.FindSubmatch(s)
		if string(subs[0]) != "aaabb" || string(subs[1]) != "aab" {
			b.Fatal("wrong match")
		}
	}
}

func BenchmarkRegexpFindStringSubmatch(b *testing.B) {
	re := regexp.MustCompile("a(a+b+)b")
	s := "acbbaaabbdd"
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		subs := re.FindStringSubmatch(s)
		if subs[0] != "aaabb" || subs[1] != "aab" {
			b.Fatal("wrong match")
		}
	}
}

func regexpMatchString(b *testing.B, pat, x string) {
	re := regexp.MustCompile(pat)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if !re.MatchString(x) {
			b.Fatal("no match!")
		}
	}
}

func BenchmarkRegexpLiteral(b *testing.B) {
	regexpMatchString(b, "y", strings.Repeat("x", 50)+"y")
}

func BenchmarkRegexpNotLiteral(b *testing.B) {
	regexpMatchString(b, ".y", strings.Repeat("x", 50)+"y")
}

func BenchmarkRegexpMatchClass(b *testing.B) {
	regexpMatchString(b, "[abcdw]", strings.Repeat("xxxx", 20)+"w")
}

func BenchmarkRegexpMatchClass_InRange(b *testing.B) {
	regexpMatchString(b, "[ac]", strings.Repeat("bbbb", 20)+"c")
}

func BenchmarkRegexpReplaceAll(b *testing.B) {
	x := "abcdefghijklmnopqrstuvwxyz"
	re := regexp.MustCompile("[cjrw]")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		re.ReplaceAllString(x, "")
	}
}

func regexpMatchBytes(b *testing.B, pat, text string, long bool) {
	x := []byte(text)
	if long {
		for i := 0; i < 15; i++ {
			x = append(x, x...)
		}
	}
	re := regexp.MustCompile(pat)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		re.Match(x)
	}
}

const regexpAlphabet = "abcdefghijklmnopqrstuvwxyz"

func BenchmarkRegexpAnchoredLiteralShortNonMatch(b *testing.B) {
	regexpMatchBytes(b, "^zbc(d|e)", regexpAlphabet, false)
}

func BenchmarkRegexpAnchoredLiteralLongNonMatch(b *testing.B) {
	regexpMatchBytes(b, "^zbc(d|e)", regexpAlphabet, true)
}

func BenchmarkRegexpAnchoredShortMatch(b *testing.B) {
	regexpMatchBytes(b, "^.bc(d|e)", regexpAlphabet, false)
}

func BenchmarkRegexpAnchoredLongMatch(b *testing.B) {
	regexpMatchBytes(b, "^.bc(d|e)", regexpAlphabet, true)
}

func BenchmarkRegexpOnePassShortA(b *testing.B) {
	regexpMatchBytes(b, "^.bc(d|e)*$", "abcddddddeeeededd", false)
}

func BenchmarkRegexpNotOnePassShortA(b *testing.B) {
	regexpMatchBytes(b, ".bc(d|e)*$", "abcddddddeeeededd", false)
}

func BenchmarkRegexpOnePassShortB(b *testing.B) {
	regexpMatchBytes(b, "^.bc(?:d|e)*$", "abcddddddeeeededd", false)
}

func BenchmarkRegexpNotOnePassShortB(b *testing.B) {
	regexpMatchBytes(b, ".bc(?:d|e)*$", "abcddddddeeeededd", false)
}

func BenchmarkRegexpOnePassLongPrefix(b *testing.B) {
	regexpMatchBytes(b, "^abcdefghijklmnopqrstuvwxyz.*$", regexpAlphabet, false)
}

func BenchmarkRegexpOnePassLongNotPrefix(b *testing.B) {
	regexpMatchBytes(b, "^.bcdefghijklmnopqrstuvwxyz.*$", regexpAlphabet, false)
}

var regexpSink string

func regexpQuoteMeta(b *testing.B, s string) {
	b.SetBytes(int64(len(s)))
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		regexpSink = regexp.QuoteMeta(s)
	}
}

func BenchmarkRegexpQuoteMetaAll(b *testing.B) { regexpQuoteMeta(b, `$()*+.?[\]^{|}`) }

func BenchmarkRegexpQuoteMetaNone(b *testing.B) { regexpQuoteMeta(b, regexpAlphabet) }

func regexpCompile(b *testing.B, pat string) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if _, err := regexp.Compile(pat); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkRegexpCompileOnepass(b *testing.B) { regexpCompile(b, `^a.[l-nA-Cg-j]?e$`) }

func BenchmarkRegexpCompileMedium(b *testing.B) {
	regexpCompile(b, `^((a|b|[d-z0-9])*(日){4,5}.)+$`)
}

func BenchmarkRegexpCompileHard(b *testing.B) {
	regexpCompile(b, strings.Repeat(`((abc)*|`, 50)+strings.Repeat(`)`, 50))
}

// exec_test.go's makeText.
func regexpText(n int) []byte {
	text := make([]byte, n)
	x := ^uint32(0)
	for i := range text {
		x += x
		x ^= 1
		if int32(x) < 0 {
			x ^= 0x88888eef
		}
		if x%31 == 0 {
			text[i] = '\n'
		} else {
			text[i] = byte(x%(0x7E+1-0x20) + 0x20)
		}
	}
	return text
}

func regexpMatch(b *testing.B, pat string, n int) {
	r := regexp.MustCompile(pat)
	t := regexpText(n)
	b.SetBytes(int64(n))
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if r.Match(t) {
			b.Fatal("match!")
		}
	}
}

func regexpMatchOnepass(b *testing.B, n int) {
	r := regexp.MustCompile(`(?s)\A.*\z`)
	t := regexpText(n)
	b.SetBytes(int64(n))
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if !r.Match(t) {
			b.Fatal("not match!")
		}
	}
}

func BenchmarkRegexpMatchEasy0_16(b *testing.B) { regexpMatch(b, `ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 16) }
func BenchmarkRegexpMatchEasy0_32(b *testing.B) { regexpMatch(b, `ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 32) }
func BenchmarkRegexpMatchEasy0_1K(b *testing.B) { regexpMatch(b, `ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 1024) }
func BenchmarkRegexpMatchEasy0_32K(b *testing.B) {
	regexpMatch(b, `ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 32768)
}
func BenchmarkRegexpMatchEasy0_1M(b *testing.B) {
	regexpMatch(b, `ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 1048576)
}
func BenchmarkRegexpMatchEasy0i_16(b *testing.B) {
	regexpMatch(b, `(?i)ABCDEFGHIJklmnopqrstuvwxyz$`, 16)
}
func BenchmarkRegexpMatchEasy0i_32(b *testing.B) {
	regexpMatch(b, `(?i)ABCDEFGHIJklmnopqrstuvwxyz$`, 32)
}
func BenchmarkRegexpMatchEasy0i_1K(b *testing.B) {
	regexpMatch(b, `(?i)ABCDEFGHIJklmnopqrstuvwxyz$`, 1024)
}
func BenchmarkRegexpMatchEasy0i_32K(b *testing.B) {
	regexpMatch(b, `(?i)ABCDEFGHIJklmnopqrstuvwxyz$`, 32768)
}
func BenchmarkRegexpMatchEasy0i_1M(b *testing.B) {
	regexpMatch(b, `(?i)ABCDEFGHIJklmnopqrstuvwxyz$`, 1048576)
}
func BenchmarkRegexpMatchEasy1_16(b *testing.B) {
	regexpMatch(b, `A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$`, 16)
}
func BenchmarkRegexpMatchEasy1_32(b *testing.B) {
	regexpMatch(b, `A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$`, 32)
}
func BenchmarkRegexpMatchEasy1_1K(b *testing.B) {
	regexpMatch(b, `A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$`, 1024)
}
func BenchmarkRegexpMatchEasy1_32K(b *testing.B) {
	regexpMatch(b, `A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$`, 32768)
}
func BenchmarkRegexpMatchEasy1_1M(b *testing.B) {
	regexpMatch(b, `A[AB]B[BC]C[CD]D[DE]E[EF]F[FG]G[GH]H[HI]I[IJ]J$`, 1048576)
}
func BenchmarkRegexpMatchMedium_16(b *testing.B) {
	regexpMatch(b, `[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 16)
}
func BenchmarkRegexpMatchMedium_32(b *testing.B) {
	regexpMatch(b, `[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 32)
}
func BenchmarkRegexpMatchMedium_1K(b *testing.B) {
	regexpMatch(b, `[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 1024)
}
func BenchmarkRegexpMatchMedium_32K(b *testing.B) {
	regexpMatch(b, `[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 32768)
}
func BenchmarkRegexpMatchMedium_1M(b *testing.B) {
	regexpMatch(b, `[XYZ]ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 1048576)
}
func BenchmarkRegexpMatchHard_16(b *testing.B) {
	regexpMatch(b, `[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 16)
}
func BenchmarkRegexpMatchHard_32(b *testing.B) {
	regexpMatch(b, `[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 32)
}
func BenchmarkRegexpMatchHard_1K(b *testing.B) {
	regexpMatch(b, `[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 1024)
}
func BenchmarkRegexpMatchHard_32K(b *testing.B) {
	regexpMatch(b, `[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 32768)
}
func BenchmarkRegexpMatchHard_1M(b *testing.B) {
	regexpMatch(b, `[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$`, 1048576)
}
func BenchmarkRegexpMatchHard1_16(b *testing.B) {
	regexpMatch(b, `ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ`, 16)
}
func BenchmarkRegexpMatchHard1_32(b *testing.B) {
	regexpMatch(b, `ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ`, 32)
}
func BenchmarkRegexpMatchHard1_1K(b *testing.B) {
	regexpMatch(b, `ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ`, 1024)
}
func BenchmarkRegexpMatchHard1_32K(b *testing.B) {
	regexpMatch(b, `ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ`, 32768)
}
func BenchmarkRegexpMatchHard1_1M(b *testing.B) {
	regexpMatch(b, `ABCD|CDEF|EFGH|GHIJ|IJKL|KLMN|MNOP|OPQR|QRST|STUV|UVWX|WXYZ`, 1048576)
}
func BenchmarkRegexpMatchOnepassRegex_16(b *testing.B)  { regexpMatchOnepass(b, 16) }
func BenchmarkRegexpMatchOnepassRegex_32(b *testing.B)  { regexpMatchOnepass(b, 32) }
func BenchmarkRegexpMatchOnepassRegex_1K(b *testing.B)  { regexpMatchOnepass(b, 1024) }
func BenchmarkRegexpMatchOnepassRegex_32K(b *testing.B) { regexpMatchOnepass(b, 32768) }
func BenchmarkRegexpMatchOnepassRegex_1M(b *testing.B)  { regexpMatchOnepass(b, 1048576) }
