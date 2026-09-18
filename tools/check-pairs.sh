#!/bin/sh
# Every line in tools/pairs.txt has to name a benchmark that exists on both
# sides.
#
# A pair naming a benchmark that was renamed or deleted does not fail anything
# by itself. It just makes a row quietly vanish from the comparison table, and
# a comparison table with a missing row looks exactly like a comparison table.
# That is the kind of rot this checks for.
#
# Copyright 2026 The burrow Authors. All rights reserved.
# Use of this source code is governed by a BSD-style licence that can be found
# in the LICENSE file.

set -eu

cd "$(dirname "$0")/.."

status=0

c_names=$(grep -ho 'BENCH([a-z0-9_]*)' bench/*_bench.c | sed 's/BENCH(//; s/)//' | sort)
go_names=$(grep -ho '^func Benchmark[A-Za-z0-9_]*' go/*_test.go | sed 's/^func Benchmark//' | sort)

while read -r line; do
	case "$line" in
	'#'* | '') continue ;;
	esac

	c=$(printf '%s\n' "$line" | awk '{print $1}')
	g=$(printf '%s\n' "$line" | awk '{print $2}')

	if [ -z "$c" ] || [ -z "$g" ]; then
		echo "pairs.txt: needs two columns: $line" >&2
		status=1
		continue
	fi

	if ! printf '%s\n' "$c_names" | grep -qx "$c"; then
		echo "pairs.txt: no C benchmark called $c" >&2
		status=1
	fi

	if ! printf '%s\n' "$go_names" | grep -qx "$g"; then
		echo "pairs.txt: no Go benchmark called Benchmark$g" >&2
		status=1
	fi
done <tools/pairs.txt

if [ "$status" -eq 0 ]; then
	echo "ok	pairs	$(grep -cv '^#\|^$' tools/pairs.txt) pairs"
fi

exit "$status"
