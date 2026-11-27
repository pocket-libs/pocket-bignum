/*
 * Differential fuzzer: PocketBignum rationals vs GMP, floats vs MPFR.
 *     fuzz_qf [iterations] [seed]
 */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <gmp.h>
#include <mpfr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <float.h>

static uint64_t rs;
static uint64_t rnd(void) { rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27; return rs * 2685821657736338717ull; }
static unsigned long rndn(unsigned long n) { return n ? (unsigned long)(rnd() % n) : 0; }
static int sgn(long x) { return (x > 0) - (x < 0); }

static unsigned long long checks = 0;
static const char *cur_op = "";
static char ctxbuf[4][4096];
static int nctx = 0;

static void dump(void) {
    int i;
    fprintf(stderr, "  op: %s\n", cur_op);
    for (i = 0; i < nctx; i++) fprintf(stderr, "  in%d = %s\n", i, ctxbuf[i]);
}
static void failf(const char *msg) { fprintf(stderr, "FAIL: %s\n", msg); dump(); exit(1); }
static void check_int(const char *what, long got, long exp) {
    checks++;
    if (got != exp) { fprintf(stderr, "FAIL: %s: expected %ld got %ld\n", what, exp, got); dump(); exit(1); }
}

/* ------------------------------------------------------------------ bridging */

static void z_to_gmp(mpz_ptr g, pbz_srcptr x) {
    mpz_import(g, (size_t)abs(x->_mp_size), -1, sizeof(pbn_limb), 0, 0, x->_mp_d);
    if (x->_mp_size < 0) mpz_neg(g, g);
}
static void z_from_gmp(pbz_ptr x, mpz_srcptr g) {
    size_t cnt = 0, n = (mpz_sizeinbase(g, 2) + PBN_LIMB_BITS - 1) / PBN_LIMB_BITS;
    pbn_limb *p = pbz_limbs_write(x, (pbn_size)n + 1);
    memset(p, 0, (n + 1) * sizeof(pbn_limb));
    mpz_export(p, &cnt, -1, sizeof(pbn_limb), 0, 0, g);
    pbz_limbs_finish(x, mpz_sgn(g) < 0 ? -(pbn_size)cnt : (pbn_size)cnt);
}

/* exact float transfer through (mantissa, exponent) */
static void f_from_mpfr(pbf_ptr x, mpfr_srcptr g) {
    pbf_set_prec(x, mpfr_get_prec(g));
    if (mpfr_nan_p(g)) { pbf_set_nan(x); return; }
    if (mpfr_inf_p(g)) { pbf_set_inf(x, mpfr_sgn(g)); return; }
    if (mpfr_zero_p(g)) { pbf_set_zero(x, mpfr_signbit(g) ? -1 : 1); return; }
    {
        mpz_t m; pbz_t pm; mpfr_exp_t e; pbf_t t;
        mpz_init(m); pbz_init(pm);
        e = mpfr_get_z_2exp(m, g);
        z_from_gmp(pm, m);
        pbf_init2(t, mpfr_get_prec(g) + 64);
        pbf_set_z(t, pm, PBF_RNDN);                      /* exact */
        x->_mpfr_exp = 0;
        pbf_set(x, t, PBF_RNDN);                         /* exact */
        x->_mpfr_exp += e;
        pbf_clear(t); pbz_clear(pm); mpz_clear(m);
    }
}

static void f_to_mpfr(mpfr_ptr g, pbf_srcptr x) {
    mpfr_set_prec(g, pbf_get_prec(x));
    if (pbf_nan_p(x)) { mpfr_set_nan(g); return; }
    if (pbf_inf_p(x)) { mpfr_set_inf(g, pbf_signbit(x) ? -1 : 1); return; }
    if (pbf_zero_p(x)) { mpfr_set_zero(g, pbf_signbit(x) ? -1 : 1); return; }
    {
        pbn_size n = (pbf_get_prec(x) + PBN_LIMB_BITS - 1) / PBN_LIMB_BITS;
        pbz_t m; mpz_t gm;
        pbz_init(m); mpz_init(gm);
        memcpy(pbz_limbs_write(m, n), x->_mpfr_d, (size_t)n * sizeof(pbn_limb));
        pbz_limbs_finish(m, pbf_signbit(x) ? -n : n);
        z_to_gmp(gm, m);
        mpfr_set_z_2exp(g, gm, x->_mpfr_exp - (long)n * PBN_LIMB_BITS, MPFR_RNDN);
        pbz_clear(m); mpz_clear(gm);
    }
}

static void fctx(int i, mpfr_srcptr g) {
    mpfr_snprintf(ctxbuf[i], sizeof ctxbuf[i], "%.*Ra (prec %ld)", 0, g, (long)mpfr_get_prec(g));
    mpfr_snprintf(ctxbuf[i], sizeof ctxbuf[i], "%Ra (prec %ld)", g, (long)mpfr_get_prec(g));
}

static void fcheck(const char *what, pbf_srcptr got, mpfr_srcptr exp) {
    mpfr_t g;
    int ok;
    checks++;
    mpfr_init2(g, mpfr_get_prec(exp));
    f_to_mpfr(g, got);
    if (pbf_get_prec(got) != mpfr_get_prec(exp)) ok = 0;
    else if (mpfr_nan_p(exp)) ok = mpfr_nan_p(g);
    else ok = !mpfr_nan_p(g) && mpfr_equal_p(g, exp) && mpfr_signbit(g) == mpfr_signbit(exp);
    if (!ok) {
        fprintf(stderr, "FAIL: %s mismatch\n", what);
        dump();
        mpfr_fprintf(stderr, "  expected = %Ra\n  got      = %Ra\n", exp, g);
        exit(1);
    }
    mpfr_clear(g);
}

