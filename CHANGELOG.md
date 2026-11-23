# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-11-23

### Added
- Seventeen new kernels (`f32_stencil`, `u8_to_f32`, `u8_sum`, `bulk_copy`,
  `bulk_fill`, `copy_short`, `limb_add`, `hash_mix`, `call_direct`,
  `rec_return`, `rec_update`, `op_dispatch`, `parse_ints`, `tree_sum`,
  `result_wide`, `tag_dispatch`, `vec_saxpy`), bringing the suite to 36
  kernels (#36).
- Per-kernel `basis` (`like` vs `idiom`) in both registries, grouping idiom
  rows separately and keeping `geomean (like)` over statement-identical rows
  only (#36).

### Changed
- Requires Mach 7.0.0 or later and std 10.0.0. `mach.toml` declares
  `mach = "^7"` and `[dep.std]` declares `version = "^10.0"` pinned at
  v10.0.0, the entry moves to `src/bin/main.mach`, `bench.sh` reads Mach 7
  profile keys, and CI/CD pin `MACH_VERSION: 7.0.0` (#44).
- Aligned every kernel function and helper callee to 64 bytes on both sides
  (`#[align(64)]` in Mach, `-falign-functions=64` in C) so link placement of
  one kernel does not shift another (#38).
- Documented what `recurse_fib` and `tree_sum` measure under GCC's recursion
  rewrites (#40).
- Replaced the shared toolkit pipeline with repository-owned `ci.yml` and
  `cd.yml` workflows and enforced LF line endings via `.gitattributes`.

### Fixed
- Grouped the `rec_particle` sum in `src/kernels.mach` to match C's
  floating-point evaluation order (#34).
- Exported the `steps` function-pointer table in `c/kernels.c` and
  `src/kernels.mach` so Clang cannot devirtualize and vectorize
  `call_indirect` (#42).

## [0.3.0] - 2026-09-26

### Changed
- Requires mach 6.3.0 or later and std 9.0.0. `mach.toml` declares
  `mach = "^6.3"` and `[dep.std]` declares `version = "^9.0"` with the
  gitlink pinned at v9.0.0, the first std line that accepts mach 6. No
  source site changed shape. CI seeds mach v6.3.0 in place of the family
  pin (#30).

## [0.2.0] - 2026-09-19

### Added
- An MIT `LICENSE`, copyright Briar Systems LLC, linked from the README
  (#18).

### Changed
- Requires mach 5.9.0 or later and std 6.0.0. `[dep.std]` declares
  `version = "^6.0"` with the gitlink pinned at v6.0.0. The harness times
  kernels on the monotonic `time.instant` and `time.elapsed`, since std 5.0
  removed `time.monotonic`, `time.Clock` and `time.time_sub` (#26).
- `cd.yml` (formerly `release.yml`) serializes runs per tag with a `concurrency` group, as the
  shared release workflow now requires (#22), and carries the family's name
  for the tag-triggered workflow (#24).
- `mach.toml` declares the compiler range `mach = "^5.3"`, so mach 5.3 and
  later no longer warn on every build (#20).
- Releases run through the family's shared release workflow. A pushed `v*`
  tag checks the tag against the manifest version and the CHANGELOG, runs
  every CI leg, and publishes the GitHub release with the CHANGELOG section
  as its notes (#16).

## [0.1.1] - 2026-09-16

### Changed
- Requires mach 5.2.0 or later and std 4.0.0. `dep/std` is pinned to
  `tag/v4.0.0`, and the clock failure message now comes from
  `std.system.os.message`, which replaces the removed `io.error.message`
  (#11).

## [0.1.0] - 2026-09-16

First release.

### Added
- A Mach-vs-C codegen benchmark. Paired kernels run over identical,
  deterministically generated data at the same baseline x86-64 ISA, and
  `./bench.sh` builds both sides, runs them, and prints one comparison table.
- Ten array-loop kernels (`f32_saxpy`, `f64_dot`, `f32_norm`, `i32_madd`,
  `u8_count`, `u64_fnv1a`, `i32_filter_sum`, `i64_prefix_sum`, `f32_matmul`,
  `stream_triad`) and nine general-codegen kernels (`call_indirect`,
  `rec_particle`, `list_walk`, `branch_dispatch`, `sort_blocks`,
  `recurse_fib`, `str_search`, `binsearch`, `result_chain`), each group with
  its own geometric mean.
- Per-kernel checksums with a declared tolerance, compared on every run.
- Interleaved round-robin scheduling (`--rounds`) with a note when the
  round-to-round spread shows a busy machine.
- `--native`, `--fast-math`, `--asm` and `--filter` options.
- A `reassoc` profile, so `--fast-math` compares reassociating C against
  reassociating Mach.
- `--check` runs one rep and one round and exits 1 when a kernel's checksums
  disagree beyond its tolerance or a kernel is registered on one side only
  (#3).
- CI on the family contract in briar-systems/.github: x86_64 linux, windows
  and darwin legs over the debug, release and reassoc profiles, with
  `bench.sh --check` as the verify hook on linux and darwin. Timings are never
  judged in CI (#3).

### Changed
- Requires mach 5.1.0 or later and std 3.2.0. std is a gitlink under `dep/std`
  pinned to `tag/v3.2.0` (#1, #7).
- The release profile is the default and vectorizes (#1).

### Fixed
- `bench.sh` runs the host target's binary rather than whichever target's
  binary it found first, so a tree holding an `--all-targets` build no longer
  runs a darwin binary on linux (#3).

[Unreleased]: https://github.com/briar-systems/mach-bench/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/briar-systems/mach-bench/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/briar-systems/mach-bench/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/briar-systems/mach-bench/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/briar-systems/mach-bench/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/briar-systems/mach-bench/releases/tag/v0.1.0
