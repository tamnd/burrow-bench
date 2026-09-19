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

On Linux, pin it:

```sh
tools/run.sh -pin 5 -o results/server2-2026-09-18.txt
```

Both sides get the same core, and Go also gets `GOMAXPROCS=1`, since a pinned Go process otherwise starts a thread per core and then fights itself for the one core it is allowed to use. Pick a core that is not core 0, because that is where interrupts land.

To run part of the suite, and to get a number a busy machine cannot ruin:

```sh
tools/run.sh -pin 5 -run hash -stat min -count 9
```

`-run` takes a substring of the C benchmark names. The Go side gets the matching Go names out of `tools/pairs.txt` rather than the same string, because `hash_int` and `HashInt` are one benchmark spelled two ways and no single pattern finds both.

`-stat min` prints the fastest of the runs instead of the median. The median is right on a machine that is doing nothing else, and none of the machines this project has are doing nothing else. Nothing another process does can make a benchmark run faster, so on a loaded box the minimum is the closest available thing to what a quiet machine would say, while the median tracks whatever else is running. The spread column is still printed next to it, because a minimum taken out of nine runs that were all interrupted is still not a measurement.

This matters more than it sounds like it should. An unpinned run gets migrated between cores partway through, arrives with a cold cache and a cold branch predictor, and on a two socket machine can end up reading memory attached to the other socket. Three runs of one benchmark on a lightly loaded server came back 9.3, 15.1 and 22.8 nanoseconds unpinned, and within one percent of each other pinned. The header of every results file records whether the run was pinned, so a file that does not say so is not evidence about anything smaller than a factor of two.

macOS has no equivalent. Thread affinity there is a hint and there is nothing that pins a process to a core, so `-pin` is Linux only and a Mac run says `pinned no` in its header.

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

`bench_hide` is the other half of that. It hands back the pointer it was given from a translation unit the caller's optimiser cannot see into, and it is for work the compiler would otherwise do at compile time because it can trace the input back to a constant. Use it once, outside the loop, on whatever the loop's work hangs off. An interface call is the case that needs it most: give clang a vtable it can see and the indirect call becomes a direct one and then disappears into the loop body.

Setup outside the loop is not timed. If you need setup inside the loop, stop the clock around it with `bench_pause(b)` and `bench_resume(b)`, exactly as you would in Go.

## The rules

These exist because benchmark suites rot, and they rot in predictable ways.

**Both sides do the same work.** If the C version skips a bounds check the Go version performs, the number is not a comparison, it is a lie with a decimal point. Where the two genuinely cannot do the same work, the benchmark says so in a comment and the README entry says so too.

**The optimiser gets no free lunch.** Every result is either consumed by `bench_keep` or written somewhere the compiler cannot prove is dead. A benchmark that got faster by a factor of a thousand did not get faster.

**Allocation is counted, not hidden.** burrow's benchmarks report bytes per operation and allocations per operation next to the time, the same three columns Go prints, because the interesting part of an arena is usually the second two.

**The machine is named.** Every file in `results/` says which machine, which compiler, which flags, and which burrow commit. A number without those is not reproducible and therefore is not evidence.

**CI does not produce numbers.** The workflow builds the benchmarks and runs them once with a tiny iteration count, to prove they still compile and still finish. Shared runners are noisy enough that a regression check built on them would cry wolf until everybody ignored it. Real numbers come from a machine that is doing nothing else.

## What is measured so far

burrow is early and this tracks it. Right now that means the allocators, `Str`, `Slice`, `Error`, `Map`, the hash under it, interface dispatch, function values, the arithmetic that Go defines and C leaves undefined, reading bytes as text, and the pieces the scheduler is going to be assembled from.

