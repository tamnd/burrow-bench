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

`-run` takes a substring of the C benchmark names, or an exact name if it ends in `$`, so `-run 'goroutine_start$'` runs that row and not `goroutine_start_batch` as well. The Go side gets the matching Go names out of `tools/pairs.txt` rather than the same string, because `hash_int` and `HashInt` are one benchmark spelled two ways and no single pattern finds both.

`-stat min` prints the fastest of the runs instead of the median. The median is right on a machine that is doing nothing else, and none of the machines this project has are doing nothing else. Nothing another process does can make a benchmark run faster, so on a loaded box the minimum is the closest available thing to what a quiet machine would say, while the median tracks whatever else is running. The spread column is still printed next to it, because a minimum taken out of nine runs that were all interrupted is still not a measurement.

This matters more than it sounds like it should. An unpinned run gets migrated between cores partway through, arrives with a cold cache and a cold branch predictor, and on a two socket machine can end up reading memory attached to the other socket. Three runs of one benchmark on a lightly loaded server came back 9.3, 15.1 and 22.8 nanoseconds unpinned, and within one percent of each other pinned. The header of every results file records whether the run was pinned, so a file that does not say so is not evidence about anything smaller than a factor of two.

To compare two burrow trees rather than burrow and Go, which is the question when a change to burrow is meant to make something faster:

```sh
tools/ab.sh ../burrow-old ../burrow goroutine_start goroutine_yield_pair
```

That builds the benchmarks against each tree and runs each named benchmark on both under `perf stat`, taking turns five times, and prints the fewest user space cycles per iteration with the instruction count of that run. On a shared machine this is the number to trust. Wall clock time on the servers this project uses moves by a factor of two with the load, while a cycle count only moves with what the core did for this process, so two trees measured a minute apart on the same core compare well at a load of thirty. The instruction count does not move at all, which makes it a check on the cycles: fewer instructions and more cycles means a stall worth looking at. It needs `perf` and `taskset`, so it is Linux only.

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

**Two results files are not a comparison.** A file in `results/` is a measurement of one machine at one moment, so subtracting one from another taken hours later measures the hours. To find out what a change cost, build both trees side by side, interleave the runs rather than doing all of one and then all of the other, and read the rounds against each other. This rule is here because ignoring it produced a repeatable 45 percent regression that did not exist, which is written up in [results/server2-2026-09-19-sysmon.txt](results/server2-2026-09-19-sysmon.txt).

**The library in the binary is the library you think it is.** burrow is built into `$(BURROW_DIR)/build-bench` rather than its own `build`, because make hands a variable set on its command line down to every sub-make, and a `make BUILD=whatever` used to move burrow's library somewhere the link line was not looking while an older one sat where it was. A benchmark compiled against one version of a header and linked against a library built from another is two definitions of the same struct in one binary, and what it produces is not a crash, it is a table of plausible numbers. This one cost an afternoon and the numbers it invented were only caught because they disagreed with the same change measured on a laptop.

**CI does not produce numbers.** The workflow builds the benchmarks and runs them once with a tiny iteration count, to prove they still compile and still finish. Shared runners are noisy enough that a regression check built on them would cry wolf until everybody ignored it. Real numbers come from a machine that is doing nothing else.

## What is measured so far

