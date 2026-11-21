# mach-bench

A small, maintained benchmark comparing [Mach](https://github.com/briar-systems/mach)
against C on the same kernels, the same data, and the same instruction set.

The question it answers is narrow on purpose: **for a given loop, how far is Mach
from what a mature C compiler produces?** It is a codegen yardstick, not a
general-purpose benchmark suite.

```sh
mach dep pull .   # once, after cloning
./bench.sh
```

That builds both sides, runs every kernel, and prints one table. It needs `mach`
and a C compiler on `PATH`; nothing else.

## Reading the table

The rows below are a **format illustration**, not a published result — run the
suite to get numbers for your machine and toolchain.

```
  kernel               cc -O2     cc -O3       mach     vs O2     vs O3  chk
  ----------------------------------------------------------------------------
  array loops
    f32_saxpy         9.23 us    9.35 us   98.32 us    10.65x    10.51x    =
    u64_fnv1a       309.56 us  309.45 us  309.59 us     1.00x     1.00x    =
    geomean                                             5.79x     5.84x
```

- Times are **per kernel invocation** — one full pass over that kernel's inputs.
- `vs <variant>` is `mach / C`. **Lower is better; 1.00x is parity.**
- Each group gets its own `geomean`, with `geomean (like)` at the foot. All of
  them are geometric means over the kernels actually shown, so they respect
  `--filter`. `geomean (like)` covers only the rows that are the same statements
  on both sides. The `idioms` group is left out of it (see
  [Basis](#basis)).
- `chk` compares the C and Mach checksums: `=` bit-identical, `~` within the
  tolerance that kernel declares, `!` a real disagreement (the run prints a
  warning, and the timings should not be trusted).

## Kernels

Kernels are reported in three groups, each with its own geometric mean.

**array loops** — tight numeric loops over flat arrays. This is the group that
moves when vectorisation, unrolling, or address-generation changes.

| kernel | shape | what it probes |
| --- | --- | --- |
| `f32_saxpy` | elementwise f32 | the textbook autovectorisation target |
| `f64_dot` | f64 reduction | reduction vectorisation, which requires reassociation |
| `f32_norm` | f32 reduction | the same at f32 width, where reassociation is visible in the result |
| `i32_madd` | i32 reduction | integer SIMD |
| `u8_count` | u8 compare-and-count | byte-width packed compare |
| `u64_fnv1a` | serial u64 chain | **control**: a true dependency chain must stay scalar |
| `i32_filter_sum` | data-dependent branch | predication versus a mispredicting branch |
| `i64_prefix_sum` | loop-carried dependency | scan-shaped dependency |
| `f32_matmul` | 96×96 nested loops | inner-loop vectorisation and hoisting |
| `stream_triad` | 24 MB streaming | memory bandwidth; should converge toward parity |
| `f32_stencil` | three-point stencil | vectorisation with the index offset by one either way |
| `u8_to_f32` | bytes to scaled floats | unsigned to float at mixed lane widths |
| `u8_sum` | bytes summed into an `i64` | the plain widening reduction |
| `bulk_copy` | byte copy, 256 KB | what each compiler makes of a copy loop |
| `bulk_fill` | byte fill, 256 KB | what each compiler makes of a fill loop |
| `copy_short` | 4096 copies of 8 to 39 bytes | short copies whose length comes from the data |
| `limb_add` | multi-limb `u64` add | carry written as compares |
| `hash_mix` | serial mixer | rotates written as shift pairs |

**general codegen** — everything a program does that is not a numeric array
loop: calls, records, branches, recursion, and error handling.

| kernel | shape | what it probes |
| --- | --- | --- |
| `call_indirect` | indirect call | call overhead with inlining taken off the table |
| `rec_particle` | AoS record walk | field access at a 24-byte stride |
| `list_walk` | pointer chase | memory latency with prefetching defeated |
| `branch_dispatch` | if/else chain | unpredictable branches |
| `sort_blocks` | insertion sort | compare, branch and shift heavy |
| `recurse_fib` | naive recursion | call overhead and stack traffic |
| `str_search` | substring search | nested loop with an early exit |
| `binsearch` | binary search | unpredictable branches over a large working set |
| `call_direct` | direct call, never inlined | call overhead with nothing to rewrite |
| `rec_return` | 40-byte record by value, never inlined | what an aggregate costs to move across a call |
| `rec_update` | AoS record update | field stores at a 36-byte stride |
| `op_dispatch` | sixteen-way if/else chain | a long chain over an unpredictable opcode stream |
| `parse_ints` | decimal integers from bytes | short data-dependent loops and byte compares |
| `tree_sum` | recursive tree walk | recursion whose shape comes from the data |

**idioms** — rows where the two languages do not share one way of writing the
thing, so each side uses its own. They are reported, and kept out of the overall
geomean.

| kernel | Mach | C |
| --- | --- | --- |
| `result_chain` | `res[i64, StepError]`, inlinable | `int` status plus out-parameter, inlinable |
| `result_wide` | `res[i64, Fault]` with a three-word error, never inlined | status plus two out-parameters, never inlined |
| `tag_dispatch` | an array of a `tag` read with `sel` | a tagged union read with `switch` |
| `vec_saxpy` | `f32x4` over array windows | GCC vector extension over unaligned loads |

`u64_fnv1a` is the honesty check. Nothing can vectorise it, so both sides should
land on top of each other. If that row drifts far from `1.00x`, suspect the
harness before believing any other row.

`call_indirect` goes through a data-dependent function pointer so GCC does not
devirtualise it. `branch_dispatch` and `op_dispatch` use an `if`/`else` chain on
both sides because Mach has no `switch`. `call_direct`, `rec_return` and
`result_wide` mark their callees never-inline on both sides
(`#[inline("never")]` and `__attribute__((noinline))`), so the call is a real
one.

`recurse_fib` and `tree_sum` measure a recursion rewrite GCC has and Mach does
not: GCC turns one of the two recursive calls into a loop with an accumulator
and inlines the other several levels deep. Nearly all of those two rows is that
rewrite, not codegen distance. Building the C side with
`-fno-inline -fno-optimize-sibling-calls` turns it off, and on one machine
(GCC 16.2.1, mach 7.0.0, a Ryzen 7 5800X3D) `tree_sum` then reads about 1.10x
and `recurse_fib` about 1.22x.

### Basis

Every kernel states what its row compares, as `like` or `idiom`, and both
harnesses emit it.

A `like` kernel is the same statements on both sides, so its ratio is codegen
distance and nothing else. An `idiom` kernel is each language's ordinary way of
writing the thing where the two do not share one: Mach's `res` against a status
code, a `tag` against a tagged union, a vector type against a compiler
extension. Its ratio mixes codegen with what the idiom itself costs, which is
worth seeing and is not the same quantity. So idiom rows get a group and a
geomean of their own, and `geomean (like)` is taken over the `like` rows only.

## Methodology

**Same ISA.** The Mach target is `arch = "x86_64"` and names no `isa`, which is
the baseline, so C is compiled at the baseline x86-64 ISA (SSE2) with no `-march`
flag. Comparing against
`-march=native` would hand C an instruction set the Mach build is not allowed to
emit. `--native` adds that column anyway when you want the host-ISA ceiling, but
it is not a like-for-like number.

**Same data.** Both sides generate their inputs with the same xorshift64\*
sequence, in the same draw order, from the same seed, so the arrays are
bit-identical. This is what makes the checksums comparable at all. Inputs added
later are drawn after every earlier one, so adding a kernel never moves an
existing checksum.

**Checksums, not trust.** Every kernel returns a checksum, and the runner
compares them. Inputs are exact multiples of 1/256, which keeps the integer
kernels and `f64_dot` exactly order-independent — those demand bit-identical
results. The f32 reductions cannot be exact under reassociation, so they declare
a tolerance next to the kernel rather than the runner special-casing them.

**Timing.** Each measurement is the minimum of `--reps` repetitions (default 7),
each repetition being N back-to-back invocations, after one untimed warmup pass.
The minimum is used because the fastest observed run is the one least polluted by
scheduling noise.

**Interleaving.** The binaries are run round-robin for `--rounds` passes (default
3) rather than each being run to completion in turn, and the reported time is the
best across rounds. Background load drifts over the length of a run; a
block-sequential schedule charges that drift to whichever binary happened to be
running, which corrupts the *ratios* and not merely the absolute times. On a
machine under load this was worth a factor of two. If the round-to-round spread
exceeds 1.25x the run prints a note naming the worst kernel — treat the absolute
numbers from such a run as unusable and re-run idle.

**Placement.** Where a loop lands in memory moves its time: the same
instructions have measured a third apart at different starts inside one 64-byte
line. All kernels share one binary per side, so without care a kernel's time
would depend on whatever is linked ahead of it. Every kernel function, and every
function a kernel calls, therefore starts on a 64-byte boundary on both sides
(`#[align(64)]` in `src/kernels.mach`, `-falign-functions=64` for C), and adding
or changing one kernel does not move another's code.

Two things are left to the compilers on purpose, because they are part of what a
row measures. The loop's position inside its function is the compiler's own
output. The data arrays sit wherever each compiler's default layout puts them,
and adding an array can move the ones declared after it. Treat a row that
changes with no change to its own code or compiler as placement before anything
else.

**No cheating by the optimiser.** Kernels live in a separate translation unit
(C) and a separate module (Mach) from the timing loop, and the C side is built
without LTO, so neither compiler can inline through a kernel or hoist it out of
the loop. Results are accumulated into a sink so they cannot be discarded as
dead.

Note that GCC enables vectorisation at `-O2` these days, so `cc -O2` and `cc -O3`
often land close together. Both columns are kept because the gap is occasionally
informative.

## Options

| flag | effect |
| --- | --- |
| `--profile <name>` | Mach profile to build (default `release`) |
| `--reps <n>` | timed repetitions per kernel, within one binary (default 7) |
| `--rounds <n>` | interleaved passes over every binary (default 3) |
| `--filter <substr>` | only report kernels whose name contains `<substr>` |
| `--native` | add a `cc -O3 -march=native` column (host ISA, not baseline) |
| `--fast-math` | compare `cc -O3 -ffast-math` against the Mach `reassoc` profile, both permitting FP reassociation |
| `--asm` | also emit assembly for both sides |
| `--check` | correctness only: one rep and one round unless given, and exit 1 when a kernel's checksums disagree beyond its tolerance or a kernel is registered on one side only |

`CC` and `MACH` override the compilers used.

`--fast-math` is the interesting one for reductions: neither compiler will
vectorise an FP reduction without permission, because doing so changes the
result. Mach float arithmetic is exact unless a profile relaxes it, so the
`reassoc` profile states `relax = ["float-reassoc"]`, and `--fast-math` builds it
against `cc -O3 -ffast-math` in place of the usual columns.

`--asm` writes C assembly to `out/c/kernels-<variant>.s` and Mach assembly to
`out/<target>/<profile>/asm/bench/kernels.s`. The two are different dialects, so
they are for eyeballing whether vector instructions appear, not for
instruction-by-instruction diffing.

## Adding a kernel

Kernels are registered in a table on each side; the runner has no per-kernel
knowledge and needs no changes.

1. Write the kernel in `c/kernels.c` and mirror it statement-for-statement in
   `src/kernels.mach`. Mach has no `switch` and no compound assignment (`+=`);
   `else`/`else if` are spelled `or`, and `break`/`continue` are `brk`/`cnt`.
2. Add an entry to both registries with the **same name, same `iters`, same
   `tol`, same `group`, same `basis`**. Bump `REGISTRY_LEN` in
   `src/kernels.mach` (the C side sizes itself). The basis is `like` when the
   two sides are the same statements and `idiom` when each uses its own
   construct. The runner refuses a row whose basis differs between the sides.
3. If it needs new input arrays, declare them in `c/bench.h` + `c/kernels.c` and
   `src/data.mach`, and extend both `init` routines **in the same draw order**,
   after every existing draw — otherwise the two sides see different data and
   every checksum breaks.
4. Run `./bench.sh --filter <yourkernel>` and confirm `chk` is `=`.

Pick `iters` so one repetition lands in the low milliseconds. Leave `tol` at `0`
unless the kernel is an f32 reduction, where vectorising legitimately changes the
result.

## Layout

```
bench.sh          build both sides, run, render the table
c/bench.h         sizes, data declarations, kernel registry type
c/kernels.c       the kernels + deterministic data generation
c/bench.c         timing harness
src/data.mach     the same sizes, the same PRNG, the same draw order
src/kernels.mach  the kernels, mirroring c/kernels.c
src/harness.mach  timing harness, emits the same line format as bench.c
src/bin/main.mach entry point
out/              build output and captured runs (not tracked)
```

## License

MIT. See [LICENSE](LICENSE).
