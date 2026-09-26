// The Go side of the net/textproto benchmarks.
//
// The first three are Go's own BenchmarkReadMIMEHeader and BenchmarkUncommon
// from net/textproto, with the two ReadMIMEHeader cases as functions of their
// own so pairs.txt can name them. The canonical key row does what the row of
// the same name in bench/textproto_bench.c does.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bufio"
	"bytes"
	"net/textproto"
	"strings"
	"testing"
)

var (
	sinkMIMEHeader textproto.MIMEHeader
	sinkHeaderKey  string
)

var textprotoClientHeaders = strings.Replace(`Host: golang.org
Connection: keep-alive
Cache-Control: max-age=0
Accept: application/xml,application/xhtml+xml,text/html;q=0.9,text/plain;q=0.8,image/png,*/*;q=0.5
User-Agent: Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.63 Safari/534.3
Accept-Encoding: gzip,deflate,sdch
Accept-Language: en-US,en;q=0.8,fr-CH;q=0.6
Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.3
COOKIE: __utma=000000000.0000000000.0000000000.0000000000.0000000000.00; __utmb=000000000.0.00.0000000000; __utmc=000000000; __utmz=000000000.0000000000.00.0.utmcsr=code.google.com|utmccn=(referral)|utmcmd=referral|utmcct=/p/go/issues/detail
Non-Interned: test

`, "\n", "\r\n", -1)

var textprotoServerHeaders = strings.Replace(`Content-Type: text/html; charset=utf-8
Content-Encoding: gzip
Date: Thu, 27 Sep 2012 09:03:33 GMT
Server: Google Frontend
Cache-Control: private
Content-Length: 2298
VIA: 1.1 proxy.example.com:80 (XXX/n.n.n-nnn)
Connection: Close
Non-Interned: test

`, "\n", "\r\n", -1)

func textprotoReadHeaders(b *testing.B, headers string) {
	b.ReportAllocs()
	var buf bytes.Buffer
	br := bufio.NewReader(&buf)
	r := textproto.NewReader(br)
	for i := 0; i < b.N; i++ {
		buf.WriteString(headers)
		h, err := r.ReadMIMEHeader()
		if err != nil {
			b.Fatal(err)
		}
		sinkMIMEHeader = h
	}
}

func BenchmarkTextprotoReadMIMEHeaderClient(b *testing.B) {
	textprotoReadHeaders(b, textprotoClientHeaders)
}

func BenchmarkTextprotoReadMIMEHeaderServer(b *testing.B) {
	textprotoReadHeaders(b, textprotoServerHeaders)
}

func BenchmarkTextprotoUncommon(b *testing.B) {
	textprotoReadHeaders(b, "uncommon-header-for-benchmark: foo\r\n\r\n")
}

func BenchmarkTextprotoCanonicalKey(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		sinkHeaderKey = textproto.CanonicalMIMEHeaderKey("x-forwarded-for")
	}
}