burrow is early and this tracks it. Right now that means the allocators, `Str`, `Slice`, `Error`, `Map`, the hash under it, interface dispatch, function values, the arithmetic that Go defines and C leaves undefined, reading bytes as text, the pieces the scheduler is assembled from, the scheduler itself, the monotonic clock underneath it, the timers built on that, channels and the select in front of them, defer, panic, the locks and the two containers in `sync`, and `context`.

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
| `sync_atomic_add_int64` | Go's `atomic.AddInt64` | The public package rather than the internal layer, so this row and `atomic_add_u64` should read the same |
| `sync_atomic_load_int64` | Go's `atomic.LoadInt64` | The same load through the public name, sequentially consistent because that is the only ordering Go's package has |
| `sync_atomic_int64_add` | Go's `atomic.Int64.Add` | The typed struct, which is the plain function with the address of a field and should cost exactly that |
| `sync_atomic_int64_load` | Go's `atomic.Int64.Load` | The same again for a load |
| `sync_atomic_int64_cas` | Go's `atomic.Int64.CompareAndSwap` | Always succeeding, the same as the row above it |
| `sync_atomic_bool_load` | Go's `atomic.Bool.Load` | A 32 bit word and a compare against zero, since there is no byte wide operation underneath |
| `sync_atomic_pointer_load` | Go's `atomic.Pointer.Load` | One untyped type here against one instantiation per element type there |
| `sync_atomic_pointer_store` | Go's `atomic.Pointer.Store` | A sequentially consistent store, which is an exchange on x86 and a store release on arm64 |
| `sync_atomic_value_load` | Go's `atomic.Value.Load` | Two words published without a lock, so a load is a load, a branch and a load |
| `sync_atomic_value_store` | Go's `atomic.Value.Store` | Every store after the first one, which is the path a configuration swap takes |
| `mutex_lock_unlock` | Go's `sync.Mutex` | A lock nobody holds, which is one compare and swap and is the case nearly every lock in a running program finds |
| `mutex_try_lock_unlock` | Go's `Mutex.TryLock` | Reads the state before the compare and swap, so it is a dearer lock rather than a cheaper one |
| `mutex_locker_lock_unlock` | Go's `sync.Locker` | The same lock behind an indirect call, which is what code that takes a Locker instead of a Mutex pays |
| `rw_mutex_lock_unlock` | Go's `RWMutex.Lock` | The write side, which goes through the inner mutex before it announces itself to the readers |
| `rw_mutex_r_lock_unlock` | Go's `RWMutex.RLock` | The read side with no writer in sight, which is one add in and one add out |
| `mutex_contended` | the same in Go | Four goroutines on four Ps and an empty critical section, which is the shape starvation mode exists for |
| `rw_mutex_contended_read` | the same in Go | Four readers and no writer, which is the honest answer to whether a read lock is free |
| `wait_group_add_done` | Go's `sync.WaitGroup` | An Add and a Done with nobody waiting, which is one atomic each on one word plus the misuse checks both sides run |
| `wait_group_wait_empty` | Go's `WaitGroup.Wait` | A Wait on a group already at zero, which returns without queueing and happens more often than it sounds like it would |
| `wait_group_go_wait` | Go's `WaitGroup.Go` | Four goroutines started and waited for, which is mostly the scheduler and is the row that says what using a group costs |
| `once_do` | Go's `sync.Once` | The already ran path, which is one load and a branch on both sides and is what a Once costs in every call but the first |
| `once_func_call` | Go's `sync.OnceFunc` | A struct the caller declares against a closure the collector owns, so the absolute figures say more than the ratio |
| `once_value_get` | Go's `sync.OnceValue` | The same again with a value coming back, an `Any` here against a type parameter there |
| `cond_signal_empty` | Go's `sync.Cond.Signal` | A signal with nobody waiting, which is the call a producer that keeps up with its consumers makes most of the time |
| `cond_broadcast_empty` | Go's `sync.Cond.Broadcast` | The same for a broadcast, and both sides answer it without touching a lock |
| `cond_ping_pong` | the same in Go | Two goroutines taking turns through one Cond, so each iteration is two handoffs and is mostly the scheduler |
| `sync_map_load_hit` | Go's `sync.Map.Load` | A key that is there, which is the call the whole structure is shaped around |
| `sync_map_load_miss` | the same in Go | A key that is not, which stops partway down the trie and never copies anything |
| `sync_map_has_hit` | nothing | The same lookup with the value thrown away, so the gap against `sync_map_load_hit` is what copying a value costs |
| `sync_map_load_or_store_hit` | Go's `sync.Map.LoadOrStore` | The path that finds the key already there and has to get out before taking a lock |
| `sync_map_store_update` | Go's `sync.Map.Store` | Storing over a key that exists, which is a lookup, a lock and a new entry spliced into the chain |
| `sync_map_store_delete_pair` | the same in Go | A store and the delete that undoes it, so the trie is the same size every iteration |
| `sync_map_range` | Go's `sync.Map.Range` | A full walk of a thousand entries, which is one pin held across the whole thing |
| `sync_pool_get_put` | Go's `sync.Pool.Get` and `Put` | A pair back to back on one P, which fills the private slot and empties it again with no atomic anywhere |
| `sync_pool_get_put_pair` | the same in Go | Two out and two back, so the second of each goes through the ring rather than the private slot |
| `sync_pool_get_new` | the same in Go | A Get on a pool with nothing in it, which walks every slot in both generations and then calls the new function |
| `sync_pool_sweep_idle` | nothing | What the once a second tick costs when there is nothing to throw away, which is what it costs almost every time it fires |
| `note_cycle` | Go's `sync.WaitGroup` cycle | Close the gate, open it, walk through it, with nobody waiting, which is the case a scheduler hits most |
| `note_pingpong` | Go's unbuffered channel | A real handoff between two threads, against the same handoff between two goroutines, which is the gap the scheduler exists to close |
| `thread_start_join` | Go's `go` plus a `WaitGroup` | A clone syscall and a stack from the kernel, against a few hundred bytes from a free list |
| `thread_yield` | Go's `runtime.Gosched` | Giving the processor up through the kernel against giving a turn up in user space |
| `thread_self` | nothing | Go does not export a goroutine id on purpose, so there is nothing honest to pair it with |
| `mcontext_switch` | Go's goroutine switch | Two switches per iteration on both sides, one of which goes through a scheduler and a channel and one of which does not |
| `mcontext_make` | nothing | Laying a trampoline over a stack that already exists, which Go does not let a program do |
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
| `chan_uncontended` | Go's buffered channel | One send and one receive with nobody else in the program, so the lock, the ring and the copy and nothing else |
| `chan_nonblocking` | Go's `select` with a `default` | A receive from an empty channel that answers instead of blocking, which is what a poll loop pays per turn |
| `chan_pingpong_unbuffered` | the same in Go | The same volley as `note_pingpong` and `goroutine_handoff`, written the third way, which is the way a Go program writes it |
| `chan_pingpong_buffered` | the same in Go | Capacity one, so the send never blocks and only the receive does, and the difference is what that change buys |
| `chan_prodcons` | Go's `range` over a channel | One value down a pipeline with a hundred slots, where the scheduler gets involved once per buffer full rather than once per value |
| `runtime_start_stop` | nothing | Starting the whole runtime and stopping it again, which in Go means starting a process |
| `nanotime` | nothing | One reading of the monotonic clock, which every timer and every deadline starts with |
| `nanotime_elapsed` | Go's `time.Since` | One reading and a subtraction on both sides, which is the only shape Go lets an outsider compare against |
| `nanotime_interval` | nothing | Two readings and the subtraction between them, which is what timing a piece of work costs |
| `note_timeout_hit` | nothing | A timed sleep on a gate that is already open, which is the path a parking thread takes almost every time |
| `note_timeout_poll` | nothing | The same call with no time in it, which is how a caller looks without waiting |
| `timer_arm_stop` | Go's `AfterFunc` plus `Stop` | A whole timer from nothing to stopped, allocation included, which is the deadline round trip for a program that does not keep its timers |
| `timer_reset_stop` | Go's `Reset` plus `Stop` | One timer armed and cancelled over and over, which is a connection moving its read deadline, and the row with no allocator in it |
| `timer_reset_stop_deep` | the same with a thousand others in the heap | Whether the cost of a deadline grows with the number of connections, which it should not |
| `timer_sleep_1ms` | Go's `time.Sleep` | Read as latency rather than throughput, since the number worth having is the overshoot past a millisecond |
| `timer_fire_burst` | the same in Go | A thousand timers due at once and the wait until every callback has run, which is a heap pop and a goroutine launch each |
| `select_uncontended` | Go's two arm `select` | A select over two arms with one of them ready, which is a channel operation with a decision on the front of it |
| `select_default` | Go's `select` with a `default` | Two arms and a default with nothing ready, which is a poll loop's cost per turn |
| `select_eight_arms` | the same in Go | Eight arms with the ready one moving around the ring, which is where the lock ordering shows up |
| `select_pingpong` | the same in Go | A volley where all four operations are selects rather than bare channel operations |
| `defer_one` | Go's `defer` | One scope with one deferred call in it, which is the shape nearly every defer has |
| `defer_direct` | Go's call with no `defer` | The same call written at the bottom of the block by hand, so the gap between this row and the one above it is the feature |
| `defer_four` | the same in Go | Four calls, which is half of what a burrow scope holds without asking anybody for memory |
| `defer_eight` | the same in Go | Eight, which is the last one a scope holds in the frame, and the number Go's compiler stops open-coding at |
| `defer_scope_empty` | nothing | A scope with nothing deferred in it, which is what an early error return out of a function with a scope pays |
| `defer_loop_ten` | the same in Go | Ten turns of a loop with a defer in the body, which is the one place the two languages deliberately do different things |
| `panic_try_empty` | Go's deferred `recover` that finds nothing | A guarded call that does not panic, which is every request that goes fine, and the row that decides whether guarding is affordable |
| `panic_caught` | the same in Go | The same block with a panic in it and nothing in the way, so the gap from the row above is what going wrong costs |
| `panic_caught_scopes` | the same in Go | Three frames deep with a deferred call in each, which is the shape a real panic has, since the reason to unwind is that there is cleanup to run |
| `callers_ten` | Go's `runtime.Callers` | Ten frames deep with room for the whole stack, which is what a crash reporter or a profiler asks for |
| `callers_two` | the same in Go | The same stack with room for two frames, which is what a logger asks for when all it wants is the line above it |
| `context_done` | Go's `Context.Done` | The call every select on a cancellation goes through, a load here against an atomic load and a branch there |
| `context_err_live` | Go's `Context.Err` | A context nobody has cancelled, so it is an uncontended lock and unlock plus two loads on both sides |
| `context_value_shallow` | Go's `Context.Value` | The value on the context being asked, so one interface comparison and no walk |
| `context_value_deep` | the same in Go | Eight levels up, so the gap against the row above divided by seven is what a level costs |
| `context_value_miss` | the same in Go | Eight levels and then the root, finding nothing, which is what a lookup for somebody else's key costs |
| `context_with_cancel` | Go's `context.WithCancel` | Make it, cancel it, free it, which is the whole life of the context a request handler holds |
| `context_with_cancel_nested` | the same in Go | The same under a cancellable parent, so the gap against the row above is what attaching and detaching cost |
| `context_with_timeout` | Go's `context.WithTimeout` | The same life with a deadline an hour out, so the timer is armed and stopped and never fires |
| `context_with_timeout_expired` | the same in Go | A deadline already gone by, so no timer is armed and the context comes back already cancelled |
| `context_with_value` | Go's `context.WithValue` | A four word node and no lock at all on either side |
| `context_cancel_tree` | the same in Go | Sixty four children under one parent and a cancel that has to reach all of them, which is a request fanning out |

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

