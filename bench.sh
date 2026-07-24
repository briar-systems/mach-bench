#!/bin/sh
# build and run the Mach-vs-C benchmark suite, then print a comparison table.
#
# usage: ./bench.sh [options]
#   --profile <name>  mach profile to build (default: release)
#   --reps <n>        timed repetitions per kernel (default: 7)
#   --filter <substr> only report kernels whose name contains <substr>
#   --native          add a `cc -O3 -march=native` column (host ISA, not baseline)
#   --fast-math       add a `cc -O3 -ffast-math` column (allows FP reassociation)
#   --asm             also emit assembly for both sides, for inspecting codegen
#   --help            show this message

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/out"
CC=${CC:-cc}
MACH=${MACH:-mach}

PROFILE=release
REPS=7
FILTER=
WANT_NATIVE=0
WANT_FAST=0
WANT_ASM=0

usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case $1 in
        --profile) PROFILE=$2; shift 2 ;;
        --reps)    REPS=$2; shift 2 ;;
        --filter)  FILTER=$2; shift 2 ;;
        --native)  WANT_NATIVE=1; shift ;;
        --fast-math) WANT_FAST=1; shift ;;
        --asm)     WANT_ASM=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "bench.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

# C variants are compiled at the baseline x86-64 ISA (SSE2) to match the
# `isa` declared for the Mach target -- comparing against -march=native would
# hand C an instruction set the Mach build is not allowed to emit.
CFLAGS_COMMON="-std=c11 -Wall -Wextra"
VARIANTS="O2 O3"
flags_for() {
    case $1 in
        O2)     echo "-O2" ;;
        O3)     echo "-O3" ;;
        native) echo "-O3 -march=native" ;;
        fast)   echo "-O3 -ffast-math" ;;
    esac
}
label_for() {
    case $1 in
        O2)     echo "cc -O2" ;;
        O3)     echo "cc -O3" ;;
        native) echo "cc native" ;;
        fast)   echo "cc fast" ;;
    esac
}

[ "$WANT_NATIVE" -eq 1 ] && VARIANTS="$VARIANTS native"
[ "$WANT_FAST" -eq 1 ] && VARIANTS="$VARIANTS fast"

# dependencies are a fetched checkout pinned by mach.lock, not tracked in git
if [ ! -d "$ROOT/dep/mach-std" ]; then
    echo "bench.sh: dependencies not present -- run '$MACH dep pull $ROOT' first" >&2
    exit 1
fi

mkdir -p "$OUT/c" "$OUT/run"

