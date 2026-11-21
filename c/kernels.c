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
uint64_t limb_a[N_LIMB];
uint64_t limb_b[N_LIMB];
uint64_t limb_out[N_LIMB];
uint8_t u8_out[N_BYTES];
body bodies[N_PART];
node nodes[N_TREE];
int32_t tree_root;
item items[N_ITEM];
uint8_t text[N_TEXT];

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

    // everything below was added after the arrays above, and draws after
    // them, so their contents never move
    for (int i = 0; i < N_LIMB; i++) {
        limb_a[i] = bench_rand(&s);
        limb_b[i] = bench_rand(&s);
    }

    for (int i = 0; i < N_PART; i++) {
        bodies[i].x = parts[i].x;
        bodies[i].y = parts[i].y;
        bodies[i].z = parts[i].z;
        bodies[i].vx = parts[i].vx;
        bodies[i].vy = parts[i].vy;
        bodies[i].vz = parts[i].vz;
        bodies[i].nx = 0.0f;
        bodies[i].ny = 0.0f;
        bodies[i].nz = 0.0f;
    }

    // a complete binary tree over logical positions, each stored at a shuffled
    // index, so a walk follows links the prefetcher cannot predict.
    // i64_out is the permutation scratch again
    for (int i = 0; i < N_TREE; i++) i64_out[i] = i;
    for (int i = N_TREE - 1; i > 0; i--) {
        int j = (int)(bench_rand(&s) % (uint64_t)(i + 1));
        int64_t t = i64_out[i];
        i64_out[i] = i64_out[j];
        i64_out[j] = t;
    }
    for (int k = 0; k < N_TREE; k++) {
        int n = (int)i64_out[k];
        nodes[n].left = -1;
        nodes[n].right = -1;
        if (2 * k + 1 < N_TREE) nodes[n].left = (int32_t)i64_out[2 * k + 1];
        if (2 * k + 2 < N_TREE) nodes[n].right = (int32_t)i64_out[2 * k + 2];
        nodes[n].val = (int32_t)(bench_rand(&s) & 0xFF);
    }
    tree_root = (int32_t)i64_out[0];

    // three draws per item whatever its case, so the two sides stay in step
    for (int i = 0; i < N_ITEM; i++) {
        uint64_t kind = bench_rand(&s) & 3;
        uint64_t a = bench_rand(&s) & 0xFF;
        uint64_t b = bench_rand(&s) & 0xFF;
        if (kind == 1) {
            items[i].kind = ITEM_SMALL;
            items[i].u.small = (int32_t)a;
        } else if (kind == 2) {
            items[i].kind = ITEM_PAIR;
            items[i].u.pair.a = (int32_t)a;
            items[i].u.pair.b = (int32_t)b;
        } else if (kind == 3) {
            items[i].kind = ITEM_WIDE;
            items[i].u.wide = (int64_t)(a * 65536 + b);
        } else {
            items[i].kind = ITEM_NONE;
        }
    }

    // decimal integers of one to six digits, each followed by a space, then
    // spaces to the end
    int pos = 0;
    while (pos + 7 <= N_TEXT) {
        int len = 1 + (int)(bench_rand(&s) % 6);
        for (int j = 0; j < len; j++) {
            text[pos] = (uint8_t)(48 + bench_rand(&s) % 10);
            pos++;
        }
        text[pos] = 32;
        pos++;
    }
    while (pos < N_TEXT) {
        text[pos] = 32;
        pos++;
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

// three-point stencil: the same loop as saxpy with the index offset by one
// either way, which is what a vectoriser has to see through
static double k_f32_stencil(void) {
    for (int i = 1; i < N_SMALL - 1; i++) {
        f32_out[i] = (f32_a[i - 1] + f32_a[i]) + f32_a[i + 1];
    }
    return (double)f32_out[1] + (double)f32_out[N_SMALL / 2] + (double)f32_out[N_SMALL - 2];
}

// bytes widened to float and scaled -- unsigned to float at mixed lane widths
static double k_u8_to_f32(void) {
    for (int i = 0; i < N_SMALL; i++) {
        f32_out[i] = (float)u8_a[i] * (1.0f / 256.0f);
    }
    return (double)f32_out[0] + (double)f32_out[N_SMALL / 2] + (double)f32_out[N_SMALL - 1];
}

// bytes summed into a 64-bit total -- the plain widening reduction
static double k_u8_sum(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_BYTES; i++) {
        acc += u8_a[i];
    }
    return (double)acc;
}

