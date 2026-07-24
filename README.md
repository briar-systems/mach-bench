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
  f32_saxpy           9.23 us    9.35 us   98.32 us    10.65x    10.51x    =
  u64_fnv1a         309.56 us  309.45 us  309.59 us     1.00x     1.00x    =
```

- Times are **per kernel invocation** — one full pass over that kernel's inputs.
- `vs <variant>` is `mach / C`. **Lower is better; 1.00x is parity.**
- `geomean` is the geometric mean of the ratios over the kernels shown, so it
  respects `--filter`.
- `chk` compares the C and Mach checksums: `=` bit-identical, `~` within the
  tolerance that kernel declares, `!` a real disagreement (the run prints a
  warning, and the timings should not be trusted).

## Kernels

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

`u64_fnv1a` is the honesty check. Nothing can vectorise it, so both sides should
land on top of each other. If that row drifts far from `1.00x`, suspect the
harness before believing any other row.

## Methodology

**Same ISA.** The Mach target declares `isa = "x86_64"`, so C is compiled at the
baseline x86-64 ISA (SSE2) with no `-march` flag. Comparing against
`-march=native` would hand C an instruction set the Mach build is not allowed to
emit. `--native` adds that column anyway when you want the host-ISA ceiling, but
it is not a like-for-like number.

**Same data.** Both sides generate their inputs with the same xorshift64\*
sequence, in the same draw order, from the same seed, so the arrays are
bit-identical. This is what makes the checksums comparable at all.

**Checksums, not trust.** Every kernel returns a checksum, and the runner
compares them. Inputs are exact multiples of 1/256, which keeps the integer
kernels and `f64_dot` exactly order-independent — those demand bit-identical
results. The f32 reductions cannot be exact under reassociation, so they declare
a tolerance next to the kernel rather than the runner special-casing them.

**Timing.** Each measurement is the minimum of `--reps` repetitions (default 7),
each repetition being N back-to-back invocations, after one untimed warmup pass.
The minimum is used because the fastest observed run is the one least polluted by
scheduling noise.

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
| `--reps <n>` | timed repetitions per kernel (default 7) |
| `--filter <substr>` | only report kernels whose name contains `<substr>` |
| `--native` | add a `cc -O3 -march=native` column (host ISA, not baseline) |
| `--fast-math` | add a `cc -O3 -ffast-math` column (permits FP reassociation) |
| `--asm` | also emit assembly for both sides |

`CC` and `MACH` override the compilers used.

`--fast-math` is the interesting one for reductions: C will not vectorise an FP
reduction without it, because doing so changes the result. If Mach reassociates
by default, then the honest comparison for `f64_dot` and `f32_norm` is against
the `cc fast` column, not `cc -O3`.

`--asm` writes C assembly to `out/c/kernels-<variant>.s` and Mach assembly to
`out/<target>/<profile>/asm/bench/kernels.s`. The two are different dialects, so
they are for eyeballing whether vector instructions appear, not for
instruction-by-instruction diffing.

## Adding a kernel

Kernels are registered in a table on each side; the runner has no per-kernel
knowledge and needs no changes.

1. Write the kernel in `c/kernels.c` and mirror it statement-for-statement in
   `src/kernels.mach`.
2. Add an entry to both registries with the **same name, same `iters`, same
   `tol`**. Bump `REGISTRY_LEN` in `src/kernels.mach` (the C side sizes itself).
3. If it needs new input arrays, declare them in `c/bench.h` + `c/kernels.c` and
   `src/data.mach`, and extend both `init` routines **in the same draw order** —
   otherwise the two sides see different data and every checksum breaks.
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
src/main.mach     entry point
out/              build output and captured runs (not tracked)
```
