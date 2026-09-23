#!/bin/sh
# Compare two burrow trees in cycles and instructions rather than in time.
#
#     tools/ab.sh ../burrow-old ../burrow goroutine_start goroutine_yield_pair
#
# Builds the benchmarks once against each tree, then runs each named benchmark
# under perf stat on both, alternating old and new five times, and prints the
# fewest user space cycles and the instructions of that run, per iteration.
#
# This is the tool for a change to burrow itself, where the question is whether
# the new tree is faster than the old one and the machine is somebody else's
# build server. Time on a shared machine moves by a factor of two with the load,
# and a minimum over wall clock runs only helps so far. A cycle count only
# moves with what the core did for this process, so two trees measured within
# a minute of each other on the same core compare well even at a load of
# thirty. Instructions do not move at all, which makes them the check on the
# cycles: fewer instructions and more cycles is a stall worth looking at.
#
# Linux only, since it needs perf and taskset. Names are exact, so
# goroutine_start does not also run goroutine_start_batch.
#
# Copyright 2026 The burrow Authors. All rights reserved.
# Use of this source code is governed by a BSD-style licence that can be found
# in the LICENSE file.

set -eu

cd "$(dirname "$0")/.."

if [ $# -lt 3 ]; then
	printf 'usage: tools/ab.sh OLD_TREE NEW_TREE BENCHMARK...\n' >&2
	exit 2
fi
for tool in perf taskset; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		printf 'ab: needs %s, which is Linux only\n' "$tool" >&2
		exit 1
	fi
done

old=$1
new=$2
shift 2

# Which core, and how many iterations per run. Enough iterations that the
# startup and the runtime's own threads are lost in the total.
PIN=${PIN:-5}
N=${N:-2000000}
ROUNDS=${ROUNDS:-5}

make -s BUILD=build-ab-old BURROW_DIR="$old" build-ab-old/bench
make -s BUILD=build-ab-new BURROW_DIR="$new" build-ab-new/bench

# Prints "cycles instructions" for one run, both per iteration.
measure() {
	out=$(perf stat -x, -e cycles:u,instructions:u taskset -c "$PIN" "$1" -run "$2\$" -n "$N" -count 1 2>&1 >/dev/null)
	cyc=$(printf '%s\n' "$out" | grep 'cycles' | cut -d, -f1)
	ins=$(printf '%s\n' "$out" | grep 'instructions' | cut -d, -f1)
	case "$cyc$ins" in
	*[!0-9]* | '')
		printf 'ab: perf did not count %s, is perf_event_paranoid too high?\n' "$2" >&2
		exit 1
		;;
	esac
	printf '%d %d\n' $((cyc / N)) $((ins / N))
}

printf '%-28s %10s %10s   %10s %10s\n' benchmark 'old cyc' 'old ins' 'new cyc' 'new ins'
for b in "$@"; do
	best_old=0 ins_old=0 best_new=0 ins_new=0
	r=0
	while [ "$r" -lt "$ROUNDS" ]; do
		# shellcheck disable=SC2046
		set -- $(measure build-ab-old/bench "$b")
		if [ "$best_old" -eq 0 ] || [ "$1" -lt "$best_old" ]; then
			best_old=$1 ins_old=$2
		fi
		# shellcheck disable=SC2046
		set -- $(measure build-ab-new/bench "$b")
		if [ "$best_new" -eq 0 ] || [ "$1" -lt "$best_new" ]; then
			best_new=$1 ins_new=$2
		fi
		r=$((r + 1))
	done
	printf '%-28s %10d %10d   %10d %10d\n' "$b" "$best_old" "$ins_old" "$best_new" "$ins_new"
done
