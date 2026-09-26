/* net/netip, on the rows Go's own netip benchmarks have.
 *
 * The inputs are Go's parseBenchInputs. The rows that make a string take it
 * from an arena that is reset every time round, as the strings rows do, so
 * they measure the formatting and a bump allocation, which is the nearest C
 * gets to what Go's allocator does for a short string.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

#include "burrow/core.h"
#include "burrow/mem/arena.h"
#include "burrow/net/netip.h"

#include <string.h>

static Str cstr(const char *s) {
    return (Str){(const Byte *)s, (Int)strlen(s)};
}

static const char *const inputs[] = {
    "192.168.1.1",
    "fd7a:115c:a1e0:ab12:4843:cd96:626b:430b",
    "fd7a:115c::626b:430b",
    "::ffff:192.168.140.255",
    "1:2::ffff:192.168.140.255%eth1",
};

static const char *const addr_ports[] = {
    "192.168.1.1:1234",
    "[fd7a:115c:a1e0:ab12:4843:cd96:626b:430b]:1234",
    "[fd7a:115c::626b:430b]:1234",
    "[::ffff:192.168.140.255]:1234",
    "[1:2::ffff:192.168.140.255%eth1]:1234",
};

static void keep_addr(NetipAddr ip) {
    bench_keep_u64(ip.hi ^ ip.lo);
}

static void parse_addr(Bench *b, int i) {
    Str s = cstr(inputs[i]);
    BENCH_LOOP(b) {
        Error err;
        keep_addr(netip_parse_addr(s, &err));
    }
}

static void addr_string(Bench *b, int i) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    NetipAddr ip = netip_must_parse_addr(cstr(inputs[i]));
    BENCH_LOOP(b) {
        Str s = netip_addr_string(ip, a);
        bench_keep(s.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

static void addr_port_string(Bench *b, int i) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    NetipAddrPort ipp =
        netip_addr_port_from(netip_must_parse_addr(cstr(inputs[i])), 60000);
    BENCH_LOOP(b) {
        Str s = netip_addr_port_string(ipp, a);
        bench_keep(s.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

static void parse_addr_port(Bench *b, int i) {
    Str s = cstr(addr_ports[i]);
    BENCH_LOOP(b) {
        Error err;
        NetipAddrPort p = netip_parse_addr_port(s, &err);
        keep_addr(p.ip);
    }
}

BENCH(netip_parse_addr_v4) {
    parse_addr(b, 0);
}
BENCH(netip_parse_addr_v6) {
    parse_addr(b, 1);
}
BENCH(netip_parse_addr_v6_ellipsis) {
    parse_addr(b, 2);
}
BENCH(netip_parse_addr_v6_v4) {
    parse_addr(b, 3);
}
BENCH(netip_parse_addr_v6_zone) {
    parse_addr(b, 4);
}

BENCH(netip_addr_string_v4) {
    addr_string(b, 0);
}
BENCH(netip_addr_string_v6) {
    addr_string(b, 1);
}
BENCH(netip_addr_string_v6_ellipsis) {
    addr_string(b, 2);
}
BENCH(netip_addr_string_v6_v4) {
    addr_string(b, 3);
}
BENCH(netip_addr_string_v6_zone) {
    addr_string(b, 4);
}

BENCH(netip_addr_port_string_v4) {
    addr_port_string(b, 0);
}
BENCH(netip_addr_port_string_v6) {
    addr_port_string(b, 1);
}
BENCH(netip_addr_port_string_v6_ellipsis) {
    addr_port_string(b, 2);
}
BENCH(netip_addr_port_string_v6_v4) {
    addr_port_string(b, 3);
}
BENCH(netip_addr_port_string_v6_zone) {
    addr_port_string(b, 4);
}

BENCH(netip_parse_addr_port_v4) {
    parse_addr_port(b, 0);
}
BENCH(netip_parse_addr_port_v6) {
    parse_addr_port(b, 1);
}
BENCH(netip_parse_addr_port_v6_ellipsis) {
    parse_addr_port(b, 2);
}
BENCH(netip_parse_addr_port_v6_v4) {
    parse_addr_port(b, 3);
}
BENCH(netip_parse_addr_port_v6_zone) {
    parse_addr_port(b, 4);
}

BENCH(netip_prefix_string) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);
    NetipPrefix p = netip_must_parse_prefix(cstr("66.55.44.33/22"));
    BENCH_LOOP(b) {
        Str s = netip_prefix_string(p, a);
        bench_keep(s.p);
        arena_reset(&ar);
    }
    arena_free(&ar);
}

BENCH(netip_ipv4_contains) {
    Byte net4[4] = {192, 168, 1, 0};
    Byte ip4[4] = {192, 168, 1, 1};
    NetipPrefix p = netip_prefix_from(netip_addr_from4(net4), 24);
    NetipAddr ip = netip_addr_from4(ip4);
    BENCH_LOOP(b) {
        bench_keep_u64(netip_prefix_contains(p, ip));
    }
}

BENCH(netip_ipv6_contains) {
    NetipPrefix p = netip_must_parse_prefix(cstr("::1/128"));
    NetipAddr ip = netip_must_parse_addr(cstr("::1"));
    BENCH_LOOP(b) {
        bench_keep_u64(netip_prefix_contains(p, ip));
    }
}

BENCH(netip_as16) {
    NetipAddr ip = netip_must_parse_addr(cstr("1::10"));
    BENCH_LOOP(b) {
        NetipAddrAs16Ret r = netip_addr_as16(ip);
        bench_keep(r.a);
    }
}

void register_netip_benchmarks(void);

void register_netip_benchmarks(void) {
    BENCH_RUN(netip_parse_addr_v4);
    BENCH_RUN(netip_parse_addr_v6);
    BENCH_RUN(netip_parse_addr_v6_ellipsis);
    BENCH_RUN(netip_parse_addr_v6_v4);
    BENCH_RUN(netip_parse_addr_v6_zone);
    BENCH_RUN(netip_addr_string_v4);
    BENCH_RUN(netip_addr_string_v6);
    BENCH_RUN(netip_addr_string_v6_ellipsis);
    BENCH_RUN(netip_addr_string_v6_v4);
    BENCH_RUN(netip_addr_string_v6_zone);
    BENCH_RUN(netip_addr_port_string_v4);
    BENCH_RUN(netip_addr_port_string_v6);
    BENCH_RUN(netip_addr_port_string_v6_ellipsis);
    BENCH_RUN(netip_addr_port_string_v6_v4);
    BENCH_RUN(netip_addr_port_string_v6_zone);
    BENCH_RUN(netip_parse_addr_port_v4);
    BENCH_RUN(netip_parse_addr_port_v6);
    BENCH_RUN(netip_parse_addr_port_v6_ellipsis);
    BENCH_RUN(netip_parse_addr_port_v6_v4);
    BENCH_RUN(netip_parse_addr_port_v6_zone);
    BENCH_RUN(netip_prefix_string);
    BENCH_RUN(netip_ipv4_contains);
    BENCH_RUN(netip_ipv6_contains);
    BENCH_RUN(netip_as16);
}