`mcontext_switch` is the row with a pair and the pair is uneven on purpose. Both sides pass a turn back and forth twice per iteration, and Go's version goes through the scheduler and an unbuffered channel while burrow's saves registers and changes a stack pointer. So the burrow number is a floor and the Go number is the target, and the distance between them is the budget a goroutine switch has to fit inside. On an M4 that came out at 36.40 nanoseconds against Go's 231.60, which says there is about 195 nanoseconds of room for everything a real scheduler has to do, and that if the finished thing lands anywhere near Go it will not be the switch that made it slow. On server2, an EPYC with the run pinned to two cores, the same row is 30.46 against Go's 659.30, so the budget there is over 600 nanoseconds. The gap between the two machines is mostly Go's side rather than burrow's, and the honest reading is that the floor is stable across both and the target is not.

`stack_alloc_free_min` is a thousand nanoseconds and that is the correct answer to the wrong question. It is one `mmap`, one `mprotect` and one `munmap`, and Go does not pay that per goroutine because it keeps 2, 4, 8 and 16 kilobyte spans in a per P cache and only goes to the kernel when the cache is empty. burrow will have the same cache and the rows for it go in next to these. Until then this row is the price of not having one, which is the most useful thing it can be: a launch that costs a microsecond cannot reach ten million launches a second on any number of cores.

`stack_page_size` came out at 7 nanoseconds on macOS, which is slower than the header's description of it as a memory read led anybody to expect. It is `sysconf`, so it is a call and a switch rather than a syscall, and against a thousand nanosecond allocation it is well under one percent and not worth caching yet. It is worth a row, because the day it becomes a syscall on some platform this is where it will show.

The six run queue rows are the first pieces of the scheduler itself rather than of the floor under it, and they are all one thread on one P, which is the case worth having a number for. The ring is lock free for its owner by design, so an uncontended put is meant to be a load and a store, and the day one of these rows doubles is the day somebody put an atomic read modify write on the hot path. What contention costs is a different question and it needs a benchmark with threads in it, which is the next thing to add here now that there is a scheduler to run them on.

On an M4, `runq_put_get` is 5.74 nanoseconds for a put and a get together and `runq_put_get_next` is 9.71 for the same pair through the `runnext` slot. The slot is two compare and swaps where the ring is a store and one compare and swap, so the four nanoseconds between those rows is what it costs to keep a freshly readied goroutine out of the ring. That is worth paying: the thing it buys is that a channel handoff runs the receiver on the core that has the value in its cache, and a cache miss on that value costs a good deal more than four nanoseconds.

`runq_steal_half` is 59.51 nanoseconds to move 128 goroutines and hand one back, which is under half a nanosecond each, and `runq_put_slow` is 121.59 to move 129 onto a batch. Both are around a tenth of what the same goroutines cost to move one at a time through `runq_put_get`, which is the answer to whether stealing and overflowing in batches earn their complexity. They do, and by an order of magnitude. `gqueue_push_pop` at 2.36 nanoseconds is the global queue without the lock that is around it in real use, so it is there to confirm the list stays out of the way rather than to be made faster.

The six scheduler rows are the first ones in this repository where both sides run the same program. Starting a goroutine, yielding, and handing a turn to another goroutine and blocking until it comes back are things Go has and burrow now has, so these are comparisons rather than budgets. All of them run with one P on both sides, because with more than one the two goroutines in a handoff land on two cores and the number turns into a measurement of the cache line between them. That is worth measuring and it is a different benchmark, and it goes in when there is a contended row to put beside it.

On server2, an EPYC with both sides pinned to one core, burrow is between three and four times quicker than Go on every one of them. A launch is 142.43 nanoseconds against 487.80, a batch of 64 is 6908.92 against 22570, a yield with nothing else to run is 50.92 against 136.80, a yield between two goroutines is 100.80 against 267.90, and a blocking volley is 150.07 against 682.30. The ratios are 0.29x, 0.31x, 0.37x, 0.38x and 0.22x.

Read those with two things in mind. The handoff row is uneven in burrow's favour, because Go's version goes through an unbuffered channel and burrow's goes through `sched_park` and `sched_ready` under a one waiter gate, so Go is doing strictly more work and the 0.22x is the room a channel has to fit into rather than a win. The other four are even, and the reason burrow is ahead is not cleverness, it is that Go's scheduler carries things burrow has not built yet: it checks timers on every pass, it polls the network, it has preemption bookkeeping on every goroutine, and it has a garbage collector with opinions about when a goroutine may be moved. Every one of those is coming here, and every one of them costs something. The honest way to read this table today is as a starting position with room in it, and the test of the scheduler is whether these rows still look like this after timers and the netpoller and preemption have landed on top of them.

`goroutine_yield_alone` at 50.92 nanoseconds is the row most worth watching. Nothing runs and nothing switches, and it still goes through `mcall` to the scheduler stack, takes the scheduler lock, puts the goroutine on the global queue, and finds it there again. That is Go's design and it is correct, since a goroutine that yields and goes straight back to the front of its own queue has not yielded to anything, but it means a polite compute loop pays fifty nanoseconds a call for nothing. Preemption is what makes those loops stop calling it.

`runtime_start_stop` has no Go pair and cannot have one. It creates the Ps, starts a thread for each, runs one goroutine and joins every thread, and on an M4 it is about 680 microseconds. It is here because burrow can be started and stopped many times in one process, which Go cannot do and which anybody embedding this in a larger C program will end up doing, so it is a number that should be watched rather than allowed to grow quietly.

The clock rows are small numbers that multiply. `burrow__nanotime` is what every timer, every deadline and every timed sleep is built on, and the scheduler is about to read it on every pass round the loop, so a clock that costs thirty nanoseconds instead of three is a tax on all of that. On server2, pinned, one reading is 36.96 nanoseconds and the reading with a subtraction on it is 36.38, which is the same number twice and says the subtraction is free. `nanotime_interval`, the two reading version, is 72.53, which is the same number again doubled. Nothing in this path is arithmetic.