// a byte copy written as a loop -- whatever each compiler makes of it
static double k_bulk_copy(void) {
    for (int i = 0; i < N_BYTES; i++) {
        u8_out[i] = u8_a[i];
    }
    return (double)u8_out[0] + (double)u8_out[N_BYTES / 2] + (double)u8_out[N_BYTES - 1];
}

// a byte fill written as a loop
static double k_bulk_fill(void) {
    for (int i = 0; i < N_BYTES; i++) {
        u8_out[i] = 0x5A;
    }
    return (double)u8_out[0] + (double)u8_out[N_BYTES / 2] + (double)u8_out[N_BYTES - 1];
}

// many short copies whose length comes from the data, 8 to 39 bytes each --
// the case where a bulk copy's setup cost shows
static double k_copy_short(void) {
    int64_t total = 0;
    for (int r = 0; r < N_BYTES / 64; r++) {
        int n = 8 + (u8_a[r] & 31);
        const uint8_t *src = &u8_a[r * 64];
        uint8_t *dst = &u8_out[r * 64];
        for (int j = 0; j < n; j++) {
            dst[j] = src[j];
        }
        total += n;
    }
    return (double)total + (double)u8_out[N_BYTES - 64];
}

// multi-limb add with the carry written as compares, the portable spelling
static double k_limb_add(void) {
    uint64_t carry = 0;
    uint64_t fold = 0;
    for (int i = 0; i < N_LIMB; i++) {
        uint64_t s = limb_a[i] + limb_b[i];
        uint64_t c1 = s < limb_a[i];
        uint64_t t = s + carry;
        uint64_t c2 = t < s;
        limb_out[i] = t;
        fold ^= t;
        carry = c1 | c2;
    }
    return (double)(fold % 1000000007ULL) + (double)carry;
}

// a serial mixer whose rotates are written as shift pairs -- each one should
// become a single rotate
static double k_hash_mix(void) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < N_LIMB; i++) {
        uint64_t k = limb_a[i] * 0x87C37B91114253D5ULL;
        k = (k << 31) | (k >> 33);
        k = k * 0x4CF5AD432745937FULL;
        h = h ^ k;
        h = (h << 27) | (h >> 37);
        h = h * 5 + 0x52DCE729;
    }
    return (double)(h % 1000000007ULL);
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

__attribute__((noinline)) static int32_t step_scale(int32_t v, int32_t k) {
    return v * k + 1;
}

// a direct call neither side may inline -- call overhead with nothing for
// the optimiser to rewrite
static double k_call_direct(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        acc += step_scale(i32_a[i], i32_b[i]);
    }
    return (double)acc;
}

typedef struct {
    int64_t a, b, c, d, e;
} box;

__attribute__((noinline)) static box box_make(int32_t v) {
    box x = {(int64_t)v, (int64_t)v * 2, (int64_t)(v + 3), (int64_t)(v ^ 5), 1};
    return x;
}

__attribute__((noinline)) static box box_step(box x) {
    box y = x;
    y.a = x.a + x.e;
    y.e = x.e + x.b;
    return y;
}

// a 40-byte record made, passed and returned by value through calls neither
// side may inline -- what an aggregate costs to move across a call
static double k_rec_return(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        box b0 = box_make(i32_a[i]);
        box b1 = box_step(b0);
        box b2 = box_step(b1);
        acc += b2.a + b2.e;
    }
    return (double)acc;
}

// array-of-structures update: three fields read, three stored back, at a
// 36-byte stride
static double k_rec_update(void) {
    const float dt = 0.00390625f;
    for (int i = 0; i < N_PART; i++) {
        bodies[i].nx = bodies[i].x + bodies[i].vx * dt;
        bodies[i].ny = bodies[i].y + bodies[i].vy * dt;
        bodies[i].nz = bodies[i].z + bodies[i].vz * dt;
    }
    return (double)bodies[0].nx + (double)bodies[N_PART / 2].ny + (double)bodies[N_PART - 1].nz;
}

