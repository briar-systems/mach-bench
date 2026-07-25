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
particle parts[N_PART];
int32_t chase[N_SMALL];
int32_t sortbuf[N_SORT];
int32_t sorted_a[N_SMALL];
uint8_t needle[NEEDLE_LEN];

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
    for (int i = 0; i < N_PART; i++) {
        parts[i].x = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        parts[i].y = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        parts[i].z = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        parts[i].vx = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        parts[i].vy = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
        parts[i].vz = (float)(bench_rand(&s) & 0xFF) * (1.0f / 256.0f);
    }

    // sorted_a is strictly ascending, so binary search needs no sort at init
    for (int i = 0; i < N_SMALL; i++) sorted_a[i] = 2 * i;

    // build a single Hamiltonian cycle over [0, N_SMALL): Fisher-Yates a
    // permutation, then link each element to its successor. chase[] is walked
    // one dependent load at a time, so no prefetcher can run ahead of it.
    // i64_out doubles as the permutation scratch -- it is output-only elsewhere.
    for (int i = 0; i < N_SMALL; i++) i64_out[i] = i;
    for (int i = N_SMALL - 1; i > 0; i--) {
        int j = (int)(bench_rand(&s) % (uint64_t)(i + 1));
        int64_t t = i64_out[i];
        i64_out[i] = i64_out[j];
        i64_out[j] = t;
    }
    for (int i = 0; i < N_SMALL; i++) {
        chase[(int)i64_out[i]] = (int32_t)i64_out[(i + 1) % N_SMALL];
    }

    for (int i = 0; i < NEEDLE_LEN; i++) needle[i] = u8_a[NEEDLE_AT + i];
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

// --- general codegen ---

typedef int32_t (*step_fn)(int32_t);
static int32_t step_add(int32_t v) { return v + 1; }
static int32_t step_mul(int32_t v) { return v * 2; }
static step_fn steps[2] = {step_add, step_mul};

// indirect call through a data-dependent function pointer. neither compiler can
// devirtualise it, so this is call overhead with inlining taken off the table
static double k_call_indirect(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        acc += steps[i32_a[i] & 1](i32_a[i] & 0xFF);
    }
    return (double)acc;
}

// array-of-structures traversal: six f32 fields at a 24-byte stride. the f64
// accumulator keeps the result exact, so this stays a bit-exact comparison
static double k_rec_particle(void) {
    const float dt = 0.00390625f;
    double acc = 0.0;
    for (int i = 0; i < N_PART; i++) {
        acc += (double)(parts[i].x + parts[i].vx * dt)
             + (double)(parts[i].y + parts[i].vy * dt)
             + (double)(parts[i].z + parts[i].vz * dt);
    }
    return acc;
}

// pointer chasing over a random Hamiltonian cycle. every load depends on the
// previous one, so this is memory latency with prefetching defeated
static double k_list_walk(void) {
    int32_t p = 0;
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        p = chase[p];
        acc += p;
    }
    return (double)acc;
}

// branchy dispatch over an unpredictable opcode stream. Mach has no switch, so
// both sides use an if/else chain and the comparison stays like-for-like
static double k_branch_dispatch(void) {
    int64_t acc = 0;
    int64_t reg = 1;
    for (int i = 0; i < N_BYTES; i++) {
        int op = u8_a[i] & 3;
        if (op == 0) {
            reg += 3;
        } else if (op == 1) {
            reg ^= 0x5A;
        } else if (op == 2) {
            reg = (reg >> 1) + 1;
        } else {
            acc += reg & 0xFF;
        }
    }
    return (double)acc;
}