Thirty seven nanoseconds for a clock read is slow, and it is the machine rather than the code. server2 is a virtual machine with `kvm-clock` as its clocksource, which is read through the vdso but is not the plain `rdtsc` a bare metal box with `tsc` gets. An M4 does the same call in about fourteen, and that one has a multiply and a divide in it to turn mach ticks into nanoseconds. The row to watch is not the absolute number, it is whether it ever jumps by a factor of twenty, because that is what a clock read falling out of the vdso and into a real system call looks like.

The paired row reads 0.84x against Go's `time.Since`, which is the answer it should give. Both sides read the monotonic clock once and subtract once, and Go's version carries a little more with it because a `time.Time` is wider than an `int64`.

`note_timeout_hit` is the row that was worth adding, because it found something. It is a timed sleep on a gate somebody has already opened, which is the case a parking thread hits nearly every time: it looks for work, finds none, and goes to sleep with a deadline, and by then the wake it was racing has landed. The comment in the benchmark says the row should stay level with `note_cycle` and that drifting above it means the timeout path has picked up a clock reading it does not need. It was above it. The timed sleep was reading the clock to work out a deadline before it had looked at the gate, and on the portable backend it was taking a mutex as well. burrow now reads the gate first, and the row moved from 21.9 nanoseconds to 14.7 on an M4 and from 296.99 to 9.73 on server2, which is level with `note_cycle` again.

Which leaves the larger thing those two numbers were sitting inside. `note_cycle` was 352.58 nanoseconds against Go's 17.35, a ratio of 20x, and it was not the sleep that cost it. It was the wake. burrow's `note_wake` went into the kernel with a futex call every time, including when nobody was waiting, and the Go row it is paired against is a `sync.WaitGroup` that keeps its waiter count in the word next to its counter and therefore never makes the call. Three hundred nanoseconds to open a gate nobody is standing at is a real problem for a scheduler that parks and unparks threads constantly.

burrow does the same thing now, and the row is in [results/server2-2026-09-19-note-count.txt](results/server2-2026-09-19-note-count.txt). A note keeps a count of the threads that are about to sleep on it, a wake with nobody in that count stays in user space, and `note_cycle` went from 352.58 nanoseconds to 10.96 on the same machine in the same conditions. The ratio went from 20.32x to 0.72x, so the row that was the worst on the board is now slightly ahead of Go. `note_timeout_hit` came with it, from 296.99 to 9.73, which is what the benchmark comment predicted would happen: the two rows do the same work when the gate is already open, so they should track each other, and they do.

`note_pingpong` did not move, and that is the right outcome rather than a disappointment. Two threads handing a turn back and forth means somebody really is asleep every time, so there is a real wake to make and no user space shortcut to take. It sits at 8795.94 nanoseconds against Go's 545.90, a ratio of 16x, and at the time that was the worst gap on the board. It is also a different problem: Go's unbuffered channel hands off between two goroutines on the same thread without the kernel ever hearing about it, and burrow will not close that gap with a faster note, it will close it with a scheduler that parks goroutines instead of threads. That is what the channel work below is for, and the channel section at the end of this file is where that prediction gets checked.

The ten `sync_atomic_` rows are the public `sync/atomic` package rather than the internal atomics under it, and the reason to have both sets is that the second one is a claim about the first. burrow's public functions are static inline over the internal ones, so every row here has a row above it that ought to read the same, and on server2 they do: 5.91 nanoseconds against 6.02 for an add, 1.88 against 1.89 for a load, 5.90 against 5.91 for a compare and swap. The layer costs nothing, which is what it was written to cost. The typed structs come out level with the plain functions for the same reason, 5.85 against 5.91 on add, so putting a counter in a `SyncAtomicInt64` buys the type discipline for free.

Against Go the arithmetic rows are level, between 0.97x and 1.01x, and that is the whole expectation: both sides emit one instruction and there is nothing left to be different about. The load rows are not level, 1.88 against 0.49, and that gap is the harness rather than the library. burrow's loop hands its result to `bench_keep_u64`, which is a real call into a translation unit the optimiser cannot see into, and Go's loop stores to a package level variable the compiler is happy to keep in a register. The internal `atomic_load_u64` row has had the same 3.9x on it since the first run, which is the tell: two different loads with the same floor under them are one floor, not two loads.

`sync_atomic_value_load` is 3.88 nanoseconds against Go's 2.45 and `sync_atomic_value_store` is 6.27 against 7.98, so the two word publish costs about what it should on both sides. The store being ahead of Go is not a win worth claiming, since Go's `Value` stores an interface the compiler may have to box and burrow's stores two words that are already words.

The timer rows are the first ones where burrow has a whole package to compare rather than a piece of one, and four of the five pair cleanly with Go because `AfterFunc`, `Stop`, `Reset` and `Sleep` mean the same thing on both sides. They are all on one P for the same reason the scheduler rows are: timers live in a heap per P, so with more than one the work spreads out and the row measures the machine instead of the timer. What contention across Ps costs is a different benchmark and on this design it should be nothing, which is a claim worth checking once there is something to check it against.

On server2, pinned, against Go 1.26, the whole table is in [results/server2-2026-09-19-timer.txt](results/server2-2026-09-19-timer.txt). `timer_arm_stop` is 206.68 nanoseconds against Go's 298.10, `timer_reset_stop` is 85.57 against 109.00, `timer_reset_stop_deep` is 83.73 against 107.60, `timer_sleep_1ms` is 1131938 against 1136778, and `timer_fire_burst` is 357715 against 639100.

`timer_reset_stop` is the row to quote. It is one timer armed and cancelled over and over, which is what a connection with a read deadline on it does on every single read, and it is the only row with no allocator anywhere in it. `timer_arm_stop` above it is a fair comparison of two programs somebody would write and an unfair comparison of two timer implementations, because Go allocates from its heap and burrow is handed an arena.

`timer_reset_stop_deep` is the row that makes the argument, and the argument is that it is the same number as the row above it. It is the same work with a thousand other timers already in the heap, which is a four way heap five levels deep against one level for an empty one, so if the cost of a deadline grew with the number of connections it would show here as a multiple. It does not show at all. Stopping marks the timer instead of pulling it out of the heap, so a reset finds it still sitting there, and the sift that would move it is a comparison and no swap when every neighbour is also an hour away. That is Go's design and the reason it is Go's design is this row.

The same shape holds on an M4, where three runs of the pair gave 87, 113 and 158 nanoseconds for the arm row against 59, 51 and 66 for the reset row, and 41, 47 and 77 for the deep one. Those are noisy enough that no two of them should be subtracted from each other, and they are quoted anyway because the ordering survives the noise and the ordering is the claim: arming from nothing costs more than resetting, and resetting does not care how many timers are already there.