// sixteen-way dispatch over an unpredictable opcode stream, as an if/else
// chain on both sides. every arm keeps reg under 2^20, so nothing overflows
static double k_op_dispatch(void) {
    int64_t acc = 0;
    int64_t reg = 1;
    for (int i = 0; i < N_BYTES; i++) {
        int op = u8_a[i] & 15;
        if (op == 0) {
            reg = (reg + 3) & 0xFFFFF;
        } else if (op == 1) {
            reg = reg ^ 0x5A;
        } else if (op == 2) {
            reg = (reg >> 1) + 1;
        } else if (op == 3) {
            acc = acc + (reg & 0xFF);
        } else if (op == 4) {
            reg = (reg * 3) & 0xFFFFF;
        } else if (op == 5) {
            reg = reg | 0x11;
        } else if (op == 6) {
            reg = reg & 0xFFF;
        } else if (op == 7) {
            acc = acc + (reg >> 12);
        } else if (op == 8) {
            reg = (reg + acc) & 0xFFFFF;
        } else if (op == 9) {
            acc = acc + 1;
        } else if (op == 10) {
            reg = (reg << 2) & 0xFFFFF;
        } else if (op == 11) {
            acc = acc + (reg & 0x0F);
        } else if (op == 12) {
            reg = reg ^ (reg >> 3);
        } else if (op == 13) {
            reg = (reg + 0x1234) & 0xFFFFF;
        } else if (op == 14) {
            acc = acc + ((reg >> 4) & 0x3F);
        } else {
            reg = (reg * 5 + 1) & 0xFFFFF;
        }
    }
    return (double)acc;
}

// decimal integers parsed from a byte buffer -- short data-dependent loops
// and compares on every byte
static double k_parse_ints(void) {
    int64_t sum = 0;
    int64_t count = 0;
    int i = 0;
    while (i < N_TEXT) {
        while (i < N_TEXT && text[i] == 32) {
            i++;
        }
        if (i >= N_TEXT) {
            break;
        }
        int64_t v = 0;
        while (i < N_TEXT && text[i] >= 48 && text[i] <= 57) {
            v = v * 10 + (int64_t)(text[i] - 48);
            i++;
        }
        sum += v;
        count++;
    }
    return (double)(sum + count);
}

static int64_t tree_walk(int32_t n) {
    if (n < 0) return 0;
    return (int64_t)nodes[n].val + tree_walk(nodes[n].left) + tree_walk(nodes[n].right);
}

// recursive walk of a tree stored in shuffled order -- recursion whose shape
// comes from the data
static double k_tree_sum(void) {
    return (double)tree_walk(tree_root);
}

// --- idioms ---

// C-idiomatic error handling: an int status plus an out-parameter, checked
// every iteration on a path that never fails. the Mach side uses res[i64,
// StepError], so this measures each language's ordinary error-propagation idiom
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

typedef struct {
    int64_t code, at, arg;
} fault;

__attribute__((noinline)) static int step_wide(int32_t v, int64_t at, int64_t *out, fault *err) {
    if (v < 0) {
        err->code = 1;
        err->at = at;
        err->arg = (int64_t)v;
        return 1;
    }
    *out = (int64_t)v * 3 + at;
    return 0;
}

// the same error path with a three-word error record and a callee neither
// side may inline. the Mach side returns res[i64, Fault], so this measures
// what each language's convention costs when the call is a real one
static double k_result_wide(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_SMALL; i++) {
        int64_t v;
        fault f;
        if (step_wide(i32_a[i], (int64_t)i, &v, &f) != 0) return (double)(0 - f.code);
        acc += v;
    }
    return (double)acc;
}

// a tagged union read with a switch. the Mach side reads a `tag` with `sel`
static double k_tag_dispatch(void) {
    int64_t acc = 0;
    for (int i = 0; i < N_ITEM; i++) {
        switch (items[i].kind) {
        case ITEM_SMALL:
            acc = acc + (int64_t)items[i].u.small;
            break;
        case ITEM_PAIR:
            acc = acc + (int64_t)items[i].u.pair.a * (int64_t)items[i].u.pair.b;
            break;
        case ITEM_WIDE:
            acc = acc + (items[i].u.wide >> 4);
            break;
        default:
            acc = acc + 1;
            break;
        }
    }
    return (double)acc;
}