static void tcheck(const char *what, int got, int exp) { check_int(what, sgn(got), sgn(exp)); }

/* ------------------------------------------------------------------ generators */

static void gen_z(mpz_ptr g, int maxbits) {
    unsigned long bits = rndn((unsigned long)maxbits + 1);
    switch (rndn(3)) {
        case 0: { gmp_randstate_t st; (void)st; } /* fallthrough */
        default: {
            unsigned long i;
            mpz_set_ui(g, 0);
            for (i = 0; i < bits; i += 32) { mpz_mul_2exp(g, g, 32); mpz_add_ui(g, g, (unsigned long)(rnd() & 0xffffffffu)); }
            if (bits) mpz_fdiv_r_2exp(g, g, bits);
            if (rndn(3) == 0 && bits) {   /* runs of ones */
                unsigned long a = rndn(bits), b = a + rndn(bits - a + 1), k;
                for (k = a; k < b; k++) mpz_setbit(g, k);
            }
        }
    }
    if (rndn(2)) mpz_neg(g, g);
}

static mpfr_prec_t gen_prec(void) {
    unsigned long r = rndn(10);
    if (r < 5) return 53;
    if (r < 9) return (mpfr_prec_t)(1 + rndn(200));
    return (mpfr_prec_t)(200 + rndn(500));
}

static void gen_f(mpfr_ptr g, mpfr_prec_t prec) {
    unsigned long r = rndn(100);
    mpfr_set_prec(g, prec);
    if (r < 3) { mpfr_set_nan(g); return; }
    if (r < 7) { mpfr_set_inf(g, rndn(2) ? 1 : -1); return; }
    if (r < 12) { mpfr_set_zero(g, rndn(2) ? 1 : -1); return; }
    {
        mpz_t m;
        long e;
        mpz_init(m);
        do gen_z(m, (int)prec); while (mpz_sgn(m) == 0);
        if (rndn(4) == 0) { mpz_set_ui(m, 1 + rndn(10)); mpz_mul_2exp(m, m, rndn(prec)); if (rndn(2)) mpz_neg(m, m); }
        r = rndn(100);
        if (r < 70) e = (long)rndn(200) - 100;
        else if (r < 90) e = (long)rndn(2400) - 1200;
        else if (r < 95) e = mpfr_get_emax() - (long)mpz_sizeinbase(m, 2) - (long)rndn(5);
        else e = mpfr_get_emin() - 1 + (long)rndn(5);
        mpfr_set_z_2exp(g, m, e, MPFR_RNDN);   /* may round if m wider than prec: fine */
        mpz_clear(m);
    }
}

static const mpfr_rnd_t rnds[] = { MPFR_RNDN, MPFR_RNDZ, MPFR_RNDU, MPFR_RNDD, MPFR_RNDA };
static mpfr_rnd_t gen_rnd(void) { return rndn(3) ? MPFR_RNDN : rnds[rndn(5)]; }

static double gen_double(void) {
    double d;
    switch (rndn(6)) {
        case 0: d = (double)(long)rndn(2000) - 1000; break;
        case 1: d = ldexp((double)(rnd() >> 11), (int)rndn(200) - 150); break;
        case 2: d = rndn(2) ? NAN : (rndn(2) ? HUGE_VAL : -HUGE_VAL); break;
        case 3: d = rndn(2) ? 0.0 : -0.0; break;
        case 4: d = ldexp(1.0, (int)rndn(2100) - 1074); break;
        default: { uint64_t b = rnd(); memcpy(&d, &b, 8); } break;
    }
    return rndn(2) ? -d : d;
}

/* ------------------------------------------------------------------ float tests */

typedef int (*pf_op)(pbf_ptr, pbf_srcptr, pbf_srcptr, pbf_rnd_t);
typedef int (*mf_op)(mpfr_ptr, mpfr_srcptr, mpfr_srcptr, mpfr_rnd_t);

static void f_binop(const char *name, pf_op pf, mf_op mf, pbf_srcptr a, pbf_srcptr b, mpfr_srcptr ga, mpfr_srcptr gb) {
    mpfr_prec_t p = gen_prec();
    mpfr_rnd_t rm = gen_rnd();
    pbf_t r; mpfr_t e;
    int t1, t2;
    pbf_init2(r, p); mpfr_init2(e, p);
    cur_op = name;
    t2 = mf(e, ga, gb, rm);
    t1 = pf(r, a, b, (pbf_rnd_t)rm); fcheck(name, r, e); tcheck(name, t1, t2);
    if (pbf_get_prec(a) == p) {   /* aliasing r = a */
        pbf_t ra; pbf_init2(ra, p); pbf_set(ra, a, PBF_RNDN);
        t1 = pf(ra, ra, b, (pbf_rnd_t)rm); fcheck(name, ra, e); tcheck(name, t1, t2);
        pbf_clear(ra);
    }
    if (pbf_get_prec(b) == p) {
        pbf_t rb; pbf_init2(rb, p); pbf_set(rb, b, PBF_RNDN);
        t1 = pf(rb, a, rb, (pbf_rnd_t)rm); fcheck(name, rb, e); tcheck(name, t1, t2);
        pbf_clear(rb);
    }
    pbf_clear(r); mpfr_clear(e);
}