| Benchmark | Against | Notes |
| --- | --- | --- |
| `arena_alloc` | `malloc_free`, Go's `make` | The bump pointer path, which is the one that has to be fast |
| `arena_alloc_zeroed` | `calloc_free`, Go's `make` | Go zeroes everything, so the honest comparison zeroes too |
| `arena_alloc_large` | Go's `make` | A megabyte at a time, which leaves the bump path for the large one |
| `arena_append_grow` | `realloc_grow`, Go's `append` | The last allocation unwind is meant to make this grow in place |
| `arena_reset` | nothing | Resetting should not touch the memory it is keeping, so this should be flat |
| `heap_alloc` | `malloc_free` | The same malloc behind the allocator interface, so the gap is what the interface costs |
| `track_arena_alloc` | `arena_alloc` | The tracking allocator over an arena, so the gap is what the bookkeeping costs |
| `track_heap_alloc` | `heap_alloc` | The same over the heap, where the allocation underneath is no longer nearly free |
| `track_heap_quarantine` | `track_heap_alloc` | Allocate and free in a loop, which is the path that poisons the block and holds it |
| `str_eq_long` | `memcmp_long`, Go's `==` | Same length, differing in the last byte, so the comparison reads all of it |
| `str_eq_different_lengths` | `strcmp_different_lengths` | Answered from the length words, which is where the design shows |
| `str_cmp_long` | `memcmp_long`, Go's `strings.Compare` | |
| `str_from_cstr_short` | `strlen_short` | Should be exactly `strlen` and nothing more |
| `str_clone_medium` | Go's `strings.Clone` | |
| `str_at` | `raw_index`, Go's `s[i]` | What the bounds check costs |
| `slice_append_grow` | Go's `append` | A thousand ints one at a time, which is the shape of every parse loop |
| `slice_append_prealloc` | `raw_append_prealloc`, Go's `append` | The gap against `slice_append_grow` is what the growth costs |
| `slice_append_bulk` | Go's `append(s, xs...)` | Sixteen appends of sixty four instead of a thousand and twenty four of one |
| `slice_append_call` | `slice_append_prealloc` | The same loop through the function rather than the macro, so the inlining stays measured |
| `slice_make` | `calloc_1k`, Go's `make` | Zeroed, because Go's is |
| `slice_index` | `raw_index_int`, Go's `s[i]` | What the bounds check and the element size lookup cost |
| `slice_index_call` | `slice_index` | The function rather than the macro, the other half of that pair |
| `slice_copy` | `memcpy_same_size`, Go's `copy` | A memmove, because `copy(s, s[1:])` is how you delete an element |
| `error_is_sentinel` | Go's `errors.Is` | Once per read in every loop that reads to the end, so it has to be two loads and a branch |
| `error_is_chain` | Go's `errors.Is` | Five layers deep, which is what a chain looks like after three package boundaries |
| `error_as_chain` | Go's `errors.As` | Pointer comparison against Go's reflection |
| `error_message` | Go's `err.Error()` | The vtable dispatch, both sides holding a message that is already built |
| `error_return_ok` | Go's `return nil` | Two words through the return registers against a nil interface, which is the success path of the whole library |
| `error_new` | Go's `errors.New` | One allocation here against Go's, because the struct and its text share a block |
| `error_join_two` | Go's `errors.Join` | burrow builds the message now and Go builds it when somebody prints, so this one is slower on purpose |
| `error_is_tree` | Go's `errors.Is` | The depth first walk over what `Join` produces, rather than a chain |
| `hash_int` | Go's `maphash.Comparable` | Eight bytes, all per call cost, and the floor under every map operation |
| `hash_float64` | Go's `maphash.Comparable` | The same eight bytes through the float rules, so the gap is what those cost |
| `hash_str_short` | Go's `maphash.String` | Eleven bytes, a short name, which is one overlapping pair of loads on both sides |
| `hash_str_long` | Go's `maphash.String` | Sixty four bytes, a path or a URL, where the per byte cost starts to show |
| `hash_str_huge` | Go's `maphash.String` | A kilobyte, which is per byte cost and nothing else, and where AES should win outright |
| `map_get_hit_int` | Go's `m[k]` | Two Swiss tables, so this is the comparison the whole file exists for |
| `map_get_miss_int` | Go's `m[k]` | The answer a cache gets on the request that matters, and it has to prove absence |
| `map_get2_hit_int` | Go's `v, ok := m[k]` | The gap against `map_get_hit_int` is what copying the value out costs |
| `map_get_hit_str` | Go's `m[k]` | Eleven byte keys, a length both sides read in two loads, so the gap is the table |
| `map_get_miss_str` | Go's `m[k]` | The same with keys that differ in their first three bytes |
| `map_set_update` | Go's `m[k] = v` | A key that is already there, which never grows the table, where counters live |
| `map_set_grow` | Go's `append` of a map, `make` with no hint | Eleven doublings, which is where the two growth designs actually differ |
| `map_set_prealloc` | Go's `make(map, n)` | The gap against `map_set_grow` is what the growth costs |
| `map_set_prealloc_str` | Go's `make(map, n)` | The hash again, this time in construction rather than lookup |
| `map_del_miss` | Go's `delete` | The probe and nothing else, which is the floor for the pair below |
| `map_set_del_pair` | Go's `delete` then `m[k] = v` | Measuring a delete alone empties the table, and an LRU moves an entry this way anyway |
| `map_iter` | Go's `range m` | A scan over the control bytes with a skip for every slot that is not full |
| `map_churn` | Go's fill and `delete` loop | Fill a thousand, delete a thousand, forever, which is the tombstone reclamation showing up as a number |
| `map_clear_refill` | Go's `clear` then refill | The other way to reuse a map, and the one that keeps the memory |
| `iface_call` | `direct_call`, Go's interface call | A load and an indirect call, which is the claim the whole interface design makes |
| `direct_call` | `iface_call` | The same work without the interface, so the gap is what dispatch costs |
| `iface_call_two_types` | Go's | Two dynamic types at one call site, which is what real code looks like |
| `iface_convert` | Go's `var v I = c` | Two moves on both sides whenever the compiler knows both types |
| `iface_narrow` | Go's `var r io.Reader = rw` | A member address here against Go's `runtime.convI2I` and its cache |
| `iface_assert_hit` | Go's `v.(*T)` | One load of the descriptor and one pointer comparison |
| `iface_assert_miss` | Go's `v.(*T)` | The answer every arm of a type switch but one gets |
| `iface_type` | nothing | Reading the dynamic type, which is the first step of a type switch |
| `any_make` | Go's `var v any = p` | A descriptor and a pointer, no allocation on either side |
| `any_box` | Go's `var v any = i` | The one that allocates, arena against Go's heap, so read the allocation columns |
| `any_assert_hit` | Go's `v.(int)` | `iface_assert` with a descriptor in place of a vtable |
| `any_assert_miss` | Go's `v.(int64)` | |
| `any_equal_hit` | Go's `==` | What a `map[any]V` pays once the hash has found the slot |
| `any_equal_type_miss` | Go's `==` | The same number as two types, answered from the descriptors without reading either value |
| `io_copy_buffer_64k` | Go's `io.CopyBuffer` | Dispatch where it happens, two hundred and fifty six interface calls per iteration |
| `io_read_full_4k` | Go's `io.ReadFull` | Eight reads into one buffer, which is the shape of every header parse |
| `func_call` | `func_call_direct`, Go's closure call | A load and an indirect call, which is the claim the function value design makes |
| `func_call_direct` | `func_call` | The same work with the function named at the call site, so the gap is what the value costs |
| `func_call_env` | Go's closure over a variable | A target that reads its environment, which is a closure reading a capture |
| `func_call_write_env` | Go's closure assigning to a capture | Capture by reference, where the store cannot be hoisted out of either loop |
| `func_call_two_targets` | Go's | Two targets at one call site, which is what real code looks like |
| `func_call0` | Go's `func()` | The no argument shape, which is what `defer`, `go` and `sync.Once.Do` all take |
| `func_make` | Go's closure literal | Two stores here against a heap allocation in Go, so read the allocation columns |
| `func_higher_order` | Go's | Sixty four calls through a value passed into a loop, which is where people actually use one |
| `num_add` | `num_add_raw`, Go's `+` | A wrapping add against the plain operator, which should be the same instruction |
| `num_mul` | `num_mul_raw`, Go's `*` | The other operation that overflows on almost every hash input |
| `num_add_generic` | `num_add` | The `_Generic` macro, to show the selection happens at compile time and nowhere else |
| `num_div` | `num_div_raw`, Go's `/` | Two compares in front of a divide that already costs twenty to forty cycles |
| `num_mod` | `num_mod_raw`, Go's `%` | The same pair of checks on the remainder |
| `num_shl` | `num_shl_raw`, Go's `<<` | A count that goes past the width of the type, which is the case the header exists for |
| `num_shr` | Go's `>>` | The arithmetic shift, where past the width means a sign extension rather than zero |
| `num_from_float64` | `num_from_float64_raw`, Go's `int(f)` | A NaN check, two bound compares and a select around one convert instruction |
| `num_hash_round` | Go's | Sixty four wrapping multiplies in a chain, which is where these functions actually get used |
| `num_sum` | `num_sum_raw`, Go's `range` | An accumulator a long enough input overflows, in a loop both compilers want to unroll |
| `utf8_decode_ascii` | Go's `utf8.DecodeRuneInString` | One rune off the front, taking the one byte path, which should be a compare and a load |
| `utf8_decode_japanese` | Go's `utf8.DecodeRuneInString` | The same call taking the three byte path, so the gap is the table lookup and two range checks |
| `utf8_decode_slice` | Go's `utf8.DecodeRune` | The byte slice spelling of the row above, to show the `Slice` header costs nothing on the way in |
| `utf8_range_mixed` | `utf8_range_ascii_raw`, Go's `range` | The loop Go's compiler generates inline against one written out of two functions, which is the row that matters most here |
| `utf8_range_ascii` | `utf8_range_ascii_raw`, Go's `range` | The same loop on input that never leaves the one byte path |
| `utf8_range_ascii_raw` | `utf8_range_ascii` | The byte loop you should write when you know the input is ASCII, which is what the decode is being measured against |
| `utf8_rune_count_ascii` | Go's `utf8.RuneCountInString` | Ten characters, where the per call cost is most of the number |
| `utf8_rune_count_japanese` | Go's `utf8.RuneCountInString` | The same ten characters in thirty bytes |
| `utf8_rune_count_long` | `utf8_rune_count_long_raw`, Go's `utf8.RuneCountInString` | Four kilobytes, which is per byte cost and nothing else |
| `utf8_rune_count_long_raw` | `utf8_rune_count_long` | Counting the bytes that are not continuation bytes, which is the same answer by hand |
| `utf8_valid_ascii` | Go's `utf8.ValidString` | Ten bytes, too short for either side's word at a time skip to start |
| `utf8_valid_japanese` | Go's `utf8.ValidString` | Thirty bytes that never take the ASCII path at all |
| `utf8_valid_long` | Go's `utf8.ValidString` | Four kilobytes of ASCII, which is the word at a time skip and almost nothing else |
| `utf8_valid_mixed` | Go's `utf8.ValidString` | A line of real text, where the skip keeps stopping and restarting |
| `utf8_encode_ascii` | Go's `utf8.EncodeRune` | One byte out, which is a compare and a store |
| `utf8_encode_wide` | Go's `utf8.EncodeRune` | Four bytes out, an emoji, which is the longest form |
| `utf8_rune_len` | Go's `utf8.RuneLen` | Three compares and no memory at all |
| `atomic_add_u64` | Go's `atomic.AddUint64` | One instruction on both sides, so a gap here is a wrapper that failed to inline |
| `atomic_add_u32` | Go's `atomic.AddUint32` | The same on the width 32 bit machines do without a lock table |
| `atomic_load_u64` | Go's `atomic.LoadUint64` | An acquire load, which on arm64 is a different instruction and on x86 is an ordinary one |
| `atomic_store_u64` | Go's `atomic.StoreUint64` | The release half of the pair |
| `atomic_cas_u64` | Go's `atomic.CompareAndSwapUint64` | Always succeeding, because a failing one measures the retry loop instead |
| `note_cycle` | Go's `sync.WaitGroup` cycle | Close the gate, open it, walk through it, with nobody waiting, which is the case a scheduler hits most |
| `note_pingpong` | Go's unbuffered channel | A real handoff between two threads, against the same handoff between two goroutines, which is the gap the scheduler exists to close |
| `thread_start_join` | Go's `go` plus a `WaitGroup` | A clone syscall and a stack from the kernel, against a few hundred bytes from a free list |
| `thread_yield` | Go's `runtime.Gosched` | Giving the processor up through the kernel against giving a turn up in user space |
| `thread_self` | nothing | Go does not export a goroutine id on purpose, so there is nothing honest to pair it with |
| `context_switch` | Go's goroutine switch | Two switches per iteration on both sides, one of which goes through a scheduler and a channel and one of which does not |
| `context_make` | nothing | Laying a trampoline over a stack that already exists, which Go does not let a program do |
| `stack_alloc_free_min` | nothing | The mapping and the guard page a goroutine gets when nothing is cached, which is the number a free list has to beat |
| `stack_alloc_free_large` | nothing | A megabyte, because unmapping has to walk the page tables for everything it takes back |
| `stack_set_current` | nothing | What the scheduler will call on every switch, so it has to stay a thread local store |
| `stack_page_size` | nothing | Asked for on every allocation, and the header claims it is a memory read rather than a system call |
| `runq_put_get` | nothing | One goroutine through an empty local queue and back out, which is the path almost every goroutine in almost every program takes |
| `runq_put_get_next` | nothing | The same pair through the `runnext` slot, so the difference between the two rows is what the slot costs to have |
| `runq_fill_drain` | nothing | 256 in and 256 out, which is the same work with the cache line no longer permanently hot |
| `runq_steal_half` | nothing | One steal of half a full ring, so 128 goroutines moved and one handed back to run |
| `runq_put_slow` | nothing | What a put onto a full queue turns into: half the ring plus the new one onto a batch for the global queue |
| `gqueue_push_pop` | nothing | The global queue's list on its own, without the lock that in real use is around it |
| `goroutine_start` | Go's `go f()` plus a `Gosched` | A launch and the switch into it and out again, one at a time, which is the best case |
| `goroutine_start_batch` | the same, 64 at a time | The case a server produces, where the queue fills and the launcher is not what runs next |
| `goroutine_yield_alone` | Go's `runtime.Gosched` | Yielding with nothing else runnable, which is the scheduler looking, finding nothing and coming back |
| `goroutine_yield_pair` | the same with two goroutines | Every yield is a real switch, so one iteration is two of them |
| `goroutine_handoff` | Go's unbuffered channel | A blocking volley through `sched_park` and `sched_ready`, against the same volley through a channel |
| `runtime_start_stop` | nothing | Starting the whole runtime and stopping it again, which in Go means starting a process |

