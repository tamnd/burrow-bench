// The Go side of the net/netip benchmarks.
//
// These are Go's own netip benchmarks with the same inputs, so that each row
// here does what the row of the same name in bench/netip_bench.c does. Go's
// live inside the package, so they are copied here through the public API.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"net/netip"
	"strings"
	"testing"
)

var (
	sinkNetipAddr     netip.Addr
	sinkNetipAddrPort netip.AddrPort
	sinkNetipString   string
	sinkNetipBool     bool
	sinkNetip16       [16]byte
)

// Go's parseBenchInputs, in the same order: v4, v6, v6_ellipsis, v6_v4 and
// v6_zone. Go runs them as sub-benchmarks, which pairs.txt cannot name, so
// each one gets a function of its own here.
var netipBenchInputs = []string{
	"192.168.1.1",
	"fd7a:115c:a1e0:ab12:4843:cd96:626b:430b",
	"fd7a:115c::626b:430b",
	"::ffff:192.168.140.255",
	"1:2::ffff:192.168.140.255%eth1",
}

func netipParseAddr(b *testing.B, i int) {
	b.ReportAllocs()
	s := netipBenchInputs[i]
	for n := 0; n < b.N; n++ {
		sinkNetipAddr, _ = netip.ParseAddr(s)
	}
}

func netipAddrString(b *testing.B, i int) {
	b.ReportAllocs()
	ip := netip.MustParseAddr(netipBenchInputs[i])
	for n := 0; n < b.N; n++ {
		sinkNetipString = ip.String()
	}
}

func netipAddrPortString(b *testing.B, i int) {
	b.ReportAllocs()
	ipp := netip.AddrPortFrom(netip.MustParseAddr(netipBenchInputs[i]), 60000)
	for n := 0; n < b.N; n++ {
		sinkNetipString = ipp.String()
	}
}

// Go's BenchmarkParseAddrPort puts brackets round the IPv6 inputs and adds
// port 1234.
func netipParseAddrPort(b *testing.B, i int) {
	b.ReportAllocs()
	ip := netipBenchInputs[i]
	s := ip + ":1234"
	if strings.Contains(ip, ":") {
		s = "[" + ip + "]:1234"
	}
	for n := 0; n < b.N; n++ {
		sinkNetipAddrPort, _ = netip.ParseAddrPort(s)
	}
}

func BenchmarkNetipParseAddrV4(b *testing.B)         { netipParseAddr(b, 0) }
func BenchmarkNetipParseAddrV6(b *testing.B)         { netipParseAddr(b, 1) }
func BenchmarkNetipParseAddrV6Ellipsis(b *testing.B) { netipParseAddr(b, 2) }
func BenchmarkNetipParseAddrV6V4(b *testing.B)       { netipParseAddr(b, 3) }
func BenchmarkNetipParseAddrV6Zone(b *testing.B)     { netipParseAddr(b, 4) }

func BenchmarkNetipAddrStringV4(b *testing.B)         { netipAddrString(b, 0) }
func BenchmarkNetipAddrStringV6(b *testing.B)         { netipAddrString(b, 1) }
func BenchmarkNetipAddrStringV6Ellipsis(b *testing.B) { netipAddrString(b, 2) }
func BenchmarkNetipAddrStringV6V4(b *testing.B)       { netipAddrString(b, 3) }
func BenchmarkNetipAddrStringV6Zone(b *testing.B)     { netipAddrString(b, 4) }

func BenchmarkNetipAddrPortStringV4(b *testing.B)         { netipAddrPortString(b, 0) }
func BenchmarkNetipAddrPortStringV6(b *testing.B)         { netipAddrPortString(b, 1) }
func BenchmarkNetipAddrPortStringV6Ellipsis(b *testing.B) { netipAddrPortString(b, 2) }
func BenchmarkNetipAddrPortStringV6V4(b *testing.B)       { netipAddrPortString(b, 3) }
func BenchmarkNetipAddrPortStringV6Zone(b *testing.B)     { netipAddrPortString(b, 4) }

func BenchmarkNetipParseAddrPortV4(b *testing.B)         { netipParseAddrPort(b, 0) }
func BenchmarkNetipParseAddrPortV6(b *testing.B)         { netipParseAddrPort(b, 1) }
func BenchmarkNetipParseAddrPortV6Ellipsis(b *testing.B) { netipParseAddrPort(b, 2) }
func BenchmarkNetipParseAddrPortV6V4(b *testing.B)       { netipParseAddrPort(b, 3) }
func BenchmarkNetipParseAddrPortV6Zone(b *testing.B)     { netipParseAddrPort(b, 4) }

func BenchmarkNetipPrefixString(b *testing.B) {
	b.ReportAllocs()
	p := netip.MustParsePrefix("66.55.44.33/22")
	for i := 0; i < b.N; i++ {
		sinkNetipString = p.String()
	}
}

func BenchmarkNetipIPv4Contains(b *testing.B) {
	b.ReportAllocs()
	p := netip.PrefixFrom(netip.AddrFrom4([4]byte{192, 168, 1, 0}), 24)
	ip := netip.AddrFrom4([4]byte{192, 168, 1, 1})
	for i := 0; i < b.N; i++ {
		sinkNetipBool = p.Contains(ip)
	}
}

func BenchmarkNetipIPv6Contains(b *testing.B) {
	b.ReportAllocs()
	p := netip.MustParsePrefix("::1/128")
	ip := netip.MustParseAddr("::1")
	for i := 0; i < b.N; i++ {
		sinkNetipBool = p.Contains(ip)
	}
}

func BenchmarkNetipAs16(b *testing.B) {
	ip := netip.MustParseAddr("1::10")
	for i := 0; i < b.N; i++ {
		sinkNetip16 = ip.As16()
	}
}
