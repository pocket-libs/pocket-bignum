/* PocketBignum rationals/floats vs GMP/MPFR.  ratio = pbn time / reference time */
#define _POSIX_C_SOURCE 200112L   /* clock_gettime & CLOCK_MONOTONIC with -std=c99 (glibc) */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <gmp.h>
#include <mpfr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile long sink;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

#define MIN_TIME 0.12
#define BENCH(label, P, R) do {                                                     \
    double t0, tp, tr; long it, reps = 1;                                           \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { P; }                     \
               tp = now() - t0; if (tp > MIN_TIME) break; reps *= 2; }              \
    tp /= reps; reps = 1;                                                           \
    for (;;) { t0 = now(); for (it = 0; it < reps; it++) { R; }                     \
               tr = now() - t0; if (tr > MIN_TIME) break; reps *= 2; }              \
    tr /= reps;                                                                     \
    printf("%-26s %10.1f %10.1f   %5.2fx\n", label, tp * 1e9, tr * 1e9, tp / tr);   \
    fflush(stdout);                                                                 \
} while (0)

static void bench_float(long prec) {
    pbf_t a, b, c, y; mpfr_t ga, gb, gc, gy;
    pbz_t z; mpz_t gz;
    char buf[4096], label[64];
    mpfr_exp_t e;
    pbf_init2(a, prec); pbf_init2(b, prec); pbf_init2(c, prec); pbf_init2(y, prec);
    mpfr_inits2(prec, ga, gb, gc, gy, (mpfr_ptr)0);
    pbz_init(z); mpz_init(gz);
    pbf_set_str(a, "3.14159265358979323846264338327950288419716939937510", 10, PBF_RNDN);
    mpfr_set_str(ga, "3.14159265358979323846264338327950288419716939937510", 10, MPFR_RNDN);
    pbf_set_str(b, "-2.71828182845904523536028747135266249775724709369995e-3", 10, PBF_RNDN);
    mpfr_set_str(gb, "-2.71828182845904523536028747135266249775724709369995e-3", 10, MPFR_RNDN);
    pbf_set_d(y, 0.7071, PBF_RNDN); mpfr_set_d(gy, 0.7071, MPFR_RNDN);
    pbz_set_str(z, "123456789012345678901234567890", 10); mpz_set_str(gz, "123456789012345678901234567890", 10);

    printf("\n-- float, %ld bits --\n", prec);
#define L(s) (snprintf(label, sizeof label, "%s", s), label)
    BENCH(L("add"), pbf_add(c, a, b, PBF_RNDN), mpfr_add(gc, ga, gb, MPFR_RNDN));
    BENCH(L("sub"), pbf_sub(c, a, b, PBF_RNDN), mpfr_sub(gc, ga, gb, MPFR_RNDN));
    BENCH(L("mul"), pbf_mul(c, a, b, PBF_RNDN), mpfr_mul(gc, ga, gb, MPFR_RNDN));
    BENCH(L("div"), pbf_div(c, a, b, PBF_RNDN), mpfr_div(gc, ga, gb, MPFR_RNDN));
    BENCH(L("add_d"), pbf_add_d(c, a, 1.25, PBF_RNDN), mpfr_add_d(gc, ga, 1.25, MPFR_RNDN));
    BENCH(L("mul_z"), pbf_mul_z(c, a, z, PBF_RNDN), mpfr_mul_z(gc, ga, gz, MPFR_RNDN));
    BENCH(L("cmp"), sink += pbf_cmp(a, b), sink += mpfr_cmp(ga, gb));
    BENCH(L("set_d"), pbf_set_d(c, 1234.5678, PBF_RNDN), mpfr_set_d(gc, 1234.5678, MPFR_RNDN));
    BENCH(L("get_d"), sink += (long)pbf_get_d(a, PBF_RNDN), sink += (long)mpfr_get_d(ga, MPFR_RNDN));
    BENCH(L("get_str 10"), pbf_get_str(buf, &e, 10, 0, a, PBF_RNDN), mpfr_get_str(buf, &e, 10, 0, ga, MPFR_RNDN));
    BENCH(L("set_str 10"), pbf_set_str(c, "1234.5678e-20", 10, PBF_RNDN), mpfr_set_str(gc, "1234.5678e-20", 10, MPFR_RNDN));
    mpz_set_ui(gz, 17); pbz_set_ui(z, 17);
    BENCH(L("pow_z (x^17)"), pbf_pow_z(c, a, z, PBF_RNDN), mpfr_pow_z(gc, ga, gz, MPFR_RNDN));
    BENCH(L("pow (pi^0.7071)"), pbf_pow(c, a, y, PBF_RNDN), mpfr_pow(gc, ga, gy, MPFR_RNDN));
    BENCH(L("pow (pi^-2.5)"), (pbf_set_d(y, -2.5, PBF_RNDN), pbf_pow(c, a, y, PBF_RNDN)), (mpfr_set_d(gy, -2.5, MPFR_RNDN), mpfr_pow(gc, ga, gy, MPFR_RNDN)));

    pbz_clear(z); mpz_clear(gz);
    pbf_clear(a); pbf_clear(b); pbf_clear(c); pbf_clear(y);
    mpfr_clears(ga, gb, gc, gy, (mpfr_ptr)0);
}