`ErrorfWrap` is on the Go side with no C counterpart, on purpose. `fmt.Errorf` with `%w` is how Go wraps in practice and burrow has no wrapping constructor until `fmt` lands, so the Go number is here first and `fmt_errorf` will arrive next to a target instead of next to nothing.

Two of the Go pairings are uneven and the table in `results/` says so where it matters. `str_from_cstr` against Go's `len` is comparing a `strlen` call against a field read, because a Go string carries its length and a `char *` does not. That gap is the cost of the boundary between C and burrow, it is paid once when a string enters the library, and it is not a fact about `Str`.

The hash rows are the one case so far where these benchmarks changed the library rather than reporting on it, and the story is worth keeping because it is how this is supposed to work. The map rows were expected to show the string lookups losing badly, because burrow hashed a byte at a time with FNV-1a and Go has an AES round per sixteen bytes. They did not show that. What they showed was the int key losing, which nobody predicted, and the reason is that FNV on an eight byte key is eight multiplies that each wait for the one before, with nothing else for the chip to do. Replacing it took thirteen to forty two percent off the map operations. The string keys were never the problem, and that is why `hash_int` exists and sits at the top of the table.

The interface rows have two things worth knowing before anybody quotes a ratio off them.

The first is that most of them measure one or two nanoseconds, and on that scale the harness is a large share of the number. The C side calls `bench_keep` once per iteration and that is an out of line call, while the Go side stores to a package level variable, which is a store. Both are doing their job, which is stopping the optimiser deleting the work, and neither is free. The gaps between the C rows are solid. The absolute C to Go ratio on a one nanosecond row is mostly a measurement of two different sinks.

