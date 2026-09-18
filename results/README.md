# Results

One file per run. The name is the machine and the date, so `server3-2026-09-18.txt`.

Every file starts with a header naming the machine, the CPU, the compiler, the burrow commit, the burrow-bench commit and the Go version. `tools/run.sh` writes that header, which is the only reason to use the script rather than running the binary yourself.

These files are appended to, not replaced. A result from six months ago is not stale data, it is the point of comparison that tells you whether the last six months made anything faster.

## Where the numbers come from

Not from CI. A GitHub runner is a virtual machine sharing a host with strangers, and the variance between two consecutive runs on one is larger than most of the differences worth measuring. CI here builds the benchmarks and runs them once with a tiny iteration count, which proves they still work and proves nothing about speed.

Numbers come from a machine that is doing nothing else, run by hand.

## Reading them

The ratio column is the number that matters. Raw nanoseconds tell you about the machine.

A ratio under 1.0 means burrow is faster than Go at that operation. Over 1.0 means it is slower, and anything over about 1.2 that is not explained by a comment in the benchmark is a bug worth an issue against burrow.

Some pairs are uneven by construction and the ratio for those is not a verdict. `str_from_cstr` against Go's `len` is the clearest case: Go is comparing against a field read and burrow is comparing against a `strlen` call, so the ratio is large and means nothing except that C strings do not carry their length. The benchmark says so in a comment.
