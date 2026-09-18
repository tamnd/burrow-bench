# Contributing

This repository holds the benchmarks for [burrow](https://github.com/tamnd/burrow). Changes to burrow itself go there. Changes to how burrow is measured go here.

## Adding a benchmark

Four steps, and all four are required or something breaks quietly.

1. Write the C benchmark in `bench/<package>_bench.c`, using `BENCH` and `BENCH_LOOP`.
2. Register it in the `register_<package>_benchmarks` function at the bottom of the same file. CI checks that every declared benchmark is registered, because an unregistered one compiles fine and never runs.
3. Write the Go counterpart in `go/<package>_test.go` if there is one. Not everything has one. A benchmark comparing `str_eq` against `strcmp` is comparing two C things and does not need Go.
4. Add the pair to `tools/pairs.txt` if there is a Go counterpart. CI checks that both names exist.

Then run `make bench` and look at the number before you send it. A benchmark that reports 0.3 nanoseconds per operation is measuring an empty loop, not a fast function.

## The rule that matters most

Both sides do the same work, or the number is worthless.

This is easy to get wrong in the direction that flatters burrow, and it is easy to get wrong in the direction that does not. The `str_from_cstr` pair is a good example of the second kind. Go knows a string's length without looking, so `len(s)` is a field read and burrow has to call `strlen`, which makes burrow look about fifteen times slower at something it does not actually do. The comment in the benchmark says so, and so should yours when the comparison is uneven.

If you cannot make both sides do the same work, say so in a comment on both and in the README table. A benchmark with an honest caveat is useful. A benchmark with a hidden one is worse than nothing, because somebody will make a decision with it.

## Stopping the optimiser

Every result goes through `bench_keep` or `bench_keep_u64`. These are real calls into another translation unit, so the compiler cannot prove the value is dead and cannot delete the loop that produced it.

The usual failure looks like a benchmark that got a hundred times faster after a refactor that should not have changed anything. When you see that, the refactor let the optimiser see through something, and the number is about nothing. Check the disassembly before you celebrate.

## Timing setup you cannot avoid

`bench_pause` and `bench_resume` work like Go's `b.StopTimer` and `b.StartTimer`. Use them sparingly. If the untimed part is much bigger than the timed part, the harness has to run a very large number of iterations to accumulate enough measured time, and the wall clock goes up by that ratio. The harness watches both clocks and stops scaling when either reaches the target, so it will not hang, but you will get a benchmark that ran a hundred iterations and reports a number with no precision behind it.

Prefer restructuring so the setup is amortised inside the timed region, and say in a comment that it is amortised.

## Running the comparison

```sh
tools/run.sh -o results/<machine>-<date>.txt
```

The header it writes names the machine, the CPU, the compiler, the burrow commit and the Go version. Do not hand edit a results file, and do not commit one produced on a laptop that was doing other things, or on a shared CI runner. Numbers from a noisy machine are not a smaller version of good numbers, they are a different thing that looks the same.

## Style

C follows burrow's `.clang-format`, which is in this repository too. Run `make fmt`. Go follows `gofmt`, which CI checks.

Comments explain why, not what. `/* add one to i */` helps nobody. `/* differ in the last byte only, so the comparison has to read all of it */` is the sentence that stops somebody breaking the benchmark next year.