`timer_sleep_1ms` is level with Go on Linux and that is the whole finding on that row, but it was not level on macOS and chasing that turned up something worth recording. A one millisecond sleep on macOS comes back at one and a half, every time. It is not the timers and it is not the scheduler. A parked thread waits on `pthread_cond_timedwait_relative_np` and macOS gives that timeout a leeway of half the interval so it can coalesce the wakeup with something else, which the way the overshoot scales confirms: 1ms comes back at 1.50, 5ms at 7.51, 10ms at 15.02. Linux waits on a futex and has no such thing, which is why the server2 row is 1.00x. The way out on macOS is to block in `kevent` instead, which is what Go does, so this is the netpoller's problem rather than a bug with a fix in it. It is here so that the row moving on macOS is recognised as the netpoller landing rather than as luck.

`timer_fire_burst` is the other half of the design and the only row here where timers really fire. A thousand of them come due at once and the row waits until every callback has run, so it is a heap pop and a goroutine launch a thousand times over, and the launch is most of it. It comes out at 0.56x, which is roughly where the scheduler rows already are, so what this row says is that firing a timer costs a goroutine launch and nothing much on top. Both sides spin on a counter with a yield in it rather than waiting properly, because burrow has no wait group yet, so neither number is what this costs in a program that waits on something. The comparison is still fair because both sides pay the same spin.

The monitor thread has no row of its own and is not going to get one. What it does is notice a timer that has come due on a processor nobody is holding, which should never happen, so there is no throughput in it to measure and a benchmark that made it happen would be measuring a program that is already broken. What it can do is cost something, and that is answerable: it is a thread that wakes up on a timer inside every burrow program, and the scheduler rows are where the cost would land if there were one.

[results/server2-2026-09-19-sysmon.txt](results/server2-2026-09-19-sysmon.txt) is that measurement, and the answer is that nothing here can see it. Three trees on one machine, the release before the monitor, the monitor as merged, and the same source with the thread deliberately not started so that what the thread does at runtime is separated from what adding fields to the scheduler's global struct does to its layout. Interleaved, minimum of eleven, both sides pinned. Every column moves both ways across rounds and no tree sits consistently on either side of another.

That file is worth reading for how it went wrong rather than for the table in it, because the first attempt found a 45 percent regression on `goroutine_yield_alone` and repeated it. It was the machine. The comparison was against a results file taken on the same box earlier the same day, the load average went from under one to over seven in between, and every row moved with it. That is where the rule about two results files not being a comparison came from.

Channels are where the prediction above gets checked, and it held. [results/server2-2026-09-19-pingpong.txt](results/server2-2026-09-19-pingpong.txt) is the three rows side by side, same machine, same pin, same minute. `note_pingpong` is 8624.47 nanoseconds. `chan_pingpong_unbuffered` is 231.98. That is the same program written twice, once with two threads handing a turn through the kernel and once with two goroutines handing a value through a channel, and the second one is thirty seven times cheaper. Go's side of both rows is about 550, so the row that was 15.59x Go is now 0.42x Go.

The thing to take from that pair is not the ratio, it is that they are the same program. Nothing about notes got faster. A blocking handoff between two threads costs a wake and a sleep and always will, and the way to not pay it is to have something to run that is not a thread, which is what the scheduler is for. Every number in the note file is still true and still the cost of doing it the expensive way.

[results/server2-2026-09-19-chan.txt](results/server2-2026-09-19-chan.txt) is the rest of the channel rows. `chan_uncontended` is 36.80 against Go's 48.71, which is a send and a receive on a buffered channel with nobody else in the program, so it is the lock, the ring arithmetic and the copy and nothing else. That row is the one that says whether one lock per channel was the right call, and at 37 nanoseconds with no scheduler involved the lock is not the problem. `chan_nonblocking` is 6.33 against 8.19, which is the unlocked early reject doing its job. `chan_prodcons`, a value down a hundred deep buffer, is 40.97 against 59.98.

`goroutine_handoff` is the floor under the ping pong rows and it is 163.35 on the same machine, so the channel costs about 70 nanoseconds on top of the park and ready it is built from. That is the lock on both channels, two waiter records, the direct copy, and the queue work, paid twice per volley. It is also the number to watch: `goroutine_handoff` volleys through a one waiter gate rather than a channel, so if that 70 nanosecond gap grows it is the channel getting heavier and not the scheduler.

One row did not do what its comment predicted. `chan_pingpong_buffered` was supposed to be cheaper than the unbuffered one, on the grounds that a send into an empty one deep buffer never blocks, and it is not: 232.74 against 231.98, which is the same number twice. Go does the same thing, 566.30 against 549.20. The reason is that a volley is symmetric. The send does not block, so the receive on the other side does instead, and the two goroutines still take turns exactly as often. A buffer only buys something when one side can run ahead, which is what `chan_prodcons` measures and why that row is six times cheaper. The comment in the benchmark has been left as it was written, because a prediction that turned out wrong is more useful in the file than a prediction quietly corrected afterwards.

The select rows are the second case where a benchmark changed the library. [results/server2-2026-09-19-select.txt](results/server2-2026-09-19-select.txt) is the table as it was when they landed, and `select_eight_arms` was 549.81 nanoseconds against Go's 417.70. burrow took the channel locks by walking the case list for the lowest address above the last one it locked, which needs no storage and reads better than a sort and is quadratic, and the argument for it was that a real select has two or three arms. Eight arms said the argument was wrong: a quarter of the call was in the unlock. burrow sorts the arms now, as Go does, and [results/server2-2026-09-21-select.txt](results/server2-2026-09-21-select.txt) has that row at 310.94 against 413.70. The row is kept at eight arms so that anybody who revisits the decision has to answer it again.

The defer rows are new, two of them said burrow was slower, and one of those two has already been fixed. [results/server2-2026-09-21-defer.txt](results/server2-2026-09-21-defer.txt) is the table as it was when they landed, and the first thing to look at is `defer_direct`, at 2.39 nanoseconds against Go's 2.29. That is the same call on both sides with no defer anywhere near it, so the two baselines agree and every other number in the group can be read against them.

`defer_one` is 13.54 against Go's 3.90. Go's compiler open-codes a defer of a named function into the frame, so its one deferred call is a flag byte, a stored argument and a direct call at the return, and it is hard to beat by design. burrow's is a function value in a scope on a chain, and the chain is the part worth looking at, because `defer_scope_empty` is 8.76 nanoseconds on its own. Two thirds of what a defer costs here is opening and closing the scope it lives in, and the thing that costs is asking which goroutine is running so the scope can go on its chain. That is the one left to chase.

`defer_eight` was the other one and the louder one, and it is the row that changed the library. It was 126.99 nanoseconds against Go's 30.27, where `defer_four` just above it was 29.27 against 15.17. Four extra deferred calls cost 98 nanoseconds, which is not four calls, it is one `malloc` and one `free`. A burrow scope held four calls in the caller's frame and asked the heap for room past that, and eight is past that. Go stops open-coding at eight too and falls back to a record per call on its heap, and it still came out four times cheaper, so Go's compiler was not the explanation.

