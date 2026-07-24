// shared contract between the C harness and its kernels
//
// the Mach side mirrors every constant and data-generation rule in this file.
// both implementations must produce bit-identical input arrays so that the
// reported checksums are directly comparable.

#ifndef BENCH_H
#define BENCH_H

#include <stddef.h>
#include <stdint.h>

// working-set sizes, shared with src/data.mach
#define N_SMALL 65536   // 256 KB as f32 -- L2 resident
#define N_BYTES 262144  // 256 KB as u8
#define N_STREAM 2097152 // 8 MB per array, 24 MB working set across the triad
#define MAT_N 96        // 96x96 f32 matmul

// deterministic input data, generated once at startup
extern float f32_a[N_SMALL];
extern float f32_b[N_SMALL];
extern float f32_out[N_SMALL];
extern double f64_a[N_SMALL];
extern double f64_b[N_SMALL];
extern int32_t i32_a[N_SMALL];
extern int32_t i32_b[N_SMALL];
extern int64_t i64_out[N_SMALL];
extern uint8_t u8_a[N_BYTES];
extern float st_a[N_STREAM];
extern float st_b[N_STREAM];
extern float st_out[N_STREAM];
extern float mat_a[MAT_N * MAT_N];
extern float mat_b[MAT_N * MAT_N];
extern float mat_out[MAT_N * MAT_N];

// xorshift64* -- the shared PRNG; the Mach side implements the same recurrence
uint64_t bench_rand(uint64_t *state);

// fill every input array from the fixed seed
void bench_data_init(void);

// a kernel runs its loop once over its inputs and returns a checksum.
// checksums are f64 so one comparison rule covers both integer and float
// kernels; integer kernels keep their value under 2^53 so it stays exact.
typedef double (*kernel_fn)(void);

// tol is the relative difference permitted between the C and Mach checksums.
// most kernels are exactly order-independent on this input data and demand a
// bit-identical result (tol 0); the exceptions are f32 reductions, where
// vectorising reassociates the sum and f32's 24-bit mantissa cannot absorb the
// difference. a kernel's tolerance is declared next to the kernel so the
// runner needs no per-kernel knowledge.
typedef struct {
    const char *name;
    kernel_fn fn;
    int iters; // inner invocations per timed rep
    double tol;
} kernel;

extern const kernel kernels[];
extern const int kernel_count;

#endif