The second is `iface_call`, which needs `bench_hide` to mean anything at all. Hand clang a vtable it can trace back to a constant and it turns the indirect call into a direct one, inlines the body, and leaves a loop that adds one to a register, which measures at the cost of an empty loop and looks like a triumph. The Go side hides the same thing by reading the value out of a package level variable. This is the same failure as a missing `bench_keep` wearing a different hat, and `bench_hide` exists because of it.

`iface_narrow` is the one uneven pair in the group and it is uneven in burrow's favour, so it is worth saying plainly what the difference is. Go converting an `io.ReadWriter` to an `io.Reader` calls `runtime.convI2I`, which finds the itab for the narrower interface in a cache, because that itab is a separate object. burrow keeps the narrower vtable inside the wider one, so the conversion is the address of a member. Different work, not a faster version of the same work.

The function value rows have the same harness floor under them and one finding on top of it. On a quiet core, `func_call` measured 3.23 nanoseconds against `func_call_direct` at 1.85, and Go's pair measured 2.24 against 0.54. The absolute gap between the two languages is the floor, since the C side calls `bench_keep` per iteration and Go stores to a package variable, and it is the same 1.3 nanoseconds on every row here. What the rows say once you stop reading the absolute numbers is that a call through a function value costs 1.38 nanoseconds more than a call the compiler can see through, and that the same step in Go costs 1.70. A function value is not paying for being written out by hand.

