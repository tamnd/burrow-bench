/* The list of benchmarks to run.
 *
 * C has no way to find functions by pattern the way `go test` finds Benchmark*,
 * and the tricks that come close all rely on linker sections that differ on
 * every platform this has to build on. So the list is written by hand. It is
 * one line per benchmark and the compiler catches a typo, which is a fair price
 * for not having a build step that reads the source.
 *
 * Copyright 2026 The burrow Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style licence that can be found
 * in the LICENSE file. */

#include "bench.h"

void register_error_benchmarks(void);
void register_hash_benchmarks(void);
void register_iface_benchmarks(void);
void register_map_benchmarks(void);
void register_mem_benchmarks(void);
void register_slice_benchmarks(void);
void register_str_benchmarks(void);

int main(int argc, char **argv) {
    register_error_benchmarks();
    register_hash_benchmarks();
    register_iface_benchmarks();
    register_map_benchmarks();
    register_mem_benchmarks();
    register_slice_benchmarks();
    register_str_benchmarks();
    return bench_main(argc, argv);
}
