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
#define N_PART 16384    // particles, 24 bytes each
#define N_SORT 4096     // sort scratch, sorted in blocks
#define SORT_BLOCK 64   // insertion-sort block length
#define N_QUERY 8192    // binary-search queries
#define FIB_N 27        // naive recursive fibonacci depth
#define NEEDLE_AT 200000 // offset in u8_a the search needle is copied from
#define NEEDLE_LEN 8
#define N_LIMB 32768    // u64 limbs, 256 KB per array
#define N_TREE 16384    // tree nodes, 12 bytes each
#define N_ITEM 16384    // tagged items
#define N_TEXT 262144   // decimal text, 256 KB

// a particle in array-of-structures layout; the Mach side declares the same
// record so the two agree on field order and stride
typedef struct {
    float x, y, z;
    float vx, vy, vz;
} particle;

// a particle with a slot for its next position, so an update stores back
// without changing its own inputs
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float nx, ny, nz;
} body;

// a binary tree node linked by index, -1 for no child
typedef struct {
    int32_t left, right;
    int32_t val;
} node;

// a tagged value; the Mach side declares the same cases as a `tag`
enum { ITEM_NONE = 0, ITEM_SMALL = 1, ITEM_PAIR = 2, ITEM_WIDE = 3 };
typedef struct {
    uint8_t kind;
    union {
        int32_t small;
        struct {
            int32_t a, b;
        } pair;
        int64_t wide;
    } u;
} item;

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
extern particle parts[N_PART];
extern int32_t chase[N_SMALL];   // a single random cycle over [0, N_SMALL)
extern int32_t sortbuf[N_SORT];  // scratch, refilled by the sort kernel
extern int32_t sorted_a[N_SMALL]; // strictly ascending: sorted_a[i] == 2*i
extern uint8_t needle[NEEDLE_LEN];
extern uint64_t limb_a[N_LIMB];
extern uint64_t limb_b[N_LIMB];
extern uint64_t limb_out[N_LIMB];
extern uint8_t u8_out[N_BYTES];
extern body bodies[N_PART];
extern node nodes[N_TREE];  // a complete tree stored in shuffled order
extern int32_t tree_root;
extern item items[N_ITEM];
extern uint8_t text[N_TEXT]; // space-separated decimal integers

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
//
// group is the table section the kernel is reported under.
//
// basis says what the row compares: "like" is the same statements on both
// sides, "idiom" is each language's ordinary way of writing the thing where
// the two do not share one. only "like" rows enter the overall geomean.
typedef struct {
    const char *name;
    kernel_fn fn;
    int iters; // inner invocations per timed rep
    double tol;
    const char *group;
    const char *basis;
} kernel;

extern const kernel kernels[];
extern const int kernel_count;

#endif
