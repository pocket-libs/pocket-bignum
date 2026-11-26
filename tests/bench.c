/* PocketBignum vs GMP micro-benchmarks.  ratio = pbn time / gmp time (lower is better, 1.0 = parity) */
#define _POSIX_C_SOURCE 200112L   /* clock_gettime & CLOCK_MONOTONIC with -std=c99 (glibc) */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static volatile int sink;
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

#define MIN_TIME 0.15
/* keeps the compiler from hoisting a cheap, inlined statement out of the timing loop */
#define BARRIER() __asm__ __volatile__("" ::: "memory")

#define BENCH(label, n, PBN_STMT, GMP_STMT) do {                                   \
    double t0, tp, tg; long it, reps = 1;                                           \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { PBN_STMT; BARRIER(); }   \
               tp = now() - t0; if (tp > MIN_TIME) break; reps *= 2; }              \
    tp /= reps; reps = 1;                                                           \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { GMP_STMT; BARRIER(); }   \
               tg = now() - t0; if (tg > MIN_TIME) break; reps *= 2; }              \
    tg /= reps;                                                                     \
    printf("%-14s %8ld %12.1f %12.1f   %5.2fx\n", label, (long)(n), tp * 1e9, tg * 1e9, tp / tg); \
    fflush(stdout);                                                                 \
} while (0)

int main(int argc, char **argv) {
    static const long sizes_mul[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096 };
    static const long sizes_div[] = { 1, 4, 16, 64, 256, 1024 };
    static const long sizes_str[] = { 4, 64, 1024 };
    pbz_t a, b, c, d; mpz_t ga, gb, gc, gd;
    size_t i;
    int quick = argc > 1 && strcmp(argv[1], "quick") == 0;
    pbz_inits(a, b, c, d, NULL); mpz_inits(ga, gb, gc, gd, NULL);

    printf("pocketbignum %s vs GMP %s  (%d-bit limbs)\n", pbn_version, gmp_version, PBN_LIMB_BITS);
    printf("%-14s %8s %12s %12s   %6s\n", "op", "limbs", "pbn ns", "gmp ns", "ratio");

    for (i = 0; i < 5; i++) {
        long n = sizes_mul[i * 2];
        rand_pair(a, ga, n); rand_pair(b, gb, n);
        BENCH("add", n, pbz_add(c, a, b), mpz_add(gc, ga, gb));
    }
    BENCH("add_ui", 1, pbz_add_ui(c, c, 3), mpz_add_ui(gc, gc, 3));
    for (i = 0; i < sizeof sizes_mul / sizeof *sizes_mul; i++) {
        long n = sizes_mul[i];
        if (quick && n > 1024) break;
        rand_pair(a, ga, n); rand_pair(b, gb, n);
        BENCH("mul", n, pbz_mul(c, a, b), mpz_mul(gc, ga, gb));
    }
    for (i = 0; i < sizeof sizes_mul / sizeof *sizes_mul; i += 2) {
        long n = sizes_mul[i];
        rand_pair(a, ga, n);
        BENCH("sqr", n, pbz_mul(c, a, a), mpz_mul(gc, ga, ga));
    }
    {
        rand_pair(a, ga, 1000); rand_pair(b, gb, 10);
        BENCH("mul 1000x10", 1000, pbz_mul(c, a, b), mpz_mul(gc, ga, gb));
        rand_pair(b, gb, 300);
        BENCH("mul 1000x300", 1000, pbz_mul(c, a, b), mpz_mul(gc, ga, gb));
    }
    for (i = 0; i < sizeof sizes_div / sizeof *sizes_div; i++) {
        long n = sizes_div[i];
        rand_pair(a, ga, 2 * n); rand_pair(b, gb, n);
        BENCH("tdiv_qr 2n/n", n, pbz_tdiv_qr(c, d, a, b), mpz_tdiv_qr(gc, gd, ga, gb));
    }
    /* a quotient much shorter than the divisor */
    rand_pair(a, ga, 2300); rand_pair(b, gb, 2000);
    BENCH("tdiv_qr +300/n", 2000, pbz_tdiv_qr(c, d, a, b), mpz_tdiv_qr(gc, gd, ga, gb));
    if (!quick) {
        rand_pair(a, ga, 10500); rand_pair(b, gb, 10000);
        BENCH("tdiv_qr +500/n", 10000, pbz_tdiv_qr(c, d, a, b), mpz_tdiv_qr(gc, gd, ga, gb));
    }
    rand_pair(a, ga, 64);
    BENCH("tdiv_q_ui", 64, pbz_tdiv_q_ui(c, a, 1000000007UL), mpz_tdiv_q_ui(gc, ga, 1000000007UL));
    for (i = 0; i < sizeof sizes_str / sizeof *sizes_str; i++) {
        long n = sizes_str[i];
        char *s;
        rand_pair(a, ga, n);
        BENCH("get_str 10", n, free(pbz_get_str(NULL, 10, a)), free(mpz_get_str(NULL, 10, ga)));
        s = mpz_get_str(NULL, 10, ga);
        BENCH("set_str 10", n, pbz_set_str(c, s, 10), mpz_set_str(gc, s, 10));
        free(s);
    }
    {
        static const unsigned long facs[] = { 20, 50, 100, 300, 1000, 10000, 100000 };
        for (i = 0; i < sizeof facs / sizeof facs[0]; i++) {
            if (quick && facs[i] > 10000) break;
            BENCH("fac_ui", (long)facs[i], pbz_fac_ui(c, facs[i]), mpz_fac_ui(gc, facs[i]));
        }
    }
    {
        static const long pm[] = { 4, 8, 16, 32 };
        for (i = 0; i < 4; i++) {
            long n = pm[i];
            rand_pair(a, ga, n); rand_pair(b, gb, n); rand_pair(d, gd, n);
            pbz_limbs_modify(d, n)[0] |= 1; mpz_setbit(gd, 0);
            BENCH("powm (odd m)", n, pbz_powm(c, a, b, d), mpz_powm(gc, ga, gb, gd));
        }
    }
    {
        static const long gs[] = { 1, 4, 64, 256 };
        for (i = 0; i < 4; i++) {
            long n = gs[i];
            rand_pair(a, ga, n); rand_pair(b, gb, n);
            BENCH("gcd", n, pbz_gcd(c, a, b), mpz_gcd(gc, ga, gb));
        }
    }
    {
        static const long ss[] = { 1, 8, 64 };
        for (i = 0; i < 3; i++) {
            long n = ss[i];
            rand_pair(a, ga, n);
            BENCH("sqrt", n, pbz_sqrt(c, a), mpz_sqrt(gc, ga));
        }
    }
    {
        mpz_set_ui(gd, 1); mpz_mul_2exp(gd, gd, 255); mpz_nextprime(gd, gd);
        pbz_set_str(d, mpz_get_str(NULL, 16, gd), 16);
        BENCH("prime_p 256b", 4, sink += pbz_probab_prime_p(d, 25), sink += mpz_probab_prime_p(gd, 25));
        mpz_mul_2exp(gd, gd, 768); mpz_nextprime(gd, gd);
        pbz_set_str(d, mpz_get_str(NULL, 16, gd), 16);
        BENCH("prime_p 1024b", 16, sink += pbz_probab_prime_p(d, 25), sink += mpz_probab_prime_p(gd, 25));
    }
    pbz_clears(a, b, c, d, NULL); mpz_clears(ga, gb, gc, gd, NULL);
    return 0;
}
