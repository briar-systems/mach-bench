// the benchmark kernels, in their own translation unit so the timing loop in
// bench.c cannot inline through them or hoist them out. compiled without LTO
// for the same reason -- this mirrors the Mach side, where kernels live in a
// separate module from the harness.

#include "bench.h"

float f32_a[N_SMALL];
float f32_b[N_SMALL];
float f32_out[N_SMALL];
double f64_a[N_SMALL];
double f64_b[N_SMALL];
int32_t i32_a[N_SMALL];
int32_t i32_b[N_SMALL];
int64_t i64_out[N_SMALL];
uint8_t u8_a[N_BYTES];
float st_a[N_STREAM];
float st_b[N_STREAM];
float st_out[N_STREAM];
float mat_a[MAT_N * MAT_N];
float mat_b[MAT_N * MAT_N];
float mat_out[MAT_N * MAT_N];

uint64_t bench_rand(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

void bench_data_init(void) {
    uint64_t s = 0x9E3779B97F4A7C15ULL;

    for (int i = 0; i < N_SMALL; i++) {
        // low byte scaled to [0,1) keeps float sums well inside f32 precision
        f32_a[i] = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        f32_b[i] = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        f64_a[i] = (double)(bench_rand(&s) & 0xFF) * (1.0 / 256.0);
        f64_b[i] = (double)(bench_rand(&s) & 0xFF) * (1.0 / 256.0);
        i32_a[i] = (int32_t)(bench_rand(&s) & 0xFF);
        i32_b[i] = (int32_t)(bench_rand(&s) & 0xFF);
    }
    for (int i = 0; i < N_BYTES; i++) {
        u8_a[i] = (uint8_t)(bench_rand(&s) & 0xFF);
    }
    for (int i = 0; i < N_STREAM; i++) {
        st_a[i] = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        st_b[i] = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
    }
    for (int i = 0; i < MAT_N * MAT_N; i++) {
        mat_a[i] = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        mat_b[i] = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
    }
}

// contiguous f32 multiply-add into a distinct output -- the textbook
// autovectorisation target
static double k_f32_saxpy(void) {
    const float alpha = 1.0009765625f;
    for (int i = 0; i < N_SMALL; i++) {
        f32_out[i] = alpha * f32_a[i] + f32_b[i];
    }
    return (double)f32_out[0] + (double)f32_out[N_SMALL / 2] + (double)f32_out[N_SMALL - 1];
}

// f64 reduction: vectorising this reassociates the sum, so the checksum is
// compared with a relative tolerance rather than bit-exactly
static double k_f64_dot(void) {
    double acc = 0.0;
    for (int i = 0; i < N_SMALL; i++) {
        acc += f64_a[i] * f64_b[i];
    }
    return acc;
}

// f32 sum of squares -- reduction with a single multiply per element
static double k_f32_norm(void) {
    float acc = 0.0f;
    for (int i = 0; i < N_SMALL; i++) {
        acc += f32_a[i] * f32_a[i];
    }
    return (double)acc;
}

// integer multiply-accumulate; inputs stay in [0,255] so the i64 total is
// exactly representable as f64
static double k_i32_madd(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        acc += (int64_t)i32_a[i] * (int64_t)i32_b[i];
    }
    return (double)acc;
}

// byte scan with a compare-accumulate -- vectorises to a packed compare
static double k_u8_count(void) {
    int64_t n = 0;
    for (int i = 0; i < N_BYTES; i++) {
        if (u8_a[i] > 127) n++;
    }
    return (double)n;
}

// FNV-1a over the byte array. each step depends on the previous one, so this
// is the control case: a correct vectoriser must leave it scalar
static double k_u64_fnv1a(void) {
    uint64_t h = 0xCBF29CE484222325ULL; // FNV-1a 64-bit offset basis
    for (int i = 0; i < N_BYTES; i++) {
        h ^= (uint64_t)u8_a[i];
        h *= 0x100000001B3ULL; // FNV-1a 64-bit prime
    }
    return (double)(h % 1000000007ULL);
}

// data-dependent branch -- tests whether the branch becomes a predicated
// select or stays a mispredicting jump
static double k_i32_filter_sum(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        if (i32_a[i] > i32_b[i]) acc += i32_a[i];
    }
    return (double)acc;
}

// loop-carried dependency: each output needs the previous one
static double k_i64_prefix_sum(void) {
    int64_t run = 0;
    for (int i = 0; i < N_SMALL; i++) {
        run += i32_a[i];
        i64_out[i] = run;
    }
    return (double)i64_out[N_SMALL - 1];
}

// naive ikj matmul -- the inner loop is a scalar-times-vector accumulate
static double k_f32_matmul(void) {
    for (int i = 0; i < MAT_N * MAT_N; i++) mat_out[i] = 0.0f;
    for (int i = 0; i < MAT_N; i++) {
        for (int k = 0; k < MAT_N; k++) {
            float aik = mat_a[i * MAT_N + k];
            for (int j = 0; j < MAT_N; j++) {
                mat_out[i * MAT_N + j] += aik * mat_b[k * MAT_N + j];
            }
        }
    }
    return (double)mat_out[0] + (double)mat_out[MAT_N * MAT_N - 1];
}

// STREAM triad over a 24 MB working set -- bandwidth bound, so both sides
// should converge regardless of how well the loop vectorises
static double k_stream_triad(void) {
    const float scalar = 3.0f;
    for (int i = 0; i < N_STREAM; i++) {
        st_out[i] = st_a[i] + scalar * st_b[i];
    }
    return (double)st_out[0] + (double)st_out[N_STREAM / 2] + (double)st_out[N_STREAM - 1];
}

const kernel kernels[] = {
    {"f32_saxpy", k_f32_saxpy, 100, 0.0},
    {"f64_dot", k_f64_dot, 100, 0.0},
    {"f32_norm", k_f32_norm, 100, 1e-3},
    {"i32_madd", k_i32_madd, 100, 0.0},
    {"u8_count", k_u8_count, 50, 0.0},
    {"u64_fnv1a", k_u64_fnv1a, 10, 0.0},
    {"i32_filter_sum", k_i32_filter_sum, 100, 0.0},
    {"i64_prefix_sum", k_i64_prefix_sum, 100, 0.0},
    {"f32_matmul", k_f32_matmul, 10, 1e-4},
    {"stream_triad", k_stream_triad, 5, 0.0},
};

const int kernel_count = (int)(sizeof(kernels) / sizeof(kernels[0]));
