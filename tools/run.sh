#!/bin/sh
# Run both sides and print the comparison.
#
# Needs a Go toolchain. Without one it runs the C side alone and says so,
# because a burrow number on its own is still worth having.
#
# The output goes to stdout and, if you pass -o, to a file in results/ with a
# header naming the machine, the compiler and the burrow commit. Do not commit
# a results file without that header. A number nobody can reproduce is not
# evidence, it is a rumour with a decimal point.
#
# Copyright 2026 The burrow Authors. All rights reserved.
# Use of this source code is governed by a BSD-style licence that can be found
# in the LICENSE file.

set -eu

cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
BURROW_DIR=${BURROW_DIR:-$BUILD/burrow}
TIME=${TIME:-1}
# Five runs per benchmark by default, so that the table carries a spread and
# not just a number. Go's -count defaults to one and everybody regrets it.
COUNT=${COUNT:-5}
out=""

while [ $# -gt 0 ]; do
	case "$1" in
	-o)
		out=$2
		shift 2
		;;
	-time)
		TIME=$2
		shift 2
		;;
	-count)
		COUNT=$2
		shift 2
		;;
	*)
		echo "usage: tools/run.sh [-o results/name.txt] [-time seconds] [-count runs]" >&2
		exit 2
		;;
	esac
done

# ---------------------------------------------------------------- the header

uname_s=$(uname -s)
uname_m=$(uname -m)
cc=${CC:-cc}
cc_version=$($cc --version 2>/dev/null | head -1 || echo unknown)

case "$uname_s" in
Linux)
	cpu=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//' || echo unknown)
	;;
Darwin)
	cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)
	;;
*)
	cpu=unknown
	;;
esac

make BURROW_DIR="$BURROW_DIR" >/dev/null

burrow_id=$(git -C "$BURROW_DIR" describe --always --dirty 2>/dev/null || echo unknown)
bench_id=$(git describe --always --dirty 2>/dev/null || echo unknown)

header() {
	echo "machine    $(hostname)"
	echo "os         $uname_s $uname_m"
	echo "cpu        $cpu"
	echo "compiler   $cc_version"
	echo "burrow     $burrow_id"
	echo "bench      $bench_id"
	echo "date       $(date -u '+%Y-%m-%d %H:%M:%SZ')"
	if command -v go >/dev/null 2>&1; then
		echo "go         $(go version | cut -d' ' -f3)"
	else
		echo "go         not installed, C side only"
	fi
	echo
}

# ----------------------------------------------------------------- the runs

c_out=$BUILD/c.tsv
go_out=$BUILD/go.txt

"$BUILD/bench" -time "$TIME" -count "$COUNT" -tsv >"$c_out"

if command -v go >/dev/null 2>&1; then
	(cd go && go test -run '^$' -bench . -benchmem -benchtime "${TIME}s" -count "$COUNT" ./...) >"$go_out"
else
	: >"$go_out"
fi

# ---------------------------------------------------------------- the table

table() {
	awk -v c="$c_out" -v g="$go_out" -v p=tools/pairs.txt '
	# The median of a set of samples held as vals[key, 1..n]. Median and not
	# mean, for the same reason the harness uses one: a single hiccup in one run
	# out of five moves a mean and does not move a median.
	function median(vals, key, n,   i, j, t, a) {
		for (i = 1; i <= n; i++) a[i] = vals[key, i]
		for (i = 2; i <= n; i++)
			for (j = i; j > 1 && a[j] < a[j-1]; j--) { t = a[j]; a[j] = a[j-1]; a[j-1] = t }
		if (n % 2 == 1) return a[(n+1)/2]
		return (a[n/2] + a[n/2+1]) / 2
	}
	BEGIN {
		# The C side, tab separated, with a header line to skip. The harness has
		# already taken the median over its own repetitions, and column nine is
		# how far apart the fastest and slowest of them were.
		while ((getline line < c) > 0) {
			n = split(line, f, "\t")
			if (n < 3 || f[1] == "name") continue
			cns[f[1]] = f[3]
			cspread[f[1]] = (n >= 9) ? f[9] : -1
		}
		# The Go side, whatever go test prints. The name carries a -N suffix for
		# GOMAXPROCS, and the ns/op is the field before the literal ns/op. With
		# -count there is one line per repetition, so they are collected and
		# reduced here rather than the last one winning.
		while ((getline line < g) > 0) {
			n = split(line, f, /[ \t]+/)
			if (n < 3 || f[1] !~ /^Benchmark/) continue
			name = f[1]
			sub(/-[0-9]+$/, "", name)
			sub(/^Benchmark/, "", name)
			for (i = 1; i <= n; i++) {
				if (f[i] != "ns/op") continue
				v = f[i-1] + 0
				gcount[name]++
				gvals[name, gcount[name]] = v
				if (!(name in glo) || v < glo[name]) glo[name] = v
				if (!(name in ghi) || v > ghi[name]) ghi[name] = v
			}
		}

		printf "%-26s %12s %8s %12s %8s %8s\n", "benchmark", "burrow ns", "spread", "go ns", "spread", "ratio"
		printf "%-26s %12s %8s %12s %8s %8s\n", "--------------------------", "------------", "--------", "------------", "--------", "--------"

		while ((getline line < p) > 0) {
			if (line ~ /^#/ || line ~ /^[ \t]*$/) continue
			split(line, f, /[ \t]+/)
			cname = f[1]; gname = f[2]
			if (!(cname in cns)) continue
			cs = cspread[cname] >= 0 ? sprintf("%.0f%%", cspread[cname]) : "-"
			if (gname in gcount && gcount[gname] > 0) {
				gv = median(gvals, gname, gcount[gname])
				gs = glo[gname] > 0 ? sprintf("%.0f%%", (ghi[gname]-glo[gname])/glo[gname]*100) : "-"
				if (gv > 0)
					printf "%-26s %12.2f %8s %12.2f %8s %8.2fx\n", cname, cns[cname], cs, gv, gs, cns[cname]/gv
				else
					printf "%-26s %12.2f %8s %12s %8s %8s\n", cname, cns[cname], cs, "-", "-", "-"
			} else {
				printf "%-26s %12.2f %8s %12s %8s %8s\n", cname, cns[cname], cs, "-", "-", "-"
			}
			seen[cname] = 1
		}

		print ""
		print "not paired with a Go benchmark"
		# The spread is worked out before the printf rather than inside it. A
		# > inside a print statement is an output redirection to awk, not a
		# comparison, so a ternary in an argument list is a syntax error on the
		# awk that ships with macOS and silently something else elsewhere.
		for (k in cns) {
			if (k in seen) continue
			cs = cspread[k] >= 0 ? sprintf("%.0f%%", cspread[k]) : "-"
			printf "%-26s %12.2f %8s\n", k, cns[k], cs
		}

		print ""
		print "A spread above about ten percent means the machine was busy and the"
		print "ratio next to it is not a measurement. Run it again somewhere quiet."
	}'
}

if [ -n "$out" ]; then
	mkdir -p "$(dirname "$out")"
	{
		header
		table
	} | tee "$out"
	echo
	echo "wrote $out"
else
	header
	table
fi