static void t_float(void) {
    mpfr_t ga, gb, e; pbf_t a, b, r;
    mpz_t gz; pbz_t z;
    mpfr_prec_t p;
    mpfr_rnd_t rm;
    double d;
    int t1, t2;
    mpfr_inits2(53, ga, gb, e, (mpfr_ptr)0);
    pbf_init(a); pbf_init(b); pbf_init(r);
    mpz_init(gz); pbz_init(z);

    gen_f(ga, gen_prec());
    gen_f(gb, rndn(4) ? gen_prec() : mpfr_get_prec(ga));
    if (rndn(5) == 0 && mpfr_regular_p(ga)) mpfr_set(gb, ga, MPFR_RNDN);          /* cancellation */
    if (rndn(6) == 0 && mpfr_regular_p(ga)) mpfr_nextabove(gb), mpfr_set(gb, ga, MPFR_RNDN), mpfr_nextabove(gb);
    f_from_mpfr(a, ga); f_from_mpfr(b, gb);
    nctx = 2; fctx(0, ga); fctx(1, gb);

    /* transfer sanity */
    cur_op = "transfer"; fcheck(cur_op, a, ga); fcheck(cur_op, b, gb);

    f_binop("add", pbf_add, mpfr_add, a, b, ga, gb);
    f_binop("sub", pbf_sub, mpfr_sub, a, b, ga, gb);
    f_binop("mul", pbf_mul, mpfr_mul, a, b, ga, gb);
    f_binop("div", pbf_div, mpfr_div, a, b, ga, gb);
    f_binop("sqr", pbf_mul, mpfr_mul, a, a, ga, ga);

    cur_op = "cmp"; check_int(cur_op, sgn(pbf_cmp(a, b)), sgn(mpfr_cmp(ga, gb)));

    p = gen_prec(); rm = gen_rnd();
    pbf_set_prec(r, p); mpfr_set_prec(e, p);
    cur_op = "set"; t2 = mpfr_set(e, ga, rm); t1 = pbf_set(r, a, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "neg"; t2 = mpfr_neg(e, ga, rm); t1 = pbf_neg(r, a, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "abs"; t2 = mpfr_abs(e, ga, rm); t1 = pbf_abs(r, a, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);

    /* integers */
    gen_z(gz, rndn(4) ? 64 : 400);
    z_from_gmp(z, gz);
    cur_op = "set_z"; t2 = mpfr_set_z(e, gz, rm); t1 = pbf_set_z(r, z, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "add_z"; t2 = mpfr_add_z(e, ga, gz, rm); t1 = pbf_add_z(r, a, z, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "sub_z"; t2 = mpfr_sub_z(e, ga, gz, rm); t1 = pbf_sub_z(r, a, z, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "mul_z"; t2 = mpfr_mul_z(e, ga, gz, rm); t1 = pbf_mul_z(r, a, z, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "div_z"; t2 = mpfr_div_z(e, ga, gz, rm); t1 = pbf_div_z(r, a, z, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);

    /* doubles */
    d = gen_double();
    snprintf(ctxbuf[2], sizeof ctxbuf[2], "double %a", d); nctx = 3;
    cur_op = "set_d"; t2 = mpfr_set_d(e, d, rm); t1 = pbf_set_d(r, d, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "add_d"; t2 = mpfr_add_d(e, ga, d, rm); t1 = pbf_add_d(r, a, d, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "sub_d"; t2 = mpfr_sub_d(e, ga, d, rm); t1 = pbf_sub_d(r, a, d, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "mul_d"; t2 = mpfr_mul_d(e, ga, d, rm); t1 = pbf_mul_d(r, a, d, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "div_d"; t2 = mpfr_div_d(e, ga, d, rm); t1 = pbf_div_d(r, a, d, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    cur_op = "cmp_d"; if (d == d) check_int(cur_op, sgn(pbf_cmp_d(a, d)), sgn(mpfr_cmp_d(ga, d)));
    {
        long s = (long)rnd() >> rndn(64);
        unsigned long u = (unsigned long)rnd() >> rndn(64);
        cur_op = "cmp_si"; check_int(cur_op, sgn(pbf_cmp_si(a, s)), sgn(mpfr_cmp_si(ga, s)));
        cur_op = "cmp_ui"; check_int(cur_op, sgn(pbf_cmp_ui(a, u)), sgn(mpfr_cmp_ui(ga, u)));
        cur_op = "set_si"; t2 = mpfr_set_si(e, s, rm); t1 = pbf_set_si(r, s, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
        cur_op = "set_ui"; t2 = mpfr_set_ui(e, u, rm); t1 = pbf_set_ui(r, u, (pbf_rnd_t)rm); fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);
    }

    /* to double */
    {
        double x = pbf_get_d(a, (pbf_rnd_t)rm), y = mpfr_get_d(ga, rm);
        cur_op = "get_d";
        checks++;
        if (!(x != x && y != y) && memcmp(&x, &y, sizeof x) != 0) {
            fprintf(stderr, "FAIL get_d rnd %d: expected %a got %a\n", (int)rm, y, x); dump(); exit(1);
        }
    }
    cur_op = "fits_uint_p"; check_int(cur_op, pbf_fits_uint_p(a, (pbf_rnd_t)rm), mpfr_fits_uint_p(ga, rm));

    mpz_clear(gz); pbz_clear(z);
    pbf_clear(a); pbf_clear(b); pbf_clear(r);
    mpfr_clears(ga, gb, e, (mpfr_ptr)0);
}

static void t_float_str(void) {
    mpfr_t ga, e; pbf_t a, r;
    static const int bases[] = { 10, 10, 10, 10, 2, 16, 8, 3, 36, 62 };
    int base = bases[rndn(sizeof bases / sizeof *bases)];
    size_t n = rndn(3) ? 0 : 1 + rndn(40);
    mpfr_rnd_t rm = gen_rnd();
    mpfr_exp_t e1 = 7, e2 = 7;
    char *s1, *s2;
    mpfr_init2(ga, 53); mpfr_init2(e, 53); pbf_init(a); pbf_init(r);
    gen_f(ga, gen_prec());
    f_from_mpfr(a, ga);
    nctx = 1; fctx(0, ga);
    cur_op = "get_str";
    s2 = mpfr_get_str(NULL, &e2, base, n, ga, rm);
    s1 = pbf_get_str(NULL, &e1, base, n, a, (pbf_rnd_t)rm);
    checks++;
    if (strcmp(s1, s2) != 0 || e1 != e2) {
        fprintf(stderr, "FAIL get_str base %d n %zu rnd %d\n  exp '%s' e=%ld\n  got '%s' e=%ld\n", base, n, (int)rm, s2, (long)e2, s1, (long)e1);
        dump(); exit(1);
    }
    /* parse it back in several spellings */
    if (mpfr_number_p(ga)) {
        char buf[8192];
        const char *dig = s2[0] == '-' ? s2 + 1 : s2;
        mpfr_prec_t p = gen_prec();
        int t1, t2;
        switch (rndn(3)) {
            case 0: snprintf(buf, sizeof buf, "%s0.%s@%ld", s2[0] == '-' ? "-" : "", dig, (long)e2); break;
            case 1: snprintf(buf, sizeof buf, "%s%.1s.%s@%ld", s2[0] == '-' ? "-" : "", dig, dig + 1, (long)e2 - 1); break;
            default: snprintf(buf, sizeof buf, "  %s%s@%ld", s2[0] == '-' ? "-" : "+", dig, (long)e2 - (long)strlen(dig)); break;
        }
        if (base <= 10 && rndn(2)) { char *at = strchr(buf, '@'); if (at) *at = 'e'; }
        snprintf(ctxbuf[1], sizeof ctxbuf[1], "string '%s' base %d", buf, base); nctx = 2;
        pbf_set_prec(r, p); mpfr_set_prec(e, p);
        cur_op = "set_str";
        t2 = mpfr_set_str(e, buf, base, rm);
        t1 = pbf_set_str(r, buf, base, (pbf_rnd_t)rm);
        check_int(cur_op, t1, t2);
        if (t2 == 0) fcheck(cur_op, r, e);
    }
    mpfr_free_str(s2);
    pbf_free_str(s1);
    mpfr_clear(ga); mpfr_clear(e); pbf_clear(a); pbf_clear(r);
}

static void t_float_str_ties(void) {
    /* dyadic values printed with few digits: exercises exact ties and MPFR's double rounding */
    static const int bases[] = { 3, 5, 6, 10, 10, 10, 12, 7 };
    int base = bases[rndn(sizeof bases / sizeof *bases)];
    size_t n = 1 + rndn(6);
    mpfr_rnd_t rm = rnds[rndn(5)];
    mpfr_exp_t e1 = 0, e2 = 0;
    char b1[64], b2[64];
    mpfr_t g; pbf_t a;
    long num = (long)rndn(200000) - 100000;
    mpfr_init2(g, 53); pbf_init2(a, 53);
    mpfr_set_si_2exp(g, num, -(long)rndn(6), MPFR_RNDN);
    if (rndn(3) == 0) mpfr_mul_ui(g, g, 1000 * (1 + rndn(1000)), MPFR_RNDN);
    f_from_mpfr(a, g);
    nctx = 1; fctx(0, g);
    cur_op = "get_str(ties)";
    mpfr_get_str(b2, &e2, base, n, g, rm);
    pbf_get_str(b1, &e1, base, n, a, (pbf_rnd_t)rm);
    checks++;
    if (strcmp(b1, b2) || e1 != e2) {
        fprintf(stderr, "FAIL get_str(ties) base %d n %zu rnd %d\n  exp '%s' e=%ld\n  got '%s' e=%ld\n", base, n, (int)rm, b2, (long)e2, b1, (long)e1);
        dump(); exit(1);
    }
    mpfr_clear(g); pbf_clear(a);
}

static void t_float_strfuzz(void) {
    static const char *pieces[] = { "1", "0", "9", "5", ".", "e", "E", "@", "-", "+", "x", "0x", "0b", "p", "inf",
                                    "nan", "@inf@", "Infinity", " ", "a", "f", "Z", "z", "12345678901234567890", "(1)" };
    static const int bases[] = { 10, 10, 0, 2, 16, 36, 62 };
    char buf[256] = "";
    int k = 1 + (int)rndn(6), i, base = bases[rndn(sizeof bases / sizeof *bases)], t1, t2;
    mpfr_t e; pbf_t r;
    mpfr_prec_t p = gen_prec();
    for (i = 0; i < k; i++) strcat(buf, pieces[rndn(sizeof pieces / sizeof *pieces)]);
    mpfr_init2(e, p); pbf_init2(r, p);
    snprintf(ctxbuf[0], sizeof ctxbuf[0], "string '%s' base %d", buf, base); nctx = 1;
    cur_op = "set_str(fuzz)";
    t2 = mpfr_set_str(e, buf, base, MPFR_RNDN);
    t1 = pbf_set_str(r, buf, base, PBF_RNDN);
    check_int(cur_op, t1, t2);
    if (t2 == 0) fcheck(cur_op, r, e);
    mpfr_clear(e); pbf_clear(r);
}

static void t_pow(void) {
    mpfr_t gx, gy, e; pbf_t x, y, r;
    mpz_t gz; pbz_t z;
    mpfr_prec_t p = rndn(3) ? 53 : (mpfr_prec_t)(2 + rndn(150));
    mpfr_rnd_t rm = gen_rnd();
    int t1, t2;
    mpfr_inits2(53, gx, gy, e, (mpfr_ptr)0);
    pbf_init(x); pbf_init(y); pbf_init2(r, p);
    mpfr_set_prec(e, p);
    mpz_init(gz); pbz_init(z);

    switch (rndn(6)) {
        case 0: /* small "nice" values */
            mpfr_set_prec(gx, 53); mpfr_set_d(gx, (double)((long)rndn(40) - 10) / (double)(1 + rndn(4)), MPFR_RNDN);
            mpfr_set_prec(gy, 53); mpfr_set_d(gy, (double)((long)rndn(40) - 20) / (double)(1 << rndn(4)), MPFR_RNDN);
            break;
        case 1: /* perfect powers with dyadic exponents */
        {
            unsigned long b = 1 + rndn(30), k = 1 + rndn(4);
            mpz_ui_pow_ui(gz, b, 1UL << k);
            mpfr_set_prec(gx, 200); mpfr_set_z_2exp(gx, gz, (long)rndn(16) - 8, MPFR_RNDN);
            mpfr_set_prec(gy, 53); mpfr_set_si_2exp(gy, (long)(2 * rndn(6) + 1) * (rndn(2) ? 1 : -1), -(long)rndn(k + 1), MPFR_RNDN);
            break;
        }
        case 2: /* near one */
            mpfr_set_prec(gx, gen_prec()); mpfr_set_ui(gx, 1, MPFR_RNDN);
            if (rndn(2)) mpfr_nextabove(gx); else mpfr_nextbelow(gx);
            gen_f(gy, 53);
            break;
        default:
            gen_f(gx, rndn(2) ? 53 : gen_prec());
            gen_f(gy, rndn(2) ? 53 : gen_prec());
            if (mpfr_regular_p(gx) && mpfr_get_exp(gx) > 40) mpfr_set_exp(gx, (mpfr_exp_t)rndn(80) - 40);
            if (mpfr_regular_p(gy) && mpfr_get_exp(gy) > 12) mpfr_set_exp(gy, (mpfr_exp_t)rndn(24) - 12);
            if (rndn(3) == 0 && mpfr_regular_p(gx)) mpfr_abs(gx, gx, MPFR_RNDN);
            break;
    }
    f_from_mpfr(x, gx); f_from_mpfr(y, gy);
    nctx = 2; fctx(0, gx); fctx(1, gy);
    snprintf(ctxbuf[2], sizeof ctxbuf[2], "prec %ld rnd %d", (long)p, (int)rm); nctx = 3;
    cur_op = "pow";
    t2 = mpfr_pow(e, gx, gy, rm);
    t1 = pbf_pow(r, x, y, (pbf_rnd_t)rm);
    fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);

    gen_z(gz, rndn(4) ? 8 : 40);
    z_from_gmp(z, gz);
    {
        char *zs = mpz_get_str(NULL, 10, gz);
        snprintf(ctxbuf[1], sizeof ctxbuf[1], "z = %s", zs); free(zs);
    }
    cur_op = "pow_z";
    t2 = mpfr_pow_z(e, gx, gz, rm);
    t1 = pbf_pow_z(r, x, z, (pbf_rnd_t)rm);
    fcheck(cur_op, r, e); tcheck(cur_op, t1, t2);

    mpz_clear(gz); pbz_clear(z);
    pbf_clear(x); pbf_clear(y); pbf_clear(r);
    mpfr_clears(gx, gy, e, (mpfr_ptr)0);
}

/* ------------------------------------------------------------------ rational tests */

static void q_to_gmp(mpq_ptr g, pbq_srcptr q) { z_to_gmp(mpq_numref(g), pbq_numref(q)); z_to_gmp(mpq_denref(g), pbq_denref(q)); }
static void q_from_gmp(pbq_ptr q, mpq_srcptr g) { z_from_gmp(pbq_numref(q), mpq_numref(g)); z_from_gmp(pbq_denref(q), mpq_denref(g)); }

static void qcheck(const char *what, pbq_srcptr got, mpq_srcptr exp) {
    mpq_t g;
    checks++;
    mpq_init(g);
    q_to_gmp(g, got);
    if (mpz_cmp(mpq_numref(g), mpq_numref(exp)) != 0 || mpz_cmp(mpq_denref(g), mpq_denref(exp)) != 0) {
        fprintf(stderr, "FAIL: %s mismatch\n", what);
        dump();
        gmp_fprintf(stderr, "  expected = %Qd\n  got      = %Qd\n", exp, g);
        exit(1);
    }
    mpq_clear(g);
}

static void gen_q(mpq_ptr g) {
    int big = rndn(5) == 0 ? 600 : 100;
    gen_z(mpq_numref(g), big);
    do gen_z(mpq_denref(g), rndn(3) ? big : 2); while (mpz_sgn(mpq_denref(g)) == 0);
    if (rndn(4) == 0) mpz_set_ui(mpq_denref(g), 1);
    if (rndn(6) == 0) mpz_mul_2exp(mpq_denref(g), mpq_denref(g), rndn(70));
    mpq_canonicalize(g);
}

typedef void (*pq_op)(pbq_ptr, pbq_srcptr, pbq_srcptr);
typedef void (*mq_op)(mpq_ptr, mpq_srcptr, mpq_srcptr);

static void q_binop(const char *name, pq_op pf, mq_op mf, pbq_srcptr a, pbq_srcptr b, mpq_srcptr ga, mpq_srcptr gb) {
    pbq_t r; mpq_t e;
    pbq_init(r); mpq_init(e);
    cur_op = name;
    mf(e, ga, gb);
    pf(r, a, b); qcheck(name, r, e);
    pbq_set(r, a); pf(r, r, b); qcheck(name, r, e);
    pbq_set(r, b); pf(r, a, r); qcheck(name, r, e);
    pbq_clear(r); mpq_clear(e);
}

static void t_rat(void) {
    mpq_t ga, gb, e; pbq_t a, b, r;
    pbz_t z; mpz_t gz;
    mpq_inits(ga, gb, e, NULL); pbq_inits(a, b, r, NULL);
    pbz_init(z); mpz_init(gz);
    gen_q(ga); gen_q(gb);
    if (rndn(6) == 0) mpq_set(gb, ga);
    q_from_gmp(a, ga); q_from_gmp(b, gb);
    nctx = 2;
    gmp_snprintf(ctxbuf[0], sizeof ctxbuf[0], "%Qd", ga);
    gmp_snprintf(ctxbuf[1], sizeof ctxbuf[1], "%Qd", gb);

    q_binop("add", pbq_add, mpq_add, a, b, ga, gb);
    q_binop("sub", pbq_sub, mpq_sub, a, b, ga, gb);
    q_binop("mul", pbq_mul, mpq_mul, a, b, ga, gb);
    q_binop("sqr", pbq_mul, mpq_mul, a, a, ga, ga);
    if (mpq_sgn(gb)) q_binop("div", pbq_div, mpq_div, a, b, ga, gb);
    if (mpq_sgn(ga)) q_binop("div self", pbq_div, mpq_div, a, a, ga, ga);

    cur_op = "neg"; mpq_neg(e, ga); pbq_neg(r, a); qcheck(cur_op, r, e);
    cur_op = "abs"; mpq_abs(e, ga); pbq_abs(r, a); qcheck(cur_op, r, e);
    if (mpq_sgn(ga)) {
        cur_op = "inv"; mpq_inv(e, ga); pbq_inv(r, a); qcheck(cur_op, r, e);
        pbq_set(r, a); pbq_inv(r, r); qcheck("inv alias", r, e);
    }
    cur_op = "cmp"; check_int(cur_op, sgn(pbq_cmp(a, b)), sgn(mpq_cmp(ga, gb)));
    cur_op = "equal"; check_int(cur_op, pbq_equal(a, b) != 0, mpq_equal(ga, gb) != 0);
    cur_op = "sgn"; check_int(cur_op, pbq_sgn(a), mpq_sgn(ga));
    {
        unsigned long k = rndn(150);
        cur_op = "mul_2exp"; mpq_mul_2exp(e, ga, k); pbq_mul_2exp(r, a, k); qcheck(cur_op, r, e);
        cur_op = "div_2exp"; mpq_div_2exp(e, ga, k); pbq_div_2exp(r, a, k); qcheck(cur_op, r, e);
        pbq_set(r, a); pbq_mul_2exp(r, r, k); mpq_mul_2exp(e, ga, k); qcheck("mul_2exp alias", r, e);
    }
    {
        long n = (long)rnd() >> rndn(64);
        unsigned long d = 1 + ((unsigned long)rnd() >> rndn(64));
        if (d == 0) d = 1;
        cur_op = "cmp_si"; check_int(cur_op, sgn(pbq_cmp_si(a, n, d)), sgn(mpq_cmp_si(ga, n, d)));
        cur_op = "cmp_ui"; check_int(cur_op, sgn(pbq_cmp_ui(a, (unsigned long)n, d)), sgn(mpq_cmp_ui(ga, (unsigned long)n, d)));
        cur_op = "set_si"; mpq_set_si(e, n, d); pbq_set_si(r, n, d); qcheck(cur_op, r, e);
        cur_op = "set_ui"; mpq_set_ui(e, (unsigned long)n, d); pbq_set_ui(r, (unsigned long)n, d); qcheck(cur_op, r, e);
        cur_op = "canonicalize"; mpq_canonicalize(e); pbq_canonicalize(r); qcheck(cur_op, r, e);
    }
    gen_z(gz, 200); z_from_gmp(z, gz);
    cur_op = "set_z"; mpq_set_z(e, gz); pbq_set_z(r, z); qcheck(cur_op, r, e);
    cur_op = "cmp_z"; check_int(cur_op, sgn(pbq_cmp_z(a, z)), sgn(mpq_cmp_z(ga, gz)));
    cur_op = "set_num"; mpq_set(e, ga); pbq_set(r, a); mpq_set_num(e, gz); pbq_set_num(r, z); qcheck(cur_op, r, e);
    if (mpz_sgn(gz)) { cur_op = "set_den"; mpq_set_den(e, gz); pbq_set_den(r, z); qcheck(cur_op, r, e); }
    cur_op = "get_num"; mpq_get_num(gz, ga); pbq_get_num(z, a); { mpz_t t; mpz_init(t); z_to_gmp(t, z); if (mpz_cmp(t, gz)) failf("get_num"); mpz_clear(t); }
    cur_op = "get_den"; mpq_get_den(gz, ga); pbq_get_den(z, a); { mpz_t t; mpz_init(t); z_to_gmp(t, z); if (mpz_cmp(t, gz)) failf("get_den"); mpz_clear(t); }

    cur_op = "get_d";
    {
        double x = pbq_get_d(a), y = mpq_get_d(ga);
        checks++;
        if (memcmp(&x, &y, sizeof x) != 0) { fprintf(stderr, "FAIL get_d: expected %a got %a\n", y, x); dump(); exit(1); }
    }
    {
        double d = gen_double();
        if (d == d && d - d == 0) { cur_op = "set_d"; mpq_set_d(e, d); pbq_set_d(r, d); qcheck(cur_op, r, e); }
    }
    {
        static const int bases[] = { 10, 10, 16, 2, 36, 62, -16 };
        int base = bases[rndn(sizeof bases / sizeof *bases)], t1, t2;
        char *s1 = pbq_get_str(NULL, base, a), *s2 = mpq_get_str(NULL, base, ga);
        cur_op = "get_str";
        checks++;
        if (strcmp(s1, s2)) { fprintf(stderr, "FAIL get_str %d\n exp %s\n got %s\n", base, s2, s1); dump(); exit(1); }
        if (base > 0) {
            cur_op = "set_str";
            t2 = mpq_set_str(e, s2, base); t1 = pbq_set_str(r, s2, base);
            check_int(cur_op, t1, t2);
            if (!t2) qcheck(cur_op, r, e);
        }
        free(s1); free(s2);
    }
    {
        static const char *strs[] = { "1/2", "-3/6", "7", "/3", "3/", "0/5", "1/-2", " 4 / 8", "0x10/0x4", "abc", "12/0", "1/2/3" };
        const char *s = strs[rndn(sizeof strs / sizeof *strs)];
        int base = rndn(2) ? 0 : 10, t1, t2;
        snprintf(ctxbuf[0], sizeof ctxbuf[0], "'%s' base %d", s, base); nctx = 1;
        cur_op = "set_str(fixed)";
        t2 = mpq_set_str(e, s, base); t1 = pbq_set_str(r, s, base);
        check_int(cur_op, t1, t2);
        if (!t2) qcheck(cur_op, r, e);
    }
    pbz_clear(z); mpz_clear(gz);
    mpq_clears(ga, gb, e, NULL); pbq_clears(a, b, r, NULL);
}

/* the functions added for completeness */
static void t_float_more(void) {
    mpfr_t ga, gb, e; pbf_t a, b, r;
    mpz_t gz; pbz_t z;
    mpfr_prec_t p = gen_prec();
    mpfr_rnd_t rm = gen_rnd();
    unsigned long u;
    long sl;
    int t1, t2;
    mpfr_inits2(53, ga, gb, e, (mpfr_ptr)0);
    pbf_init(a); pbf_init(b); pbf_init2(r, p); mpfr_set_prec(e, p);
    mpz_init(gz); pbz_init(z);
    gen_f(ga, gen_prec());
    gen_f(gb, rndn(4) ? gen_prec() : mpfr_get_prec(ga));
    if (rndn(6) == 0) mpfr_set(gb, ga, MPFR_RNDN);
    if (rndn(8) == 0 && mpfr_regular_p(ga)) mpfr_neg(gb, ga, MPFR_RNDN);
    f_from_mpfr(a, ga); f_from_mpfr(b, gb);
    nctx = 2; fctx(0, ga); fctx(1, gb);
    switch (rndn(4)) {
        case 0:  u = 0; break;
        case 1:  u = rndn(10); break;
        case 2:  u = ULONG_MAX - rndn(3); break;
        default: u = (unsigned long)rnd() >> rndn(64); break;
    }
    switch (rndn(4)) {
        case 0:  sl = 0; break;
        case 1:  sl = (long)rndn(20) - 10; break;
        case 2:  sl = rndn(2) ? LONG_MIN : LONG_MAX; break;
        default: sl = (long)rnd() >> rndn(64); break;
    }

#define FOP(name, pcall, mcall) do { cur_op = name; t2 = mcall; t1 = pcall; fcheck(name, r, e); tcheck(name, t1, t2); } while (0)
    FOP("add_ui", pbf_add_ui(r, a, u, (pbf_rnd_t)rm), mpfr_add_ui(e, ga, u, rm));
    FOP("add_si", pbf_add_si(r, a, sl, (pbf_rnd_t)rm), mpfr_add_si(e, ga, sl, rm));
    FOP("sub_ui", pbf_sub_ui(r, a, u, (pbf_rnd_t)rm), mpfr_sub_ui(e, ga, u, rm));
    FOP("sub_si", pbf_sub_si(r, a, sl, (pbf_rnd_t)rm), mpfr_sub_si(e, ga, sl, rm));
    FOP("ui_sub", pbf_ui_sub(r, u, a, (pbf_rnd_t)rm), mpfr_ui_sub(e, u, ga, rm));
    FOP("si_sub", pbf_si_sub(r, sl, a, (pbf_rnd_t)rm), mpfr_si_sub(e, sl, ga, rm));
    FOP("mul_ui", pbf_mul_ui(r, a, u, (pbf_rnd_t)rm), mpfr_mul_ui(e, ga, u, rm));
    FOP("mul_si", pbf_mul_si(r, a, sl, (pbf_rnd_t)rm), mpfr_mul_si(e, ga, sl, rm));
    FOP("div_ui", pbf_div_ui(r, a, u, (pbf_rnd_t)rm), mpfr_div_ui(e, ga, u, rm));
    FOP("div_si", pbf_div_si(r, a, sl, (pbf_rnd_t)rm), mpfr_div_si(e, ga, sl, rm));
    FOP("ui_div", pbf_ui_div(r, u, a, (pbf_rnd_t)rm), mpfr_ui_div(e, u, ga, rm));
    FOP("si_div", pbf_si_div(r, sl, a, (pbf_rnd_t)rm), mpfr_si_div(e, sl, ga, rm));
    FOP("sqr", pbf_sqr(r, a, (pbf_rnd_t)rm), mpfr_sqr(e, ga, rm));
    FOP("min", pbf_min(r, a, b, (pbf_rnd_t)rm), mpfr_min(e, ga, gb, rm));
    FOP("max", pbf_max(r, a, b, (pbf_rnd_t)rm), mpfr_max(e, ga, gb, rm));
    FOP("setsign", pbf_setsign(r, a, (int)(u & 1), (pbf_rnd_t)rm), mpfr_setsign(e, ga, (int)(u & 1), rm));
    FOP("copysign", pbf_copysign(r, a, b, (pbf_rnd_t)rm), mpfr_copysign(e, ga, gb, rm));
#undef FOP
    /* (the exact return value, not just its sign) */
    cur_op = "floor"; t2 = mpfr_floor(e, ga); t1 = pbf_floor(r, a); fcheck(cur_op, r, e); check_int(cur_op, t1, t2);
    cur_op = "ceil";  t2 = mpfr_ceil(e, ga);  t1 = pbf_ceil(r, a);  fcheck(cur_op, r, e); check_int(cur_op, t1, t2);
    cur_op = "trunc"; t2 = mpfr_trunc(e, ga); t1 = pbf_trunc(r, a); fcheck(cur_op, r, e); check_int(cur_op, t1, t2);

    mpfr_clear_flags();
    cur_op = "cmpabs";         check_int(cur_op, sgn(pbf_cmpabs(a, b)), sgn(mpfr_cmpabs(ga, gb)));
    cur_op = "greater_p";      check_int(cur_op, pbf_greater_p(a, b), mpfr_greater_p(ga, gb));
    cur_op = "greaterequal_p"; check_int(cur_op, pbf_greaterequal_p(a, b), mpfr_greaterequal_p(ga, gb));
    cur_op = "less_p";         check_int(cur_op, pbf_less_p(a, b), mpfr_less_p(ga, gb));
    cur_op = "lessequal_p";    check_int(cur_op, pbf_lessequal_p(a, b), mpfr_lessequal_p(ga, gb));
    cur_op = "lessgreater_p";  check_int(cur_op, pbf_lessgreater_p(a, b), mpfr_lessgreater_p(ga, gb));
    cur_op = "equal_p";        check_int(cur_op, pbf_equal_p(a, b), mpfr_equal_p(ga, gb));
    cur_op = "unordered_p";    check_int(cur_op, pbf_unordered_p(a, b), mpfr_unordered_p(ga, gb));
    cur_op = "integer_p";      check_int(cur_op, pbf_integer_p(a), mpfr_integer_p(ga));

    if (mpfr_number_p(ga) && mpfr_get_exp(ga) < 4000) {
        cur_op = "get_z"; t2 = mpfr_get_z(gz, ga, rm); t1 = pbf_get_z(z, a, (pbf_rnd_t)rm);
        { mpz_t x; mpz_init(x); z_to_gmp(x, z); check_int(cur_op, mpz_cmp(x, gz) == 0, 1); mpz_clear(x); }
        tcheck(cur_op, t1, t2);
    }
    cur_op = "get_si"; check_int(cur_op, pbf_get_si(a, (pbf_rnd_t)rm), mpfr_get_si(ga, rm));
    cur_op = "get_ui"; check_int(cur_op, (long)pbf_get_ui(a, (pbf_rnd_t)rm), (long)mpfr_get_ui(ga, rm));

    mpfr_clears(ga, gb, e, (mpfr_ptr)0);
    pbf_clear(a); pbf_clear(b); pbf_clear(r);
    mpz_clear(gz); pbz_clear(z);
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 20000, it;
    unsigned long long seed = argc > 2 ? strtoull(argv[2], NULL, 10) : 1;
    rs = seed ? seed : 1;
    printf("pocketbignum %s rational/float fuzz vs GMP %s / MPFR %s: limb=%d, %lu iterations, seed %llu\n",
           pbn_version, gmp_version, mpfr_get_version(), PBN_LIMB_BITS, iters, seed);
    for (it = 0; it < iters; it++) {
        t_rat();
        t_float();
        t_float_str();
        t_float_strfuzz();
        t_float_str_ties();
        t_float_str_ties();
        if (it % 2 == 0) t_pow();
        t_float_more();
        if ((it + 1) % 5000 == 0) { printf("  %lu iterations, %llu checks ok\n", it + 1, checks); fflush(stdout); }
    }
    pbf_free_cache();
    printf("OK: %llu checks passed\n", checks);
    return 0;
}