A scope holds eight now, which is the same number Go's compiler open-codes, and the trade is sixty four more bytes in a stack frame against a trip through the allocator. The bytes are free in time because that array is deliberately never initialised. [results/server2-2026-09-21-defer-eight.txt](results/server2-2026-09-21-defer-eight.txt) is the same table afterwards: `defer_eight` is 55.29 against Go's 30.75, so the ratio went from 4.20x to 1.80x, and every other row in the group sat still, which is what a change that only removes an allocation should do.

`defer_loop_ten` is 144.05 against Go's 257.10 and it is the one row here that is not the same work on both sides, which is the point of it. burrow puts the scope inside the loop body, so each turn runs its own call and nothing accumulates. Go holds all ten until the function returns, because a defer in a loop is never open-coded, so it allocates ten records. The ratio is a fact about the two designs rather than about the two implementations, and the reason the row exists is that this is the single place where burrow's defer deliberately does something Go's does not.

The panic rows are the newest, and they are the third case where a benchmark changed the library before the feature shipped. [results/server2-2026-09-21-panic.txt](results/server2-2026-09-21-panic.txt) is the table.

`panic_caught` is 51.29 nanoseconds against Go's 380.40, and `panic_caught_scopes`, which puts three frames and three deferred calls in the way, is 104.33 against 703.50. Those are the two rows that matter to anybody deciding whether a panic is affordable, and they are seven times cheaper than Go. The reason is not cleverness, it is that there is less to do. Go's panic allocates a record, walks the stack through the runtime's own unwinder, and builds the information a traceback needs whether or not anybody will print one. burrow walks a linked list of open scopes, calls what is on them, copies thirty two bytes and jumps. These numbers were expected to move when the stack walker landed, and they did not, because the walk happens on the way out rather than on the way in: a panic that somebody catches never collects a trace and a panic that ends the process collects one once, when there is nothing left to be fast for.

The stack walker is measured in its own two rows and they are in [results/server2-2026-09-21-callers.txt](results/server2-2026-09-21-callers.txt). `callers_ten` walks ten frames of recursion and everything under it in 73.82 nanoseconds against Go's 1246.00, and `callers_two`, which is the same stack with room for two frames, is 46.81 against 234.00. Seventeen times and five times, and neither of those is a fair fight in burrow's favour: Go consults an inline table for every frame so that one physical frame can report as several logical ones, and burrow has no such table to consult, so it reports fewer frames for less work. The gap will narrow when the symbol table lands and burrow starts doing some of what Go is doing here.

What is worth taking from those rows is the fixed cost rather than the ratio. Forty seven nanoseconds to get two frames is most of a walk, so a logger asking for its caller pays about that, and each further frame is a few nanoseconds after it. That is cheap enough to do on an error path and not cheap enough to do in a loop.

This group also produced the largest single fix in the repository so far, and it came from one row looking wrong. The walk has to know where the thread's stack ends, which means asking the system, and asking the system on Linux reads `/proc/self/maps`. Measured with that call on every walk, `callers_two` was 50794.25 nanoseconds. Cached once per thread, which is correct because a thread is given its stack before it runs anything and keeps it until it exits, it is 46.81. Three orders of magnitude, for a value that cannot change.

`panic_try_empty` is 17.25 against Go's 6.10 and it is the row burrow loses, which is the right way round. Go pays for recovery in the defer and burrow pays for it in the block, so Go's cost lands on every function with a defer in it and burrow's lands only where somebody catches something. About ten of those seventeen nanoseconds are the `setjmp` itself and the rest is the same goroutine lookup `defer_scope_empty` pays, which is the performance item already open against defer and which will move both rows when it is done.

The thing this group actually changed is not in the table, because it was fixed before the first result file was written. The first macOS run had `panic_try_empty` at 117.52 nanoseconds, against 8.01 for the same block today. C says nothing about whether `setjmp` saves the signal mask and every libc answers differently: glibc does not, so `setjmp` there is a dozen stores, and macOS and the BSDs do, so `setjmp` there is a `sigprocmask` and a `sigprocmask` is a system call. A five line microbenchmark put macOS `setjmp` at 139.90 nanoseconds against 2.52 for `_setjmp`, which is the POSIX pair that never touches the mask. burrow uses that pair on the systems that save the mask, which is macOS and the BSDs, since a panic does not run in a signal handler and has no mask to put back. Everywhere else keeps plain `setjmp`, and not out of timidity: glibc declares `_setjmp` under a strict C11 compiler and does not declare `_longjmp`, so asking for the pair on Linux is a build failure in exchange for nothing, because there the two names are the same code. Fifty five times on one of three platforms, and nothing about the feature on the other two would have hinted at it.

The mutex rows are the closest pairing in the repository, because burrow's `sync.Mutex` is a port of Go's rather than a lock that behaves like it. Same state word, same spin budget, same millisecond before starvation mode, same handoff. So a gap on one of these rows is a gap in the port and not a difference of design, which makes them unusually easy to read.

Pinned, on server2, [results/server2-2026-09-21-sync-mutex.txt](results/server2-2026-09-21-sync-mutex.txt) has the uncontended rows level with Go within the noise: `mutex_lock_unlock` at 11.28 nanoseconds against 11.37, `mutex_try_lock_unlock` at 11.12 against 10.81, `mutex_locker_lock_unlock` at 11.03 against 11.39, `rw_mutex_r_lock_unlock` at 11.41 against 11.49, and `rw_mutex_lock_unlock` at 24.46 against 23.47. The write lock costing twice the read lock is not a surprise, since it takes the inner mutex first and then announces itself to the readers.

Those rows are level because of a change the benchmark asked for. The first measurement had `mutex_lock_unlock` at 1.20x Go and `mutex_locker_lock_unlock` behind it as well, and the reason was structural rather than algorithmic: Go's compiler inlines the fast path of `Lock` into the caller, and burrow's lived in `libburrow.a` behind a function call. A lock nobody holds is one instruction, and putting a call around one instruction roughly doubles it. The fast paths are `static inline` in `burrow/sync.h` now with everything past the first compare and swap still out of line, which is the shape `sync/atomic.h` already used and the shape Go's own mutex has. The ratio went from 1.20x to 0.99x and nothing else moved.

The contended rows are in [results/server2-2026-09-21-sync-mutex-contended.txt](results/server2-2026-09-21-sync-mutex-contended.txt) and they are deliberately not pinned, because four goroutines pinned to one core are not four goroutines contending, they are four goroutines taking turns. Unpinned, `mutex_contended` is 23.94 nanoseconds against Go's 27.56 and `rw_mutex_contended_read` is 21.02 against 23.15. Both spreads are above twenty percent, which is what a contention benchmark looks like on a shared machine, so read those two as level rather than as a win.

Running them pinned found something anyway. With four Ps on one core burrow was 1.74x Go, and the reason was that `burrow__thread_ncpu` asked `sysconf` how many processors the machine has rather than asking how many this process may use. Under `taskset`, and inside a container with a cpuset, those are different numbers. burrow was reading six, concluding there was somewhere else for the lock holder to be running, and spinning on the one core it shared with it. It reads the affinity mask now, the same as Go does, and the row came back to where the unpinned one already was. The bug was worth more than the row: it also meant `GOMAXPROCS` defaulted to a thread per core on a machine where two cores were allowed, which is the single most common way to deploy a container.