// insertion sort over independent blocks -- compare, branch and shift heavy,
// and refilled each call so it stays idempotent
static double k_sort_blocks(void) {
    for (int i = 0; i < N_SORT; i++) sortbuf[i] = i32_a[i];
    for (int b = 0; b < N_SORT; b += SORT_BLOCK) {
        for (int i = b + 1; i < b + SORT_BLOCK; i++) {
            int32_t v = sortbuf[i];
            int j = i - 1;
            while (j >= b && sortbuf[j] > v) {
                sortbuf[j + 1] = sortbuf[j];
                j--;
            }
            sortbuf[j + 1] = v;
        }
    }
    // position-weighted so a wrong ordering cannot checksum equal
    int64_t acc = 0;
    for (int i = 0; i < N_SORT; i++) acc += (int64_t)(i + 1) * sortbuf[i];
    return (double)acc;
}

static int64_t fib(int64_t n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

// naive recursion: call overhead, stack traffic, and whatever the optimiser
// does with a self-recursive tree
static double k_recurse_fib(void) {
    return (double)fib(FIB_N);
}

// naive substring search -- nested loop with an early exit
static double k_str_search(void) {
    int64_t found = -1;
    for (int i = 0; i + NEEDLE_LEN <= N_BYTES; i++) {
        int k = 0;
        while (k < NEEDLE_LEN && u8_a[i + k] == needle[k]) k++;
        if (k == NEEDLE_LEN) {
            found = i;
            break;
        }
    }
    return (double)found;
}

// binary search: unpredictable branches over a working set larger than L2
static double k_binsearch(void) {
    int64_t acc = 0;
    for (int q = 0; q < N_QUERY; q++) {
        int32_t key = (i32_a[q] * 17 + q * 3) % (2 * N_SMALL);
        int lo = 0, hi = N_SMALL - 1, res = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (sorted_a[mid] == key) {
                res = mid;
                break;
            }
            if (sorted_a[mid] < key) lo = mid + 1;
            else hi = mid - 1;
        }
        acc += res;
    }
    return (double)acc;
}

// C-idiomatic error handling: an int status plus an out-parameter, checked
// every iteration on a path that never fails. the Mach side uses Result[i64,
// str], so this measures each language's ordinary error-propagation idiom
static int step_checked(int32_t v, int64_t *out) {
    if (v < 0) return 1;
    *out = (int64_t)v * 3 + 1;
    return 0;
}

static double k_result_chain(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        int64_t v;
        if (step_checked(i32_a[i], &v) != 0) return -1.0;
        acc += v;
    }
    return (double)acc;
}

const kernel kernels[] = {
    {"f32_saxpy", k_f32_saxpy, 100, 0.0, "array loops"},
    {"f64_dot", k_f64_dot, 100, 0.0, "array loops"},
    {"f32_norm", k_f32_norm, 100, 1e-3, "array loops"},
    {"i32_madd", k_i32_madd, 100, 0.0, "array loops"},
    {"u8_count", k_u8_count, 50, 0.0, "array loops"},
    {"u64_fnv1a", k_u64_fnv1a, 10, 0.0, "array loops"},
    {"i32_filter_sum", k_i32_filter_sum, 100, 0.0, "array loops"},
    {"i64_prefix_sum", k_i64_prefix_sum, 100, 0.0, "array loops"},
    {"f32_matmul", k_f32_matmul, 10, 1e-4, "array loops"},
    {"stream_triad", k_stream_triad, 5, 0.0, "array loops"},

    {"call_indirect", k_call_indirect, 50, 0.0, "general codegen"},
    {"rec_particle", k_rec_particle, 50, 0.0, "general codegen"},
    {"list_walk", k_list_walk, 20, 0.0, "general codegen"},
    {"branch_dispatch", k_branch_dispatch, 10, 0.0, "general codegen"},
    {"sort_blocks", k_sort_blocks, 20, 0.0, "general codegen"},
    {"recurse_fib", k_recurse_fib, 5, 0.0, "general codegen"},
    {"str_search", k_str_search, 20, 0.0, "general codegen"},
    {"binsearch", k_binsearch, 20, 0.0, "general codegen"},
    {"result_chain", k_result_chain, 100, 0.0, "general codegen"},
};

const int kernel_count = (int)(sizeof(kernels) / sizeof(kernels[0]));