# the simd mode recorded for the selected profile, shown in the header so a
# saved table always says which codegen mode produced it
SIMD=$(awk -v p="[profile.$PROFILE]" '
    $0 == p { inp = 1; next }
    /^\[/   { inp = 0 }
    inp && /^[ \t]*simd[ \t]*=/ { gsub(/.*=[ \t]*"|"[ \t]*$/, ""); print; exit }
' "$ROOT/mach.toml")
[ -n "$SIMD" ] || SIMD="?"

for v in $VARIANTS; do
    # kernels and harness stay separate translation units, and no LTO, so the
    # timing loop cannot inline through a kernel or hoist it out of the loop
    # shellcheck disable=SC2046
    $CC $CFLAGS_COMMON $(flags_for "$v") -c "$ROOT/c/kernels.c" -o "$OUT/c/kernels-$v.o"
    # shellcheck disable=SC2046
    $CC $CFLAGS_COMMON $(flags_for "$v") -c "$ROOT/c/bench.c" -o "$OUT/c/bench-$v.o"
    $CC "$OUT/c/kernels-$v.o" "$OUT/c/bench-$v.o" -o "$OUT/c/bench-$v"
done

ASM_FLAG=
[ "$WANT_ASM" -eq 1 ] && ASM_FLAG=--emit-asm
# shellcheck disable=SC2086
$MACH build "$ROOT" --profile "$PROFILE" $ASM_FLAG

if [ "$WANT_ASM" -eq 1 ]; then
    for v in $VARIANTS; do
        # shellcheck disable=SC2046
        $CC $CFLAGS_COMMON $(flags_for "$v") -S "$ROOT/c/kernels.c" -o "$OUT/c/kernels-$v.s"
    done
fi

MACH_BIN=$(find "$OUT" -type f -path "*/$PROFILE/bin/*" -name 'mach-bench' | head -1)
[ -n "$MACH_BIN" ] || { echo "bench.sh: mach binary not found under $OUT" >&2; exit 1; }

for v in $VARIANTS; do
    "$OUT/c/bench-$v" "$REPS" > "$OUT/run/$v.tsv"
done
"$MACH_BIN" "$REPS" > "$OUT/run/mach.tsv"

MACH_VER=$($MACH info 2>/dev/null | head -1)
CC_VER=$($CC --version 2>/dev/null | head -1)
HOST=$($MACH info 2>/dev/null | awk '/^host:/ {print $2}')

# shellcheck disable=SC2086
awk -v variants="$VARIANTS" -v outdir="$OUT/run" -v filter="$FILTER" \
    -v machver="$MACH_VER" -v ccver="$CC_VER" -v host="$HOST" \
    -v profile="$PROFILE" -v simd="$SIMD" -v reps="$REPS" '
function human(ns,   u) {
    if (ns < 1000)    { return sprintf("%.1f ns", ns) }
    if (ns < 1000000) { return sprintf("%.2f us", ns / 1000) }
    return sprintf("%.2f ms", ns / 1000000)
}
function rel(a, b) {
    if (a == b) return 0
    if (a == 0 || b == 0) return 1
    return (a > b ? a - b : b - a) / (a > b ? a : b)
}
function label(v) {
    if (v == "O2") return "cc -O2"
    if (v == "O3") return "cc -O3"
    if (v == "native") return "cc native"
    if (v == "fast") return "cc fast"
    return v
}
BEGIN {
    nv = split(variants, vs, " ")

    # read every C variant, then the mach run; first variant fixes row order
    for (i = 1; i <= nv; i++) {
        f = outdir "/" vs[i] ".tsv"
        while ((getline line < f) > 0) {
            split(line, a, "\t")
            per = a[2] / a[3]
            t[vs[i], a[1]] = per
            chk[vs[i], a[1]] = a[4] + 0
            if (i == 1) { order[++n] = a[1]; tol[a[1]] = a[5] + 0 }
        }
        close(f)
    }
    while ((getline line < (outdir "/mach.tsv")) > 0) {
        split(line, a, "\t")
        mt[a[1]] = a[2] / a[3]
        mchk[a[1]] = a[4] + 0
    }
    close(outdir "/mach.tsv")

    printf "\n  mach-bench  ·  %s  ·  %s  ·  %s\n", machver, ccver, host
    printf "  profile %s (simd=%s)  ·  min of %s reps  ·  C at baseline x86-64\n\n", profile, simd, reps

    w = 16
    line = sprintf("  %-*s", w, "kernel")
    for (i = 1; i <= nv; i++) line = line sprintf("%11s", label(vs[i]))
    line = line sprintf("%11s", "mach")
    for (i = 1; i <= nv; i++) line = line sprintf("%10s", "vs " vs[i])
    line = line sprintf("%5s", "chk")
    print line

    rulew = 2 + w + nv * 11 + 11 + nv * 10 + 5
    rule = ""
    for (i = 0; i < rulew; i++) rule = rule "-"
    print "  " rule

    for (r = 1; r <= n; r++) {
        k = order[r]
        if (filter != "" && index(k, filter) == 0) continue
        shown++

        line = sprintf("  %-*s", w, k)
        for (i = 1; i <= nv; i++) line = line sprintf("%11s", human(t[vs[i], k]))
        line = line sprintf("%11s", human(mt[k]))
        for (i = 1; i <= nv; i++) {
            ratio = mt[k] / t[vs[i], k]
            line = line sprintf("%10s", sprintf("%.2fx", ratio))
            gsum[vs[i]] += log(ratio); gn[vs[i]]++
        }

        # = bit-identical, ~ within the declared per-kernel tolerance, ! beyond
        worst = 0
        for (i = 1; i <= nv; i++) {
            d = rel(mchk[k], chk[vs[i], k])
            if (d > worst) worst = d
        }
        mark = (worst == 0) ? "=" : (worst <= tol[k] ? "~" : "!")
        if (mark == "!") bad++
        line = line sprintf("%5s", mark)
        print line
    }

    print "  " rule
    line = sprintf("  %-*s", w, "geomean")
    for (i = 1; i <= nv; i++) line = line sprintf("%11s", "")
    line = line sprintf("%11s", "")
    for (i = 1; i <= nv; i++)
        line = line sprintf("%10s", gn[vs[i]] ? sprintf("%.2fx", exp(gsum[vs[i]] / gn[vs[i]])) : "-")
    print line

    printf "\n  ratio = mach / C  ·  lower is better  ·  1.00x is parity\n"
    printf "  chk: = bit-identical   ~ within kernel tolerance   ! disagreement\n"
    if (shown == 0) printf "\n  no kernels matched filter \"%s\"\n", filter
    if (bad > 0) printf "\n  WARNING: %d kernel(s) disagree beyond tolerance -- results are not comparable\n", bad
    print ""
}' </dev/null

if [ "$WANT_ASM" -eq 1 ]; then
    MACH_ASM=$(CDPATH= cd -- "$(dirname -- "$MACH_BIN")/../asm/bench" && pwd)
    echo "  assembly:"
    echo "    C     $OUT/c/kernels-<variant>.s"
    echo "    mach  $MACH_ASM/kernels.s"
    echo ""
fi