`WaitGroup` and the `Once` family landed next and they are the same kind of pairing, since both are ports rather than lookalikes. On server2, [results/server2-2026-09-21-waitgroup.txt](results/server2-2026-09-21-waitgroup.txt) has `wait_group_add_done` at 12.77 nanoseconds against Go's 14.34 and `wait_group_wait_empty` at 2.84 against 3.73. Those two are the counter and nothing else, and level is the answer the port wants. `wait_group_go_wait`, four goroutines started through `Go` and waited for, is 880.48 against 2183.00, and that row is mostly the scheduler rather than the group: it is four goroutine starts, four exits, a park and an unpark, and burrow's goroutines are cheaper than Go's on every row that has measured them so far.

[results/server2-2026-09-21-once.txt](results/server2-2026-09-21-once.txt) has `once_func_call` at 2.70 against 5.34 and `once_value_get` at 4.06 against 5.22, and the reason is the shape rather than the speed. Go's `OnceFunc` and `OnceValue` return closures, so calling one is an indirect call through a function value that the collector owns. burrow's are structs the caller declares, with the state inline and no allocation anywhere, so the call is direct and the fast path is a load and a branch. That is a real difference and it is also a fair one to point out in both directions: Go's version composes without the caller having to find somewhere to put the state, and burrow's does not.

`once_do` is 0.91 against 0.47, which is the one row here that looks like a loss and is not. Both sides are a single load, a test and a branch. The loop gcc builds around burrow's has two taken branches per iteration and most cores retire one taken branch a cycle, which accounts for the whole gap on a body that is otherwise one instruction. A row where the loop is larger than the thing being measured is a row to read as "free on both sides", and that is what it says.

`Cond` is the clearest split between the two halves of a benchmark so far. In [results/server2-2026-09-21-cond.txt](results/server2-2026-09-21-cond.txt) the two empty rows are level, `cond_signal_empty` at 4.06 nanoseconds against 4.21 and `cond_broadcast_empty` at 4.16 against 4.52, which is what two atomic loads and a compare cost and is the same code on both sides. `cond_ping_pong` is 268.20 against 590.20, and none of that is the Cond. Each iteration is two handoffs, a park and an unpark each way, so what the row measures is the scheduler with a condition variable wrapped round it, and burrow's goroutines being cheaper to park and start again is the same result `wait_group_go_wait` and `note_pingpong` give.

`sync.Map` is the fourth case where a benchmark changed the library, and this one was changed by a row that exists only to answer a question. [results/server2-2026-09-22-sync-map.txt](results/server2-2026-09-22-sync-map.txt) is the table. Both sides are the same structure, a sixteen way hash trie with entries chained at the bottom, because Go's `sync.Map` has been `internal/sync.HashTrieMap` since 1.24 and burrow's is a port of it. The Go side pre-boxes its keys and values into `any` before the timer starts, so what these rows compare is the map rather than Go's interface conversion.

`sync_map_load_hit` is 40.38 nanoseconds against Go's 32.82 and `sync_map_load_miss` is 26.80 against 25.81. Those two read together: a miss is level and a hit is not, so whatever costs the difference happens after the key has been found, and the trie walk itself is fine. `sync_map_has_hit` is the row that says which. It does the same lookup and passes a null destination so the value is never copied, and it is 39.05, against 46.84 for the hit row as first measured. Seven nanoseconds, in a `memcpy` of eight bytes.

The reason a `memcpy` of eight bytes costs seven nanoseconds is that nothing on the path can see that it is eight. A value is copied through the type descriptor, which is a call into the runtime that ends in a `memcpy` whose size is a field load, so the compiler emits a call to `memcpy` and `memcpy` branches on a size it learns at runtime. `map.c` dispatches the sizes that actually turn up now, one, two, four, eight and sixteen bytes, and lets the compiler emit a load and a store for them. The hit row went from 46.84 to 40.38 and nothing else moved.

The attempt before that one is worth recording because it failed. The same argument applies to hashing and to equality, which are also calls through the descriptor, so both were inlined the same way. It measured at 50.66 against 50.87, which is inside the noise, and it was reverted rather than shipped. The difference between the two attempts is that a hash of an integer is already a handful of instructions behind a call the branch predictor has seen a million times, and a `memcpy` with a runtime size is not.

What is left on the read side is mostly the reclamation pin. burrow has no collector, so a reader announces itself before it starts following pointers that a writer may be unlinking, and that announcement is one sequentially consistent store and about eight nanoseconds. Go pays nothing there and gets the same safety from the collector, which accounts for most of the remaining gap on both lookup rows. There is a known way to get it back, an asymmetric pin where the reader writes with a plain store and the writer forces the barrier with `membarrier(2)` on Linux and `FlushProcessWriteBuffers` on Windows, which would take eight nanoseconds to about one. It is not done.

`sync_map_store_delete_pair` is 186.44 against 177.40 and `sync_map_store_update` is 136.97 against 112.60, so the write side is close without being level, and `sync_map_range` is 17.7 microseconds against 12.9 for a thousand entries. The loudest row is `sync_map_load_or_store_hit` at 52.14 against 36.47. That one is not a missing fast path, since the insert walk returns before it takes any lock when the key is already there, exactly as Go's does, and both sides check whether the map has been started on the way in. It has not been isolated the way the lookup row was, and it is the next thing here to put a row against.

`sync.Pool` is the first thing measured here where the two sides are the same structure and the difference is a lookup neither structure contains. [results/server2-2026-09-22-sync-pool.txt](results/server2-2026-09-22-sync-pool.txt) is the table. `sync_pool_get_put` is 21.53 nanoseconds against Go's 16.97, `sync_pool_get_put_pair` is 55.79 against 48.82 and `sync_pool_get_new` is 24.66 against 21.32. Three rows, three different paths through the pool, and the same four and a half nanoseconds on each of them.

A gap that does not move when the path changes is not in the path. What all three have in common is that a Get and a Put each have to work out which P they are on, and burrow does that by reading a thread local, where Go reads a field off the goroutine pointer it already has in a register. A temporary row that timed nothing but the thread local read put it at 3.7 nanoseconds on the development machine, against 6.9 for a whole uncontended mutex lock and unlock on the same machine, and there are two of those reads in each iteration of every row above. It is the same read the reclamation pin makes on every `sync.Map` lookup, so it looked like one fix in the runtime rather than three fixes in three packages.

The fix was written and it measured at nothing, which is the second time that has happened here and it is worth recording for the same reason the first one was. The thread local was made visible in the header and the accessor became a `static inline`, so every caller reads the variable itself instead of calling into `sched.c` for it. On the machine these numbers come from, `sync_pool_get_put` went from 21.53 to 21.45, which is not a change. A modern core runs a dependent load off a segment register while it is doing something else, and the pool's fast path has enough else to do that two of them disappear into it. The development machine showed a large change in both directions across runs, which is what a machine with a hundred percent spread does and is why it is not the machine the numbers come from. The change was reverted. The gap is real and the explanation for it is still the best one available, but the first thing tried did not close it.