`func_higher_order` is the row where that stops needing an argument, because it calls sixty four times per sink rather than once, so the floor is divided by sixty four rather than added to it. It came out at 133 nanoseconds against Go's 177, which is 2.09 nanoseconds a call against 2.77. That is the number to quote if somebody wants one.

`func_make` is the uneven pair and it is uneven in burrow's favour, so here is the difference. A Go closure that captures a variable and outlives the frame it was made in goes on the heap, which the row shows as 16 bytes and one allocation per operation. A function value here is two words built next to an environment struct the caller already had, so it allocates nothing. That is the whole trade of this design visible in one row: you write the environment struct out by hand, and in exchange building a value is free. The rows above it are where you find out that calling one is free too.

The Go side of these needed the same treatment `bench_hide` gives the C side, and the first version of the file did not have it. A closure written inside a benchmark is one Go inlines straight through, so the environment row reported 0.93 nanoseconds against a real call's 2.5, and the higher order row was three times quicker per call than the row it is meant to be comparable with. Every closure in `go/func_test.go` is now built by a function that captures its argument and read out of a package level variable, which is the Go spelling of the same trick.

The numeric rows are the cheapest thing in this repository to measure wrongly, and the reason is in the first paragraph of the interface section above. Half of them are one or two nanoseconds, which is the same scale as the harness, so the column to read is not the Go one. It is `num_add` against `num_add_raw`, both of which carry the same `bench_keep` call, and which came out at 1.89 and 1.87 nanoseconds on a pinned core. The multiply pair is 1.85 against 1.87, the shift pair is 1.89 against 1.80, and `num_add_generic` is 1.80, which is `_Generic` selecting a function name at compile time and costing nothing at run time. Those pairs are the claim `burrow/num.h` makes, which is that Go's overflow and shift rules are free in C once they are written out, and they hold.

