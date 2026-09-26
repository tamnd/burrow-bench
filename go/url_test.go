// The Go side of the net/url benchmarks.
//
// The escape rows are Go's own BenchmarkQueryEscape, BenchmarkPathEscape,
// BenchmarkQueryUnescape and BenchmarkPathUnescape on Go's escapeBenchmarks.
// Go runs the inputs as unnamed sub-benchmarks, which pairs.txt cannot name,
// so each one gets a function of its own here. The other rows do what the row
// of the same name in bench/url_bench.c does.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"net/url"
	"strings"
	"testing"
)

var (
	sinkURLString string
	sinkURL       *url.URL
	sinkURLValues url.Values
)

var urlEscapeInputs = []struct {
	unescaped string
	query     string
	path      string
}{
	{"one two", "one+two", "one%20two"},
	{
		"Фотки собак",
		"%D0%A4%D0%BE%D1%82%D0%BA%D0%B8+%D1%81%D0%BE%D0%B1%D0%B0%D0%BA",
		"%D0%A4%D0%BE%D1%82%D0%BA%D0%B8%20%D1%81%D0%BE%D0%B1%D0%B0%D0%BA",
	},
	{"shortrun(break)shortrun", "shortrun%28break%29shortrun", "shortrun%28break%29shortrun"},
	{
		"longerrunofcharacters(break)anotherlongerrunofcharacters",
		"longerrunofcharacters%28break%29anotherlongerrunofcharacters",
		"longerrunofcharacters%28break%29anotherlongerrunofcharacters",
	},
	{
		strings.Repeat("padded/with+various%characters?that=need$some@escaping+paddedsowebreak/256bytes", 4),
		strings.Repeat("padded%2Fwith%2Bvarious%25characters%3Fthat%3Dneed%24some%40escaping%2Bpaddedsowebreak%2F256bytes", 4),
		strings.Repeat("padded%2Fwith+various%25characters%3Fthat=need$some@escaping+paddedsowebreak%2F256bytes", 4),
	},
}

var urlBenchURLs = []string{
	"https://user:pass@go.dev:8443/doc/a%20b/c?q=url&m=text#top",
	"http://www.google.com/search?q=go+language",
}

func urlQueryEscape(b *testing.B, i int) {
	b.ReportAllocs()
	s := urlEscapeInputs[i].unescaped
	for n := 0; n < b.N; n++ {
		sinkURLString = url.QueryEscape(s)
	}
}

func urlPathEscape(b *testing.B, i int) {
	b.ReportAllocs()
	s := urlEscapeInputs[i].unescaped
	for n := 0; n < b.N; n++ {
		sinkURLString = url.PathEscape(s)
	}
}

func urlQueryUnescape(b *testing.B, i int) {
	b.ReportAllocs()
	s := urlEscapeInputs[i].query
	for n := 0; n < b.N; n++ {
		sinkURLString, _ = url.QueryUnescape(s)
	}
}

func urlPathUnescape(b *testing.B, i int) {
	b.ReportAllocs()
	s := urlEscapeInputs[i].path
	for n := 0; n < b.N; n++ {
		sinkURLString, _ = url.PathUnescape(s)
	}
}

func BenchmarkURLQueryEscape0(b *testing.B)   { urlQueryEscape(b, 0) }
func BenchmarkURLQueryEscape1(b *testing.B)   { urlQueryEscape(b, 1) }
func BenchmarkURLQueryEscape2(b *testing.B)   { urlQueryEscape(b, 2) }
func BenchmarkURLQueryEscape3(b *testing.B)   { urlQueryEscape(b, 3) }
func BenchmarkURLQueryEscape4(b *testing.B)   { urlQueryEscape(b, 4) }
func BenchmarkURLPathEscape0(b *testing.B)    { urlPathEscape(b, 0) }
func BenchmarkURLPathEscape1(b *testing.B)    { urlPathEscape(b, 1) }
func BenchmarkURLPathEscape2(b *testing.B)    { urlPathEscape(b, 2) }
func BenchmarkURLPathEscape3(b *testing.B)    { urlPathEscape(b, 3) }
func BenchmarkURLPathEscape4(b *testing.B)    { urlPathEscape(b, 4) }
func BenchmarkURLQueryUnescape0(b *testing.B) { urlQueryUnescape(b, 0) }
func BenchmarkURLQueryUnescape1(b *testing.B) { urlQueryUnescape(b, 1) }
func BenchmarkURLQueryUnescape2(b *testing.B) { urlQueryUnescape(b, 2) }
func BenchmarkURLQueryUnescape3(b *testing.B) { urlQueryUnescape(b, 3) }
func BenchmarkURLQueryUnescape4(b *testing.B) { urlQueryUnescape(b, 4) }
func BenchmarkURLPathUnescape0(b *testing.B)  { urlPathUnescape(b, 0) }
func BenchmarkURLPathUnescape1(b *testing.B)  { urlPathUnescape(b, 1) }
func BenchmarkURLPathUnescape2(b *testing.B)  { urlPathUnescape(b, 2) }
func BenchmarkURLPathUnescape3(b *testing.B)  { urlPathUnescape(b, 3) }
func BenchmarkURLPathUnescape4(b *testing.B)  { urlPathUnescape(b, 4) }

func urlParse(b *testing.B, s string) {
	b.ReportAllocs()
	for n := 0; n < b.N; n++ {
		sinkURL, _ = url.Parse(s)
	}
}

func urlString(b *testing.B, s string) {
	b.ReportAllocs()
	u, _ := url.Parse(s)
	for n := 0; n < b.N; n++ {
		sinkURLString = u.String()
	}
}

func BenchmarkURLParseFull(b *testing.B)   { urlParse(b, urlBenchURLs[0]) }
func BenchmarkURLParsePlain(b *testing.B)  { urlParse(b, urlBenchURLs[1]) }
func BenchmarkURLStringFull(b *testing.B)  { urlString(b, urlBenchURLs[0]) }
func BenchmarkURLStringPlain(b *testing.B) { urlString(b, urlBenchURLs[1]) }

func BenchmarkURLResolveReference(b *testing.B) {
	b.ReportAllocs()
	base, _ := url.Parse("http://a/b/c/d;p?q")
	ref, _ := url.Parse("../../g")
	for n := 0; n < b.N; n++ {
		sinkURL = base.ResolveReference(ref)
	}
}

func BenchmarkURLEncodeQuery(b *testing.B) {
	b.ReportAllocs()
	v := url.Values{}
	v.Add("q", "puppies")
	v.Add("oe", "utf8")
	for n := 0; n < b.N; n++ {
		sinkURLString = v.Encode()
	}
}

func BenchmarkURLParseQuery(b *testing.B) {
	b.ReportAllocs()
	for n := 0; n < b.N; n++ {
		sinkURLValues, _ = url.ParseQuery("oe=utf8&q=puppies&tag=a&tag=b&page=2")
	}
}
