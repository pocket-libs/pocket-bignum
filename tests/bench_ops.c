/*
 * PocketBignum timings for a few large-number operations, in a format shared
 * with tests/bench_ops.rb (Ruby's Integer), so the two can be compared with
 * tools/compare_ops.py.  No GMP needed.
 *
 *     bench_ops [quick]        prints:  <op> <limbs> <ns per op>
 *
 * "limbs" is the size of the operand(s) in 64-bit words (for sqrt: of the root;
 * for div: of the divisor, the dividend having twice as many).
 */
#define _POSIX_C_SOURCE 200112L   /* clock_gettime & CLOCK_MONOTONIC with -std=c99 (glibc) */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN_TIME 0.2
/* keeps the compiler from hoisting a cheap, inlined statement out of the timing loop */
#define BARRIER() __asm__ __volatile__("" ::: "memory")

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27;
    return rs * 2685821657736338717ull;
}

/* random number of exactly 64*n bits (top bit set) */
static void random_bits(pbz_ptr x, long n) {
    long i;
    pbz_set_ui(x, 0);
    for (i = 0; i < 2 * n; i++) {
        pbz_mul_2exp(x, x, 32);
        pbz_add_ui(x, x, (unsigned long)(rnd() & 0xFFFFFFFFu));
    }
    pbz_setbit(x, (pbn_bitcnt)(64 * n - 1));
}

#define BENCH(label, n, STMT) do {                                              \
    double t0, t; long it, reps = 1;                                            \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { STMT; BARRIER(); }  \
               t = now() - t0; if (t > MIN_TIME) break; reps *= 2; }            \
    printf("%s %ld %.1f\n", label, (long)(n), t / (double)reps * 1e9);          \
    fflush(stdout);                                                             \
} while (0)

int main(int argc, char **argv) {
    static const long sizes[] = { 1, 4, 16, 64, 256, 1024, 4096, 16384 };
    int quick = argc > 1 && strcmp(argv[1], "quick") == 0;
    long max = quick ? 1024 : 16384;
    size_t i;
    pbz_t a, b, c, r;
    pbz_inits(a, b, c, r, NULL);

    for (i = 0; i < sizeof sizes / sizeof sizes[0] && sizes[i] <= max; i++) {
        long n = sizes[i];
        random_bits(a, n);
        random_bits(b, n);
        BENCH("mul", n, pbz_mul(c, a, b));
    }
    for (i = 0; i < sizeof sizes / sizeof sizes[0] && sizes[i] <= max; i++) {
        long n = sizes[i];
        random_bits(a, n);
        BENCH("sqr", n, pbz_mul(c, a, a));
    }
    for (i = 0; i < sizeof sizes / sizeof sizes[0] && sizes[i] <= max; i++) {
        long n = sizes[i];
        random_bits(a, 2 * n);
        BENCH("sqrt", n, pbz_sqrt(c, a));
    }
    for (i = 0; i < sizeof sizes / sizeof sizes[0] && sizes[i] <= max; i++) {
        long n = sizes[i];
        random_bits(a, 2 * n);
        random_bits(b, n);
        BENCH("div", n, pbz_tdiv_qr(c, r, a, b));
    }
    for (i = 0; i < sizeof sizes / sizeof sizes[0] && sizes[i] <= max; i++) {
        long n = sizes[i];
        random_bits(a, n);
        random_bits(b, n);
        BENCH("gcd", n, pbz_gcd(c, a, b));
    }
    for (i = 0; i < sizeof sizes / sizeof sizes[0] && sizes[i] <= max; i++) {
        long n = sizes[i];
        char *s;
        random_bits(a, n);
        s = pbz_get_str(NULL, 10, a);
        BENCH("get_str", n, { char *t = pbz_get_str(s, 10, a); (void)t; });
        BENCH("set_str", n, pbz_set_str(c, s, 10));
        free(s);
    }
    pbz_clears(a, b, c, r, NULL);
    return 0;
}
