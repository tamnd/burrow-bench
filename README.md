# burrow-bench

Benchmarks for [burrow](https://github.com/tamnd/burrow), the C port of the Go standard library.

Two questions, asked over and over for every package as it lands.

**Is it as fast as Go?** A port that is slower than the thing it ports has no reason to exist, because the Go original is right there and it is free. The target is parity, and where burrow cannot reach parity the reason is written down rather than left for somebody to discover with a profiler.

**Is it as fast as the C people already write?** Nobody replaces a hand rolled `strstr` loop with a library call that costs twice as much, no matter how good the API is. So the comparison is against what the alternative actually is: libc, the common single purpose C libraries, and in a few cases the obvious loop somebody would write themselves.

Numbers, not adjectives. Everything in `results/` was produced by a script in `tools/` on a machine named in the file, and anybody can run the same script.

## Running them

```sh
make            # builds the benchmarks against burrow's main branch
make bench      # runs them
```

To benchmark a working copy instead of a fetched one:

```sh
make BURROW_DIR=../burrow bench
```

To compare against Go on a machine that has a Go toolchain:

```sh
tools/run.sh
```

That builds both sides, runs them with the same iteration counts, and prints a table with the ratio in the last column. The ratio is the number that matters. A raw nanosecond count tells you about the machine it was measured on and not much else.

## How a benchmark here is written

The harness copies Go's `testing.B`, because burrow will eventually port `testing` and these benchmarks should survive that with a search and replace rather than a rewrite.

```c
BENCH(arena_alloc_small) {
    Arena ar;
    arena_init(&ar, NULL, 0);
    Alloc *a = arena_allocator(&ar);

    BENCH_LOOP(b) {
        void *p = mem_alloc(a, 64, 8);
        bench_keep(p);
    }

    arena_free(&ar);
}
```

`BENCH_LOOP` runs the body enough times to make the measurement mean something, which it works out by running it and looking. `bench_keep` stops the optimiser deleting work whose result nobody reads, which is the single most common way a C benchmark ends up measuring nothing at all.

Setup outside the loop is not timed. If you need setup inside the loop, stop the clock around it with `bench_pause(b)` and `bench_resume(b)`, exactly as you would in Go.

## The rules

These exist because benchmark suites rot, and they rot in predictable ways.

**Both sides do the same work.** If the C version skips a bounds check the Go version performs, the number is not a comparison, it is a lie with a decimal point. Where the two genuinely cannot do the same work, the benchmark says so in a comment and the README entry says so too.

**The optimiser gets no free lunch.** Every result is either consumed by `bench_keep` or written somewhere the compiler cannot prove is dead. A benchmark that got faster by a factor of a thousand did not get faster.

**Allocation is counted, not hidden.** burrow's benchmarks report bytes per operation and allocations per operation next to the time, the same three columns Go prints, because the interesting part of an arena is usually the second two.

**The machine is named.** Every file in `results/` says which machine, which compiler, which flags, and which burrow commit. A number without those is not reproducible and therefore is not evidence.

**CI does not produce numbers.** The workflow builds the benchmarks and runs them once with a tiny iteration count, to prove they still compile and still finish. Shared runners are noisy enough that a regression check built on them would cry wolf until everybody ignored it. Real numbers come from a machine that is doing nothing else.

## What is measured so far

burrow is early and this tracks it. Right now that means the allocators and `Str`.

| Benchmark | Against | Notes |
| --- | --- | --- |
| `arena_alloc` | `malloc_free`, Go's `make` | The bump pointer path, which is the one that has to be fast |
| `arena_alloc_zeroed` | `calloc_free`, Go's `make` | Go zeroes everything, so the honest comparison zeroes too |
| `arena_alloc_large` | Go's `make` | A megabyte at a time, which leaves the bump path for the large one |
| `arena_append_grow` | `realloc_grow`, Go's `append` | The last allocation unwind is meant to make this grow in place |
| `arena_reset` | nothing | Resetting should not touch the memory it is keeping, so this should be flat |
| `heap_alloc` | `malloc_free` | The same malloc behind the allocator interface, so the gap is what the interface costs |
| `str_eq_long` | `memcmp_long`, Go's `==` | Same length, differing in the last byte, so the comparison reads all of it |
| `str_eq_different_lengths` | `strcmp_different_lengths` | Answered from the length words, which is where the design shows |
| `str_cmp_long` | `memcmp_long`, Go's `strings.Compare` | |
| `str_from_cstr_short` | `strlen_short` | Should be exactly `strlen` and nothing more |
| `str_clone_medium` | Go's `strings.Clone` | |
| `str_at` | `raw_index`, Go's `s[i]` | What the bounds check costs |

Two of the Go pairings are uneven and the table in `results/` says so where it matters. `str_from_cstr` against Go's `len` is comparing a `strlen` call against a field read, because a Go string carries its length and a `char *` does not. That gap is the cost of the boundary between C and burrow, it is paid once when a string enters the library, and it is not a fact about `Str`.

Everything else arrives as the packages do.

## Licence

BSD-3-Clause, matching burrow and Go. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