static void bench_rat(void) {
    pbq_t a, b, c; mpq_t ga, gb, gc;
    char buf[4096];
    pbq_inits(a, b, c, NULL); mpq_inits(ga, gb, gc, NULL);
    pbq_set_str(a, "355/113", 10); mpq_set_str(ga, "355/113", 10);
    pbq_set_str(b, "-22/7", 10); mpq_set_str(gb, "-22/7", 10);
    printf("\n-- rationals (small) --\n");
    BENCH("add", pbq_add(c, a, b), mpq_add(gc, ga, gb));
    BENCH("mul", pbq_mul(c, a, b), mpq_mul(gc, ga, gb));
    BENCH("div", pbq_div(c, a, b), mpq_div(gc, ga, gb));
    BENCH("cmp", sink += pbq_cmp(a, b), sink += mpq_cmp(ga, gb));
    BENCH("get_d", sink += (long)pbq_get_d(a), sink += (long)mpq_get_d(ga));
    BENCH("get_str", pbq_get_str(buf, 10, a), mpq_get_str(buf, 10, ga));
    BENCH("set_d", pbq_set_d(c, 0.1), mpq_set_d(gc, 0.1));

    pbq_set_str(a, "123456789012345678901234567890123456789/98765432109876543210987654321", 10);
    mpq_set_str(ga, "123456789012345678901234567890123456789/98765432109876543210987654321", 10);
    pbq_canonicalize(a); mpq_canonicalize(ga);
    pbq_set_str(b, "-314159265358979323846264338327950288/271828182845904523536028747135", 10);
    mpq_set_str(gb, "-314159265358979323846264338327950288/271828182845904523536028747135", 10);
    pbq_canonicalize(b); mpq_canonicalize(gb);
    printf("\n-- rationals (~120-bit parts) --\n");
    BENCH("add", pbq_add(c, a, b), mpq_add(gc, ga, gb));
    BENCH("mul", pbq_mul(c, a, b), mpq_mul(gc, ga, gb));
    BENCH("div", pbq_div(c, a, b), mpq_div(gc, ga, gb));
    BENCH("cmp", sink += pbq_cmp(a, b), sink += mpq_cmp(ga, gb));
    BENCH("get_d", sink += (long)pbq_get_d(a), sink += (long)mpq_get_d(ga));
    pbq_clears(a, b, c, NULL); mpq_clears(ga, gb, gc, NULL);
}

int main(void) {
    printf("pocketbignum %s vs GMP %s / MPFR %s\n%-26s %10s %10s   %6s\n", pbn_version, gmp_version, mpfr_get_version(),
           "op", "pbn ns", "ref ns", "ratio");
    bench_rat();
    bench_float(53);
    bench_float(256);
    return 0;
}
