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

void register_chan_benchmarks(void);
void register_clock_benchmarks(void);
void register_cond_benchmarks(void);
void register_context_benchmarks(void);
void register_defer_benchmarks(void);
void register_error_benchmarks(void);
void register_func_benchmarks(void);
void register_hash_benchmarks(void);
void register_iface_benchmarks(void);
void register_map_benchmarks(void);
void register_mem_benchmarks(void);
void register_mutex_benchmarks(void);
void register_num_benchmarks(void);
void register_panic_benchmarks(void);
void register_preempt_benchmarks(void);
void register_reclaim_benchmarks(void);
void register_runtime_benchmarks(void);
void register_sched_benchmarks(void);
void register_select_benchmarks(void);
void register_slice_benchmarks(void);
void register_str_benchmarks(void);
void register_sync_atomic_benchmarks(void);
void register_sync_benchmarks(void);
void register_sync_map_benchmarks(void);
void register_sync_pool_benchmarks(void);
void register_synctest_benchmarks(void);
void register_timer_benchmarks(void);
void register_utf8_benchmarks(void);
void register_waitgroup_benchmarks(void);

int main(int argc, char **argv) {
    register_chan_benchmarks();
    register_clock_benchmarks();
    register_cond_benchmarks();
    register_context_benchmarks();
    register_defer_benchmarks();
    register_error_benchmarks();
    register_func_benchmarks();
    register_hash_benchmarks();
    register_iface_benchmarks();
    register_map_benchmarks();
    register_mem_benchmarks();
    register_mutex_benchmarks();
    register_num_benchmarks();
    register_panic_benchmarks();
    register_preempt_benchmarks();
    register_reclaim_benchmarks();
    register_runtime_benchmarks();
    register_sched_benchmarks();
    register_select_benchmarks();
    register_slice_benchmarks();
    register_str_benchmarks();
    register_sync_atomic_benchmarks();
    register_sync_benchmarks();
    register_sync_map_benchmarks();
    register_sync_pool_benchmarks();
    register_synctest_benchmarks();
    register_timer_benchmarks();
    register_utf8_benchmarks();
    register_waitgroup_benchmarks();
    return bench_main(argc, argv);
}