The divide pair is the one where a cost would show if there were one, since `int_div` checks for a zero divisor and for the `MinInt / -1` case that faults on x86 before it divides. It measured 8.76 against the bare operator's 8.55 and against Go's 8.67. Two compares in front of a forty cycle instruction do not show up, and Go's compiler emits its own check in the same place for the same reason.

`num_hash_round` and `num_sum` are the rows worth quoting, because they call sixty four and two hundred and fifty six times per sink, so the harness floor is divided rather than added. The hash round came out at 75.7 nanoseconds against Go's 81.9, and the sum at 139.7 against 136.6. Level, which is the answer these should give: the arithmetic is identical on both sides and neither compiler is being stopped from unrolling anything.

What none of these rows measure is the case the header exists for, which is a shift count of 70 or a value of 1e300 arriving in a conversion. Those are correctness, they are tested in burrow rather than here, and the C row that looks like a fair comparison for them is undefined behaviour with a plausible number attached. `num_shl_raw` is that row: on x86 it answers with the low six bits of the count, which is a different number, not a faster one.

The UTF-8 rows are the fairest comparison in the repository, and that is not a formality. `unicode/utf8` is pure computation over a byte slice with no allocator, no interface dispatch and no runtime underneath it, so both sides are doing the same table lookups and the same compares on the same bytes. A gap on one of these is burrow's to explain rather than the harness's.

