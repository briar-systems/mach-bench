// C reference harness
//
// runs every registered kernel and writes one machine-readable line per kernel:
//
//     <name>\t<best_ns>\t<iters>\t<checksum>
//
// best_ns is the fastest of `reps` timed repetitions, each repetition being
// `iters` back-to-back kernel invocations. minimum rather than mean, because
// the fastest observed run is the one least polluted by scheduling noise.
//
// the Mach harness in src/harness.mach emits the identical format.

// clock_gettime/CLOCK_MONOTONIC are POSIX, not ISO C; declare the level we need
// here so the build line stays a plain -std=c11
#define _POSIX_C_SOURCE 200809L

#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_REPS 7

// keeps the optimiser from discarding kernel results it can prove unused
static volatile double sink;

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

static long long time_kernel(kernel_fn fn, int iters, double *checksum) {
    double acc = 0.0;
    double last = 0.0;
    long long t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        last = fn();
        acc += last;
    }
    long long elapsed = now_ns() - t0;
    // acc only exists to stop the optimiser discarding the calls; the reported
    // checksum is the kernel's own return value, exact rather than averaged
    sink = acc;
    *checksum = last;
    return elapsed;
}

// optional first positional argument: timed repetitions per kernel.
// src/main.mach takes the same argument in the same position.
int main(int argc, char **argv) {
    int reps = DEFAULT_REPS;
    if (argc > 1) {
        int n = atoi(argv[1]);
        if (n >= 1) reps = n;
    }

    bench_data_init();

    for (int k = 0; k < kernel_count; k++) {
        double checksum = 0.0;
        long long best = 0;

        // one untimed warmup pass to fault in pages and settle the caches
        time_kernel(kernels[k].fn, kernels[k].iters, &checksum);

        for (int r = 0; r < reps; r++) {
            long long ns = time_kernel(kernels[k].fn, kernels[k].iters, &checksum);
            if (r == 0 || ns < best) best = ns;
        }

        printf("%s\t%lld\t%d\t%.17g\t%g\t%s\n", kernels[k].name, best, kernels[k].iters, checksum,
               kernels[k].tol, kernels[k].group);
    }

    return 0;
}
