/* Threshold tuning: time one operation at the given sizes (limbs).
 *
 *     tune <mul|sqr|div|gcd|get_str|set_str> <n>...     prints:  <n> <pbn ns> <gmp ns>
 *
 * Built several times, with different -DPBN_..._THRESHOLD values, to find crossovers. */
#define _POSIX_C_SOURCE 200112L
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN_TIME 0.08
#define BARRIER() __asm__ __volatile__("" ::: "memory")

static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void rand_pair(pbz_ptr x, mpz_ptr g, long limbs) {
    pbn_limb *p = pbz_limbs_write(x, limbs);
    long i;
    for (i = 0; i < limbs; i++) p[i] = (pbn_limb)rnd();
    p[limbs - 1] |= (pbn_limb)1 << (PBN_LIMB_BITS - 1);
    pbz_limbs_finish(x, limbs);
    mpz_import(g, (size_t)limbs, -1, sizeof(pbn_limb), 0, 0, p);
}

/* best of 3 */
#define TIME(out, STMT) do {                                                        \
    double t0, t, best = 1e30; long it, reps = 1; int k;                            \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { STMT; BARRIER(); }       \
               t = now() - t0; if (t > MIN_TIME) break; reps *= 2; }                \
    for (k = 0; k < 3; k++) {                                                       \
        t0 = now(); for (it = 0; it < reps; it++) { STMT; BARRIER(); }              \
        t = (now() - t0) / reps; if (t < best) best = t; }                          \
    out = best * 1e9;                                                               \
} while (0)

int main(int argc, char **argv) {
    pbz_t a, b, c, d; mpz_t ga, gb, gc, gd;
    const char *op;
    int i;
    if (argc < 3) { fprintf(stderr, "usage: tune op n...\n"); return 1; }
    op = argv[1];
    pbz_init(a); pbz_init(b); pbz_init(c); pbz_init(d);
    mpz_init(ga); mpz_init(gb); mpz_init(gc); mpz_init(gd);
    for (i = 2; i < argc; i++) {
        long n = atol(argv[i]);
        double tp = 0, tg = 0;
        if (!strcmp(op, "mul")) {
            rand_pair(a, ga, n); rand_pair(b, gb, n);
            TIME(tp, pbz_mul(c, a, b)); TIME(tg, mpz_mul(gc, ga, gb));
        } else if (!strcmp(op, "sqr")) {
            rand_pair(a, ga, n);
            TIME(tp, pbz_mul(c, a, a)); TIME(tg, mpz_mul(gc, ga, ga));
        } else if (!strcmp(op, "div")) {
            rand_pair(a, ga, 2 * n); rand_pair(b, gb, n);
            TIME(tp, pbz_tdiv_qr(c, d, a, b)); TIME(tg, mpz_tdiv_qr(gc, gd, ga, gb));
        } else if (!strcmp(op, "gcd")) {
            rand_pair(a, ga, n); rand_pair(b, gb, n);
            TIME(tp, pbz_gcd(c, a, b)); TIME(tg, mpz_gcd(gc, ga, gb));
        } else if (!strcmp(op, "get_str")) {
            rand_pair(a, ga, n);
            TIME(tp, free(pbz_get_str(NULL, 10, a))); TIME(tg, free(mpz_get_str(NULL, 10, ga)));
        } else if (!strcmp(op, "set_str")) {
            char *s;
            rand_pair(a, ga, n);
            s = mpz_get_str(NULL, 10, ga);
            TIME(tp, pbz_set_str(c, s, 10)); TIME(tg, mpz_set_str(gc, s, 10));
            free(s);
        } else { fprintf(stderr, "unknown op %s\n", op); return 1; }
        printf("%ld %.1f %.1f\n", n, tp, tg);
        fflush(stdout);
    }
    return 0;
}