`sync_pool_sweep_idle` has no Go column because Go has no such call. It is here because the design raises the question: burrow drains pools on the system monitor's timer rather than at a garbage collection, which means a timer that fires in every burrow program whether it has a pool or not. It is 15.63 nanoseconds for one pool, which is a lock, a walk of an empty generation and two pointer swaps, so a program with a hundred pools spends about a microsecond and a half per second on it. That is the answer to the question and it is the end of it.

There is no steal row. A steal is a Get whose own P has nothing and takes from the far end of another P's ring, and timing one needs a second P with something in its ring at the moment the first one looks. The runner pins the process to one CPU so the numbers hold still, which makes that arrangement impossible to set up honestly here. It is the path that matters least of the three anyway, since a pool that steals often is a pool being used across Ps in a way that Go's design is explicitly not optimised for either.

`context` is the first package here that is mostly other packages. A `WithCancel` is a node, a channel, a lock and a list, a `WithTimeout` adds a timer to that, and a cancel is a channel close per node in the subtree, so these rows are a reading of the runtime underneath as much as of the package itself. [results/server2-2026-09-22-context.txt](results/server2-2026-09-22-context.txt) is the table.

The rows that were the reason to write the package the way it is written came out the way they were supposed to. `context_cancel_tree` is 12.7 microseconds against Go's 32.0, a ratio of 0.40x, which is sixty four children attached, cancelled and freed, and it is the intrusive list against Go's `map[canceler]struct{}`. `context_with_cancel_nested` is 270.64 nanoseconds against 395.20, and the unnested row next to it is level at 0.99x, so the difference between those two rows is attaching and detaching and that is where the list wins. Go cannot make the same choice, because a `canceler` is an interface value and there is nowhere to put the links.

`context_done` is 3.62 nanoseconds against 4.69. That is the other half of the trade: burrow makes the done channel when the context is made because `context_done` has no allocator and no way to report a failure, so the read is a load where Go's is an atomic load and a branch into the code that makes one. The cost of that choice is a channel per `WithCancel` whether or not anybody waits on it, and the `context_with_cancel` row being level with Go says the channel is affordable.

Two rows are slower than Go and both have an explanation that is not about `context`.

`context_value_deep` is 122.93 nanoseconds against 49.79 and `context_value_shallow` is 16.92 against 7.34, so the per level cost is about 15 nanoseconds here against about 6 there. A lookup compares the key at every level, and burrow's comparison goes through `any_equal`, which dispatches to the type descriptor's `equal` function. Go compares two interface values with an inline compare of two words. A pointer identity fast path would close most of it, since two keys built from the same `static` descriptor and the same address are the same key, but pointer identity is not reflexive for a float key holding a NaN, so it needs a kind check in front of it rather than being dropped in. That is a `reflect` and `iface` change rather than a `context` one and it is tamnd/burrow#90.

`context_err_live` is 11.91 nanoseconds against 3.61, a ratio of 3.30x. Both sides take the node's mutex, so this row is very nearly a measurement of `SyncMutex` against `sync.Mutex` with two loads on top, and Go's uncontended path is a single compare and swap on a word with nothing else in it. That is a `sync` row wearing a `context` hat, it belongs to whoever takes the mutex fast path apart next, and it is tamnd/burrow#91.

The two deadline rows are the ones added with `WithDeadline` and `WithTimeout`, and both of them are read as a distance from a row above rather than on their own.

`context_with_timeout` is 564.01 nanoseconds against Go's 735.80. Subtract the `context_with_cancel` row from each and what is left is the deadline: about 329 nanoseconds here against about 499 there, which is a timer armed and stopped again on both sides. burrow goes through `time_after_func`, so there is a `TimeTimer` allocated here that Go does not allocate, and it is ahead anyway. The row to watch is this distance rather than the absolute number, because everything in it belongs to the timer package and will move when that does.

`context_with_timeout_expired` is 274.44 against 431.30, and the thing to check is not the ratio, it is that 274 is near the 235 of `context_with_cancel` and nowhere near the 564 above it. A deadline that has already gone by should arm no timer at all, and a row that drifts up towards the armed one is how you find out that it started arming one.

`strconv` is the first package here where the numbers come from server3 rather than server2, and they come with the loudest spreads in this directory, so read [results/server3-2026-09-23-strconv.txt](results/server3-2026-09-23-strconv.txt) for its direction and not its decimals. Each row is the minimum of fifteen pinned runs, which holds up better on a busy machine than the spread column suggests, but a gap of ten percent in either direction here is not a result.

Parsing is ahead of Go across the board. `strconv_atoi` is 20.54 nanoseconds against 26.37, and the float rows sit between 0.42x and 0.77x. `strconv_atof64_hard` is 188.54 against 55,483, which looks like a typo and is not one. The input is a subnormal with more digits than the fast path can take, Go 1.26 falls back to its big decimal for it, and burrow is a port of the unrounded scaling that replaced that fallback in Go 1.27. So that row compares two algorithms rather than two implementations of one, and it will close when the Go column moves to 1.27.

Formatting is level with Go on most rows, from 0.70x to 1.23x. Two groups are not. The fixed precision rows, `strconv_ftoa64_fixed17` at 1.92x and `strconv_ftoa64_f` at 1.82x, are the path that asks for a set number of digits rather than the shortest, and it is the one to look at first. Then there are two small appends. `strconv_append_int_small` is 43.21 against 18.89 and `strconv_append_quote_rune` is 156.16 against 20.39. Both write a few bytes into a buffer that already has room for them, so the gap is overhead around a tiny amount of work rather than the work itself, and it should come out of the library rather than out of this table.

The unquote rows are 1.52x and 1.41x. The easy row has no escapes in it, and burrow returns a view into the input for that without copying anything, the same as Go does, so the whole gap is in the scan that decides there was nothing to unescape. That scan is a byte at a time today, and it is the obvious place to start.

`strings` is from server3 as well, in [results/server3-2026-09-24-strings.txt](results/server3-2026-09-24-strings.txt), and the spreads are as loud as the strconv ones, so the same warning applies.

Building new strings is ahead of Go. `strings_builder` is 0.43x, `strings_fields` 0.46x, `strings_html_escape` 0.51x, `strings_join` 0.57x, and the two replacer rows are 0.66x and 0.71x. Searching is mostly behind. Go does substring search in assembly, sixteen or thirty two bytes a step, and burrow does it in portable C eight bytes a step, which puts `strings_index_hard1` at 1.30x and `strings_index_hard2` at 1.93x, though `strings_index_hard3` is 0.83x and `strings_last_index_hard2` is 0.52x. `strings_count_hard2` at 1.73x is the same search run once per match. `strings_equal_fold` at 2.11x has no ASCII fast path yet, and that is the cheapest of these to fix.

Everything else arrives as the packages do.

## Licence

BSD-3-Clause, matching burrow and Go. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
