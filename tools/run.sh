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
# Which CPU to pin both sides to. Empty means do not pin.
#
# On a server with other people's work on it, the difference between a pinned
# run and an unpinned one is the difference between a measurement and an
# average of the scheduler. An unpinned benchmark gets migrated between cores
# mid run, arrives with a cold cache and a cold branch predictor, and on a
# machine with more than one socket it can end up reading memory attached to the
# other one. Three runs of one benchmark on a lightly loaded server came back
# 9.3, 15.1 and 22.8 nanoseconds unpinned, and within one percent of each other
# pinned.
#
# Pick a core that is not core 0, because that is where interrupts land.
PIN=${PIN:-}

# A substring of the C benchmark names, or an exact name ending in $, empty for
# all of them. The Go side gets the matching Go names out of pairs.txt rather
# than the same string, since hash_int and HashInt are the same benchmark
# spelled two ways and no single pattern matches both.
RUN=${RUN:-}

# Which number out of the repetitions ends up in the table.
#
# median is the default and is right on a machine that is doing nothing else.
# min is for the machines this project actually has, which are shared and busy.
# Nothing another process does can make a benchmark run faster, so on a loaded
# box the minimum is the closest thing to the number a quiet machine would give,
# and the median moves around with whatever else is running. It is also why the
# spread column is still printed next to it: a min with a 200 percent spread is
# a number that was measured once and interrupted six times.
STAT=${STAT:-median}
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
	-pin)
		PIN=$2
		shift 2
		;;
	-run)
		RUN=$2
		shift 2
		;;
	-stat)
		STAT=$2
		case "$STAT" in
		median | min) ;;
		*)
			echo "run.sh: -stat takes median or min, not $STAT" >&2
			exit 2
			;;
		esac
		shift 2
		;;
	*)
		echo "usage: tools/run.sh [-o results/name.txt] [-time seconds] [-count runs] [-pin cpu] [-run substring or name$] [-stat median|min]" >&2
		exit 2
		;;
	esac
done

# The pinning command, worked out once.
#
# taskset on Linux. macOS has no equivalent a benchmark can use: thread
# affinity there is a hint to the scheduler and there is nothing that pins a
# process to a core, so a run on a Mac says so in the header rather than
# pretending. If taskset is missing on a Linux box, that is nothing but a
# missing util-linux and worth saying out loud, because the alternative is a
# results file that claims a pin it did not get.
pin_cmd=""
pin_note="no"
if [ -n "$PIN" ]; then
	if command -v taskset >/dev/null 2>&1; then
		pin_cmd="taskset -c $PIN"
		pin_note="cpu $PIN with taskset"
	else
		echo "run.sh: -pin $PIN asked for but taskset is not installed" >&2
		exit 2
	fi
fi

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
	echo "pinned     $pin_note"
	echo "statistic  $STAT of $COUNT runs"
	if [ -n "$RUN" ]; then
		echo "filter     $RUN"
	fi
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

# Both sides get the same treatment, pin included. Pinning one and not the
# other would put the difference between the two environments into the ratio
# column and call it a result.
#
# Go gets GOMAXPROCS=1 alongside the pin, because a pinned Go process still
# starts a thread per core and then fights itself for the one core it is allowed
# to use.
c_filter=""
if [ -n "$RUN" ]; then
	c_filter="-run $RUN"
fi

# shellcheck disable=SC2086
$pin_cmd "$BUILD/bench" -time "$TIME" -count "$COUNT" $c_filter -tsv >"$c_out"

# The Go pattern comes out of pairs.txt, because the two sides spell their names
# differently and translating one into the other is exactly what that file is.
# A filter that matches no pair still has to be a pattern Go accepts, and one
# that matches nothing is the honest answer to a filter that matched nothing.
go_filter="."
if [ -n "$RUN" ]; then
	# A trailing $ asks for the exact name, the same as it does for the C side.
	names=$(awk -v r="$RUN" '
		BEGIN { exact = sub(/\$$/, "", r) }
		(exact ? $1 == r : index($1, r)) { printf "%s%s", (n++ ? "|" : ""), $2 }' tools/pairs.txt)
	if [ -n "$names" ]; then
		go_filter="^Benchmark($names)\$"
	else
		go_filter='^$'
	fi
fi

if command -v go >/dev/null 2>&1; then
	go_env=""
	if [ -n "$pin_cmd" ]; then
		go_env="GOMAXPROCS=1"
	fi
	# shellcheck disable=SC2086
	(cd go && env $go_env $pin_cmd go test -run '^$' -bench "$go_filter" -benchmem -benchtime "${TIME}s" -count "$COUNT" ./...) >"$go_out"
else
	: >"$go_out"
fi

# ---------------------------------------------------------------- the table

table() {
	awk -v c="$c_out" -v g="$go_out" -v p=tools/pairs.txt -v stat="$STAT" '
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
		# already reduced its own repetitions: column three is their median,
		# column seven the fastest of them, and column nine how far apart the
		# fastest and the slowest were.
		while ((getline line < c) > 0) {
			n = split(line, f, "\t")
			if (n < 3 || f[1] == "name") continue
			cns[f[1]] = (stat == "min" && n >= 7) ? f[7] : f[3]
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
				gv = (stat == "min") ? glo[gname] : median(gvals, gname, gcount[gname])
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

		heading = 0
		# The spread is worked out before the printf rather than inside it. A
		# > inside a print statement is an output redirection to awk, not a
		# comparison, so a ternary in an argument list is a syntax error on the
		# awk that ships with macOS and silently something else elsewhere.
		for (k in cns) {
			if (k in seen) continue
			if (!heading++) {
				print ""
				print "not paired with a Go benchmark"
			}
			cs = cspread[k] >= 0 ? sprintf("%.0f%%", cspread[k]) : "-"
			printf "%-26s %12.2f %8s\n", k, cns[k], cs
		}

		print ""
		if (stat == "min") {
			print "Minimum of the runs, not the median, so these survive a busy machine"
			print "better than the spread column next to them suggests. A spread in the"
			print "hundreds still means most of the runs were interrupted, and a minimum"
			print "taken out of runs that were all interrupted is not a measurement."
		} else {
			print "A spread above about ten percent means the machine was busy and the"
			print "ratio next to it is not a measurement. Run it again somewhere quiet,"
			print "or use -stat min, which is what the shared boxes need."
		}
	}'
}

if [ -n "$out" ]; then
	# A bare name means results/, which is where the file was going to end up
	# anyway. Without this, -o server3.txt writes to the repository root and
	# says "wrote server3.txt", and you find out days later that the run you
	# were waiting on is not where you went looking for it.
	case "$out" in
	*/*) ;;
	*) out="results/$out" ;;
	esac
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