They are also the rows that have changed burrow the most for their size, and both changes came from a number nobody expected.

`utf8_range_mixed` is the one to read first. It is `for i, r := range s` against the same loop written out of `str_runes` and `str_next_rune`, and Go's version is generated by the compiler with no call in it, which makes a call per rune the obvious way for a C port to lose. It was losing: 270.58 nanoseconds against Go's 117. Almost none of that was the decoding. It was the iterator, which a call the compiler cannot see into has to leave in memory, so every iteration stored the offset and loaded it back before it could do anything else. Inline, with only a rune of two bytes or more going out to a real call, the same loop is 87.26 against Go's 104.40, and `utf8_range_ascii` went from 50.26 to 8.29 against Go's 12.14.

`utf8_valid_long` is the other one. Four kilobytes of ASCII is the word at a time skip and almost nothing else, and burrow did not have that skip, on the stated grounds that reading a word at a time needs an unaligned load and an answer to the endianness question. The row said 5946.64 nanoseconds against Go's 153, which is a large enough number to make somebody check the reasoning, and the reasoning was wrong. A `memcpy` of a fixed word size is defined on any alignment and every compiler emits the single load, and byte order does not come into it because the only question asked of the value is whether the high bit is set anywhere in it and that mask has the same bit in every byte. The row now reads 172.64 against Go's 147.

There is a third finding in there that is worth recording because it is not obvious. Go's own helper builds the word out of eight separate byte loads and shifts, on purpose, to stay out of unsafe, and the first attempt at this ported that literally. In C it is slower: gcc merges the shifts back into one load when there is a single word in the expression and gives up when two of them are combined, which is exactly what the two word and four word steps do. That version measured 979 nanoseconds. Porting Go's source faithfully is usually the right instinct in burrow and this is a place where it costs five times.

