#!/usr/bin/env bash
set -euo pipefail
# the mach and c checksums must agree on every kernel, whatever the timings
case "$MACH_CI_LEG" in
  x86_64-linux|x86_64-darwin)
    export MACH="$MACH_COMPILER"
    ./bench.sh --check --profile debug
    ./bench.sh --check --profile release
    ./bench.sh --check --fast-math
    ;;
esac
