# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- An MIT `LICENSE`, copyright Briar Systems LLC, linked from the README
  (#18).

### Changed
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

[Unreleased]: https://github.com/briar-systems/mach-bench/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/briar-systems/mach-bench/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/briar-systems/mach-bench/releases/tag/v0.1.0