`utf8_decode_ascii`, `utf8_encode_ascii` and `utf8_rune_len` have the harness floor under them the same way the interface rows do, and it is worse here because the Go side of all three is a constant the compiler folds. Half a nanosecond against four is two different sinks, not two different decoders. Read those three against each other and against the Japanese rows next to them.

`utf8_rune_count_long` is an open row rather than a finding. Neither side has a word at a time skip in `RuneCount`, both loops are a compare and two increments per ASCII byte, and burrow measures 4562.28 against Go's 2429. The plain byte loop next to it, `utf8_rune_count_long_raw`, is 1389.64, so the shape of the loop is not the problem and something about the general one is. Nobody has taken it apart yet.

The runtime rows are the ones to read least literally, because eleven of the twelve have no Go pair and three of them are not meant to be fast. They are here so that the scheduler can be measured against the parts it is made of rather than against nothing.

`context_switch` is the row with a pair and the pair is uneven on purpose. Both sides pass a turn back and forth twice per iteration, and Go's version goes through the scheduler and an unbuffered channel while burrow's saves registers and changes a stack pointer. So the burrow number is a floor and the Go number is the target, and the distance between them is the budget a goroutine switch has to fit inside. On an M4 that came out at 36.40 nanoseconds against Go's 231.60, which says there is about 195 nanoseconds of room for everything a real scheduler has to do, and that if the finished thing lands anywhere near Go it will not be the switch that made it slow. On server2, an EPYC with the run pinned to two cores, the same row is 30.46 against Go's 659.30, so the budget there is over 600 nanoseconds. The gap between the two machines is mostly Go's side rather than burrow's, and the honest reading is that the floor is stable across both and the target is not.

`stack_alloc_free_min` is a thousand nanoseconds and that is the correct answer to the wrong question. It is one `mmap`, one `mprotect` and one `munmap`, and Go does not pay that per goroutine because it keeps 2, 4, 8 and 16 kilobyte spans in a per P cache and only goes to the kernel when the cache is empty. burrow will have the same cache and the rows for it go in next to these. Until then this row is the price of not having one, which is the most useful thing it can be: a launch that costs a microsecond cannot reach ten million launches a second on any number of cores.

`stack_page_size` came out at 7 nanoseconds on macOS, which is slower than the header's description of it as a memory read led anybody to expect. It is `sysconf`, so it is a call and a switch rather than a syscall, and against a thousand nanosecond allocation it is well under one percent and not worth caching yet. It is worth a row, because the day it becomes a syscall on some platform this is where it will show.

The six run queue rows are the first pieces of the scheduler itself rather than of the floor under it, and they are all one thread on one P, which is the case worth having a number for. The ring is lock free for its owner by design, so an uncontended put is meant to be a load and a store, and the day one of these rows doubles is the day somebody put an atomic read modify write on the hot path. What contention costs is a different question and it needs a benchmark with threads in it, which goes in when the scheduler does.

On an M4, `runq_put_get` is 5.74 nanoseconds for a put and a get together and `runq_put_get_next` is 9.71 for the same pair through the `runnext` slot. The slot is two compare and swaps where the ring is a store and one compare and swap, so the four nanoseconds between those rows is what it costs to keep a freshly readied goroutine out of the ring. That is worth paying: the thing it buys is that a channel handoff runs the receiver on the core that has the value in its cache, and a cache miss on that value costs a good deal more than four nanoseconds.

`runq_steal_half` is 59.51 nanoseconds to move 128 goroutines and hand one back, which is under half a nanosecond each, and `runq_put_slow` is 121.59 to move 129 onto a batch. Both are around a tenth of what the same goroutines cost to move one at a time through `runq_put_get`, which is the answer to whether stealing and overflowing in batches earn their complexity. They do, and by an order of magnitude. `gqueue_push_pop` at 2.36 nanoseconds is the global queue without the lock that is around it in real use, so it is there to confirm the list stays out of the way rather than to be made faster.

Everything else arrives as the packages do.

## Licence

BSD-3-Clause, matching burrow and Go. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
