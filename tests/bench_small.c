/*
 * Small-number timings, shaped after Arturo's use of bignums: integers that just
 * overflowed 64 bits (1-2 limbs), mixed with machine integers, small rationals,
 * conversions, and lots of short-lived numbers. No GMP needed.
 *
 *     bench_small        prints:  <op> <limbs> <ns per op>
 */
#define _POSIX_C_SOURCE 200112L   /* clock_gettime & CLOCK_MONOTONIC with -std=c99 (glibc) */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN_TIME 0.1

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* keeps results alive, so loops aren't optimized away */
static volatile long sink;

#define BENCH(label, n, STMT) do {                                              \
    double t0, t; long it, reps = 1;                                            \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { STMT; }              \
               t = now() - t0; if (t > MIN_TIME) break; reps *= 2; }            \
    printf("%s %d %.2f\n", label, (int)(n), t / (double)reps * 1e9);            \
    fflush(stdout);                                                             \
} while (0)

int main(void) {
    pbz_t a1, b1, a2, b2, a3, n1, n2, r, q, acc;
    pbq_t x, y, z;
    char buf[256];
    char *s1, *s2;

    pbz_inits(a1, b1, a2, b2, a3, n1, n2, r, q, acc, NULL);
    pbq_inits(x, y, z, NULL);

    /* 1 limb (just above 2^63), 2 limbs (~2^100), 3 limbs; and negatives */
    pbz_set_str(a1, "12345678901234567891", 10);
    pbz_set_str(b1, "10987654321098765432", 10);
    pbz_set_str(a2, "1267650600228229401496703205653", 10);
    pbz_set_str(b2, "9876543210987654321098765432101", 10);
    pbz_set_str(a3, "123456789012345678901234567890123456789012345678901", 10);
    pbz_neg(n1, b1);
    pbz_neg(n2, b2);
    s1 = pbz_get_str(NULL, 10, a1);
    s2 = pbz_get_str(NULL, 10, a2);

    /* lifetime: Arturo creates (and drops) a bignum for most results */
    BENCH("init_set_si_clear", 1, { pbz_t v; pbz_init_set_si(v, (long)0x7FFFFFFFFFFFFFFLL); sink += SIZ(v); pbz_clear(v); });

    /* addition & subtraction */
    BENCH("add", 1, pbz_add(r, a1, b1));
    BENCH("add", 2, pbz_add(r, a2, b2));
    BENCH("add_2_1", 2, pbz_add(r, a2, b1));
    BENCH("add_mixed_sign", 1, pbz_add(r, a1, n1));
    BENCH("add_mixed_sign", 2, pbz_add(r, a2, n2));
    BENCH("sub", 1, pbz_sub(r, a1, b1));
    BENCH("sub", 2, pbz_sub(r, a2, b2));
    BENCH("add_ui", 1, pbz_add_ui(r, a1, 12345));
    BENCH("add_ui", 2, pbz_add_ui(r, a2, 12345));
    BENCH("sub_ui", 1, pbz_sub_ui(r, a1, 12345));
    BENCH("sub_ui", 2, pbz_sub_ui(r, a2, 12345));
    BENCH("ui_sub", 1, pbz_ui_sub(r, 12345, a1));
    BENCH("add_inplace", 2, pbz_add(a2, a2, b1));

    /* multiplication */
    BENCH("mul", 1, pbz_mul(r, a1, b1));
    BENCH("mul", 2, pbz_mul(r, a2, b2));
    BENCH("mul_2_1", 2, pbz_mul(r, a2, b1));
    BENCH("mul_si", 1, pbz_mul_si(r, a1, -12345));
    BENCH("mul_si", 2, pbz_mul_si(r, a2, -12345));
    BENCH("mul_ui", 2, pbz_mul_ui(r, a2, 12345));
    BENCH("addmul_ui", 2, pbz_addmul_ui(acc, a2, 3));
    BENCH("mul_2exp", 2, pbz_mul_2exp(r, a2, 13));
    BENCH("fdiv_q_2exp", 2, pbz_fdiv_q_2exp(r, a2, 13));
    BENCH("pow_ui", 2, pbz_ui_pow_ui(r, 3, 60));

    /* division */
    BENCH("tdiv_qr", 2, pbz_tdiv_qr(q, r, a2, b1));
    BENCH("tdiv_qr", 3, pbz_tdiv_qr(q, r, a3, b2));
    BENCH("tdiv_q", 2, pbz_tdiv_q(q, a2, b1));
    BENCH("fdiv_qr", 2, pbz_fdiv_qr(q, r, n2, b1));
    BENCH("mod", 2, pbz_mod(r, a2, b1));
    BENCH("tdiv_r_ui", 2, sink += (long)pbz_tdiv_r_ui(r, a2, 1000003));
    BENCH("tdiv_qr_ui", 2, sink += (long)pbz_tdiv_qr_ui(q, r, a2, 1000003));

    /* comparisons & conversions */
    BENCH("cmp", 2, sink += pbz_cmp(a2, b2));
    BENCH("cmp_si", 1, sink += pbz_cmp_si(a1, -5));
    BENCH("fits_get_si", 1, sink += pbz_fits_slong_p(a1) ? pbz_get_si(a1) : 1);
    BENCH("set_si", 1, pbz_set_si(r, -123456789));
    BENCH("neg", 2, pbz_neg(r, a2));
    BENCH("gcd", 1, pbz_gcd(r, a1, b1));
    BENCH("gcd", 2, pbz_gcd(r, a2, b2));
    BENCH("get_str", 1, pbz_get_str(buf, 10, a1));
    BENCH("get_str", 2, pbz_get_str(buf, 10, a2));
    BENCH("set_str", 1, pbz_set_str(r, s1, 10));
    BENCH("set_str", 2, pbz_set_str(r, s2, 10));

    /* bitwise */
    BENCH("and", 2, pbz_and(r, a2, n2));
    BENCH("ior", 2, pbz_ior(r, a2, b2));
    BENCH("xor", 2, pbz_xor(r, a2, n2));

    /* small rationals */
    BENCH("q_set_si", 1, pbq_set_si(x, -7, 12));
    pbq_set_si(x, 355, 113);
    pbq_set_si(y, -22, 7);
    BENCH("q_add", 1, pbq_add(z, x, y));
    BENCH("q_mul", 1, pbq_mul(z, x, y));
    BENCH("q_div", 1, pbq_div(z, x, y));
    BENCH("q_cmp", 1, sink += pbq_cmp(x, y));

    free(s1);
    free(s2);
    pbz_clears(a1, b1, a2, b2, a3, n1, n2, r, q, acc, NULL);
    pbq_clears(x, y, z, NULL);
    return (int)(sink & 0);
}