// four floats as one value, loadable at any element. this is the GCC vector
// extension; the Mach side writes f32x4 over array windows
typedef float v4f __attribute__((vector_size(16), aligned(4), may_alias));

// saxpy written with explicit vectors -- vector lowering with the
// autovectoriser taken out of it
static double k_vec_saxpy(void) {
    const v4f alpha = {1.0009765625f, 1.0009765625f, 1.0009765625f, 1.0009765625f};
    for (int i = 0; i < N_SMALL; i += 4) {
        v4f x = *(const v4f *)&f32_a[i];
        v4f y = *(const v4f *)&f32_b[i];
        *(v4f *)&f32_out[i] = alpha * x + y;
    }
    return (double)f32_out[0] + (double)f32_out[N_SMALL / 2] + (double)f32_out[N_SMALL - 1];
}

const kernel kernels[] = {
    {"f32_saxpy", k_f32_saxpy, 100, 0.0, "array loops", "like"},
    {"f64_dot", k_f64_dot, 100, 0.0, "array loops", "like"},
    {"f32_norm", k_f32_norm, 100, 1e-3, "array loops", "like"},
    {"i32_madd", k_i32_madd, 100, 0.0, "array loops", "like"},
    {"u8_count", k_u8_count, 50, 0.0, "array loops", "like"},
    {"u64_fnv1a", k_u64_fnv1a, 10, 0.0, "array loops", "like"},
    {"i32_filter_sum", k_i32_filter_sum, 100, 0.0, "array loops", "like"},
    {"i64_prefix_sum", k_i64_prefix_sum, 100, 0.0, "array loops", "like"},
    {"f32_matmul", k_f32_matmul, 10, 1e-4, "array loops", "like"},
    {"stream_triad", k_stream_triad, 5, 0.0, "array loops", "like"},
    {"f32_stencil", k_f32_stencil, 100, 0.0, "array loops", "like"},
    {"u8_to_f32", k_u8_to_f32, 100, 0.0, "array loops", "like"},
    {"u8_sum", k_u8_sum, 50, 0.0, "array loops", "like"},
    {"bulk_copy", k_bulk_copy, 100, 0.0, "array loops", "like"},
    {"bulk_fill", k_bulk_fill, 100, 0.0, "array loops", "like"},
    {"copy_short", k_copy_short, 50, 0.0, "array loops", "like"},
    {"limb_add", k_limb_add, 100, 0.0, "array loops", "like"},
    {"hash_mix", k_hash_mix, 50, 0.0, "array loops", "like"},

    {"call_indirect", k_call_indirect, 50, 0.0, "general codegen", "like"},
    {"rec_particle", k_rec_particle, 50, 0.0, "general codegen", "like"},
    {"list_walk", k_list_walk, 20, 0.0, "general codegen", "like"},
    {"branch_dispatch", k_branch_dispatch, 10, 0.0, "general codegen", "like"},
    {"sort_blocks", k_sort_blocks, 20, 0.0, "general codegen", "like"},
    {"recurse_fib", k_recurse_fib, 5, 0.0, "general codegen", "like"},
    {"str_search", k_str_search, 20, 0.0, "general codegen", "like"},
    {"binsearch", k_binsearch, 20, 0.0, "general codegen", "like"},
    {"call_direct", k_call_direct, 50, 0.0, "general codegen", "like"},
    {"rec_return", k_rec_return, 5, 0.0, "general codegen", "like"},
    {"rec_update", k_rec_update, 100, 0.0, "general codegen", "like"},
    {"op_dispatch", k_op_dispatch, 10, 0.0, "general codegen", "like"},
    {"parse_ints", k_parse_ints, 10, 0.0, "general codegen", "like"},
    {"tree_sum", k_tree_sum, 20, 0.0, "general codegen", "like"},

    {"result_chain", k_result_chain, 100, 0.0, "idioms", "idiom"},
    {"result_wide", k_result_wide, 50, 0.0, "idioms", "idiom"},
    {"tag_dispatch", k_tag_dispatch, 50, 0.0, "idioms", "idiom"},
    {"vec_saxpy", k_vec_saxpy, 100, 0.0, "idioms", "idiom"},
};

const int kernel_count = (int)(sizeof(kernels) / sizeof(kernels[0]));
