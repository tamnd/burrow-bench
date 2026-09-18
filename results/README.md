# Results

One file per run. The name is the machine and the date, so `server3-2026-09-18.txt`. Two runs on one machine on one day get a word on the end saying what the second one was for, so `server2-2026-09-18-sync.txt` is the run that first had the sync benchmarks in it.

Every file starts with a header naming the machine, the CPU, the compiler, the burrow commit, the burrow-bench commit and the Go version. `tools/run.sh` writes that header, which is the only reason to use the script rather than running the binary yourself.

These files are appended to, not replaced. A result from six months ago is not stale data, it is the point of comparison that tells you whether the last six months made anything faster.

## Where the numbers come from

Not from CI. A GitHub runner is a virtual machine sharing a host with strangers, and the variance between two consecutive runs on one is larger than most of the differences worth measuring. CI here builds the benchmarks and runs them once with a tiny iteration count, which proves they still work and proves nothing about speed.

Numbers come from a machine that is doing nothing else, run by hand.

## Reading them

The ratio column is the number that matters. Raw nanoseconds tell you about the machine.

A ratio under 1.0 means burrow is faster than Go at that operation. Over 1.0 means it is slower, and anything over about 1.2 that is not explained by a comment in the benchmark is a bug worth an issue against burrow.

Some pairs are uneven by construction and the ratio for those is not a verdict. `str_from_cstr` against Go's `len` is the clearest case: Go is comparing against a field read and burrow is comparing against a `strlen` call, so the ratio is large and means nothing except that C strings do not carry their length. The benchmark says so in a comment.

## The floor

The two harnesses do not pay the same price to stop the compiler deleting the work. `bench_keep` is a call into another translation unit, and with no link time optimisation that is a real call plus a volatile store on every iteration. The Go benchmarks assign to a package level variable, which is one store and no call. On the EPYC in results that is about 1.8 ns against about 0.4 ns.

So there is a floor, and burrow sits on it for anything that compiles to an instruction or two. `num_add` reads 1.84, `num_add_raw` reads 1.82, `strlen_short` reads 1.84 and `func_call_direct` reads 1.83. Those are four different pieces of code reporting the same number, which is the number the harness costs. The ratio column next to them says 2x to 4x and it is measuring the harness on both sides, not the code.

A row is telling you something when burrow is well clear of the floor, or when a `_raw` pair next to it reads differently. `utf8_rune_count_long` at 4263 against a raw loop at 1246 is a real gap. `num_add` at 1.84 is not a gap at all.

Fixing this properly means either giving the C side a cheaper barrier or giving the Go side the same expensive one, and the second is easier to get right. Until then the floor is written down here rather than left for somebody to rediscover from a confusing ratio.

