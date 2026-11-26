/*
 * Differential fuzzer: PocketBignum vs GMP.
 *     fuzz [iterations] [seed]
 */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ---------------------------------------------------------------- rng */

static uint64_t rs;
static uint64_t rnd(void) {
    rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27;
    return rs * 2685821657736338717ull;
}
static unsigned long rndn(unsigned long n) { return n ? (unsigned long)(rnd() % n) : 0; }

/* ---------------------------------------------------------------- bridging */

static void to_gmp(mpz_ptr g, pbz_srcptr x) {
    size_t n = (size_t)abs(x->_mp_size);
    mpz_import(g, n, -1, sizeof(pbn_limb), 0, 0, x->_mp_d);
    if (x->_mp_size < 0) mpz_neg(g, g);
}

static void from_gmp(pbz_ptr x, mpz_srcptr g) {
    size_t cnt = 0, n = (mpz_sizeinbase(g, 2) + PBN_LIMB_BITS - 1) / PBN_LIMB_BITS;
    pbn_limb *p = pbz_limbs_write(x, (pbn_size)n + 1);
    memset(p, 0, (n + 1) * sizeof(pbn_limb));
    mpz_export(p, &cnt, -1, sizeof(pbn_limb), 0, 0, g);
    pbz_limbs_finish(x, mpz_sgn(g) < 0 ? -(pbn_size)cnt : (pbn_size)cnt);
}

/* ---------------------------------------------------------------- reporting */

static const char *cur_op = "";
static mpz_t ctx[4];
static int nctx = 0;
static unsigned long long checks = 0;
static unsigned long cur_ul = 0;
static long cur_k = 0;

static void dump_ctx(void) {
    int i;
    fprintf(stderr, "  op: %s   (ul=%lu, k=%ld)\n", cur_op, cur_ul, cur_k);
    for (i = 0; i < nctx; i++) gmp_fprintf(stderr, "  in%d = %#Zx\n", i, ctx[i]);
}

static void fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    dump_ctx();
    exit(1);
}

static void check_inv(pbz_srcptr x) {
    int n = abs(x->_mp_size);
    if (n > x->_mp_alloc && x->_mp_alloc != 0) fail("size > alloc");
    if (n > 0 && x->_mp_d[n - 1] == 0) fail("not normalized");
}

static void check(const char *what, pbz_srcptr got, mpz_srcptr exp) {
    mpz_t g;
    checks++;
    check_inv(got);
    mpz_init(g);
    to_gmp(g, got);
    if (mpz_cmp(g, exp) != 0) {
        fprintf(stderr, "FAIL: %s mismatch\n", what);
        dump_ctx();
        gmp_fprintf(stderr, "  expected = %#Zx\n  got      = %#Zx\n", exp, g);
        exit(1);
    }
    mpz_clear(g);
}

static void check_int(const char *what, long got, long exp) {
    checks++;
    if (got != exp) {
        fprintf(stderr, "FAIL: %s: expected %ld, got %ld\n", what, exp, got);
        dump_ctx();
        exit(1);
    }
}

static int sgn(int x) { return (x > 0) - (x < 0); }

static void set_ctx(int n, ...) {
    va_list ap;
    int i;
    va_start(ap, n);
    nctx = n;
    for (i = 0; i < n; i++) mpz_set(ctx[i], va_arg(ap, mpz_srcptr));
    va_end(ap);
}

/* ---------------------------------------------------------------- generators */

static int maxlimbs = 40;

static void gen(pbz_ptr x, mpz_ptr g) {
    unsigned long r = rndn(100), n, i;
    pbn_limb *p;
    if (r < 5)       n = 0;
    else if (r < 50) n = 1 + rndn(3);
    else if (r < 85) n = 1 + rndn(12);
    else if (r < 97) n = 1 + rndn((unsigned long)maxlimbs);
    else             n = 1 + rndn((unsigned long)maxlimbs * 6);
    p = pbz_limbs_write(x, (pbn_size)n + 1);
    switch (rndn(5)) {
        case 0: /* runs of ones and zeros (like mpz_rrandomb) */
        {
            unsigned long bits = n * PBN_LIMB_BITS, pos = 0;
            int bit = (int)rndn(2);
            memset(p, 0, (n + 1) * sizeof(pbn_limb));
            while (pos < bits) {
                unsigned long run = 1 + rndn(rndn(2) ? 8 : 150);
                if (bit) for (i = pos; i < pos + run && i < bits; i++) p[i / PBN_LIMB_BITS] |= (pbn_limb)1 << (i % PBN_LIMB_BITS);
                pos += run;
                bit ^= 1;
            }
            break;
        }
        case 1: /* all ones / power of two edge */
            for (i = 0; i < n; i++) p[i] = ~(pbn_limb)0;
            if (rndn(2) && n) { for (i = 0; i < n; i++) p[i] = 0; p[n - 1] = (pbn_limb)1 << rndn(PBN_LIMB_BITS); }
            if (rndn(2) && n) p[0] ^= (pbn_limb)rndn(3);
            break;
        case 2: /* small top limb */
            for (i = 0; i < n; i++) p[i] = (pbn_limb)rnd();
            if (n) p[n - 1] = (pbn_limb)(1 + rndn(5));
            break;
        default:
            for (i = 0; i < n; i++) p[i] = (pbn_limb)rnd();
            break;
    }
    pbz_limbs_finish(x, rndn(2) ? -(pbn_size)n : (pbn_size)n);
    to_gmp(g, x);
}

static void gen_nonzero(pbz_ptr x, mpz_ptr g) {
    do gen(x, g); while (mpz_sgn(g) == 0);
}

static unsigned long gen_ul(void) {
    switch (rndn(6)) {
        case 0: return rndn(4);
        case 1: return ULONG_MAX - rndn(3);
        case 2: return (unsigned long)LONG_MAX + rndn(3) - 1;
        case 3: return (unsigned long)rnd() >> rndn(sizeof(unsigned long) * 8);
        default: return (unsigned long)rnd();
    }
}
static long gen_sl(void) { return (long)gen_ul(); }

/* ---------------------------------------------------------------- binary ops with aliasing */

typedef void (*pfn3)(pbz_ptr, pbz_srcptr, pbz_srcptr);
typedef void (*gfn3)(mpz_ptr, mpz_srcptr, mpz_srcptr);

static void test_bin(const char *name, pfn3 pf, gfn3 gf, pbz_srcptr a, pbz_srcptr b, mpz_srcptr ga, mpz_srcptr gb) {
    pbz_t r; mpz_t e;
    cur_op = name;
    pbz_init(r); mpz_init(e);
    gf(e, ga, gb);
    pf(r, a, b); check(name, r, e);                      /* fresh */
    pbz_set(r, a); pf(r, r, b); check(name, r, e);       /* r = a */
    pbz_set(r, b); pf(r, a, r); check(name, r, e);       /* r = b */
    pbz_clear(r); mpz_clear(e);
}

static void test_self(const char *name, pfn3 pf, gfn3 gf, pbz_srcptr a, mpz_srcptr ga) {
    pbz_t r; mpz_t e;
    cur_op = name;
    pbz_init(r); mpz_init(e);
    gf(e, ga, ga);
    pf(r, a, a); check(name, r, e);
    pbz_set(r, a); pf(r, r, r); check(name, r, e);
    pbz_clear(r); mpz_clear(e);
}

/* ---------------------------------------------------------------- test groups */

static void t_arith(void) {
    pbz_t a, b; mpz_t ga, gb;
    pbz_inits(a, b, NULL); mpz_inits(ga, gb, NULL);
    gen(a, ga); gen(b, gb);
    set_ctx(2, ga, gb);
    test_bin("add", pbz_add, mpz_add, a, b, ga, gb);
    test_bin("sub", pbz_sub, mpz_sub, a, b, ga, gb);
    test_bin("mul", pbz_mul, mpz_mul, a, b, ga, gb);
    test_self("sqr", pbz_mul, mpz_mul, a, ga);
    test_self("add_self", pbz_add, mpz_add, a, ga);
    test_self("sub_self", pbz_sub, mpz_sub, a, ga);
    test_bin("and", pbz_and, mpz_and, a, b, ga, gb);
    test_bin("ior", pbz_ior, mpz_ior, a, b, ga, gb);
    test_bin("xor", pbz_xor, mpz_xor, a, b, ga, gb);
    test_bin("gcd", pbz_gcd, mpz_gcd, a, b, ga, gb);
    test_bin("lcm", pbz_lcm, mpz_lcm, a, b, ga, gb);
    {
        pbz_t r; mpz_t e;
        pbz_init(r); mpz_init(e);
        cur_op = "addmul"; pbz_set(r, a); mpz_set(e, ga); pbz_addmul(r, a, b); mpz_addmul(e, ga, gb); check("addmul", r, e);
        cur_op = "submul"; pbz_set(r, b); mpz_set(e, gb); pbz_submul(r, a, b); mpz_submul(e, ga, gb); check("submul", r, e);
        cur_op = "neg"; pbz_neg(r, a); mpz_neg(e, ga); check("neg", r, e);
        cur_op = "abs"; pbz_abs(r, a); mpz_abs(e, ga); check("abs", r, e);
        cur_op = "com"; pbz_com(r, a); mpz_com(e, ga); check("com", r, e);
        cur_op = "com_alias"; pbz_set(r, a); pbz_com(r, r); check("com", r, e);
        cur_op = "cmp"; check_int("cmp", sgn(pbz_cmp(a, b)), sgn(mpz_cmp(ga, gb)));
        cur_op = "cmpabs"; check_int("cmpabs", sgn(pbz_cmpabs(a, b)), sgn(mpz_cmpabs(ga, gb)));
        cur_op = "popcount"; check_int("popcount", (long)pbz_popcount(a), (long)mpz_popcount(ga));
        cur_op = "sgn"; check_int("sgn", pbz_sgn(a), mpz_sgn(ga));
        cur_op = "odd"; check_int("odd", pbz_odd_p(a), mpz_odd_p(ga));
        pbz_clear(r); mpz_clear(e);
    }
    pbz_clears(a, b, NULL); mpz_clears(ga, gb, NULL);
}

static void t_ui(void) {
    pbz_t a, r; mpz_t ga, e;
    unsigned long u = gen_ul();
    long s = gen_sl();
    pbz_inits(a, r, NULL); mpz_inits(ga, e, NULL);
    gen(a, ga);
    set_ctx(1, ga);
    cur_ul = u;
    cur_op = "add_ui"; pbz_add_ui(r, a, u); mpz_add_ui(e, ga, u); check(cur_op, r, e);
    cur_op = "sub_ui"; pbz_sub_ui(r, a, u); mpz_sub_ui(e, ga, u); check(cur_op, r, e);
    cur_op = "ui_sub"; pbz_ui_sub(r, u, a); mpz_ui_sub(e, u, ga); check(cur_op, r, e);
    cur_op = "mul_ui"; pbz_mul_ui(r, a, u); mpz_mul_ui(e, ga, u); check(cur_op, r, e);
    cur_op = "mul_si"; pbz_mul_si(r, a, s); mpz_mul_si(e, ga, s); check(cur_op, r, e);
    cur_op = "addmul_ui"; pbz_set(r, a); mpz_set(e, ga); pbz_addmul_ui(r, a, u); mpz_addmul_ui(e, ga, u); check(cur_op, r, e);
    cur_op = "submul_ui"; pbz_set(r, a); mpz_set(e, ga); pbz_submul_ui(r, a, u); mpz_submul_ui(e, ga, u); check(cur_op, r, e);
    cur_op = "add_ui_alias"; pbz_set(r, a); pbz_add_ui(r, r, u); mpz_add_ui(e, ga, u); check(cur_op, r, e);
    cur_op = "cmp_ui"; check_int(cur_op, sgn(pbz_cmp_ui(a, u)), sgn(mpz_cmp_ui(ga, u)));
    cur_op = "cmp_si"; check_int(cur_op, sgn(pbz_cmp_si(a, s)), sgn(mpz_cmp_si(ga, s)));
    cur_op = "cmpabs_ui"; check_int(cur_op, sgn(pbz_cmpabs_ui(a, u)), sgn(mpz_cmpabs_ui(ga, u)));
    cur_op = "get_ui"; check_int(cur_op, (long)pbz_get_ui(a), (long)mpz_get_ui(ga));
    cur_op = "get_si"; check_int(cur_op, pbz_get_si(a), mpz_get_si(ga));
    cur_op = "fits_ulong"; check_int(cur_op, pbz_fits_ulong_p(a), mpz_fits_ulong_p(ga));
    cur_op = "fits_slong"; check_int(cur_op, pbz_fits_slong_p(a), mpz_fits_slong_p(ga));
    cur_op = "fits_uint"; check_int(cur_op, pbz_fits_uint_p(a), mpz_fits_uint_p(ga));
    cur_op = "fits_sint"; check_int(cur_op, pbz_fits_sint_p(a), mpz_fits_sint_p(ga));
    cur_op = "fits_ushort"; check_int(cur_op, pbz_fits_ushort_p(a), mpz_fits_ushort_p(ga));
    cur_op = "fits_sshort"; check_int(cur_op, pbz_fits_sshort_p(a), mpz_fits_sshort_p(ga));
    cur_op = "set_ui"; pbz_set_ui(r, u); mpz_set_ui(e, u); check(cur_op, r, e);
    cur_op = "set_si"; pbz_set_si(r, s); mpz_set_si(e, s); check(cur_op, r, e);
    cur_op = "gcd_ui";
    {
        unsigned long g1 = pbz_gcd_ui(r, a, u), g2 = mpz_gcd_ui(e, ga, u);
        check_int(cur_op, (long)g1, (long)g2);
        check(cur_op, r, e);
        check_int("gcd_ui(NULL)", (long)pbz_gcd_ui(NULL, a, u), (long)g2);
    }
    cur_op = "lcm_ui"; pbz_lcm_ui(r, a, u); mpz_lcm_ui(e, ga, u); check(cur_op, r, e);
    if (u) {
        cur_op = "divisible_ui_p"; check_int(cur_op, pbz_divisible_ui_p(a, u), mpz_divisible_ui_p(ga, u));
    }
    pbz_clears(a, r, NULL); mpz_clears(ga, e, NULL);
}

typedef void (*pqr)(pbz_ptr, pbz_ptr, pbz_srcptr, pbz_srcptr);
typedef void (*gqr)(mpz_ptr, mpz_ptr, mpz_srcptr, mpz_srcptr);
typedef void (*pq)(pbz_ptr, pbz_srcptr, pbz_srcptr);
typedef void (*gq)(mpz_ptr, mpz_srcptr, mpz_srcptr);

static void test_div(const char *name, pqr fqr, gqr gqr_, pq fq, gq gq_, pq fr, gq gr_,
                     pbz_srcptr n, pbz_srcptr d, mpz_srcptr gn, mpz_srcptr gd) {
    pbz_t q, r; mpz_t eq, er;
    pbz_inits(q, r, NULL); mpz_inits(eq, er, NULL);
    cur_op = name;
    gqr_(eq, er, gn, gd);
    fqr(q, r, n, d); check(name, q, eq); check(name, r, er);
    pbz_set(q, n); fqr(q, r, q, d); check("qr q=n", q, eq); check("qr q=n", r, er);
    pbz_set(r, n); fqr(q, r, r, d); check("qr r=n", q, eq); check("qr r=n", r, er);
    pbz_set(q, d); fqr(q, r, n, q); check("qr q=d", q, eq); check("qr q=d", r, er);
    pbz_set(r, d); fqr(q, r, n, r); check("qr r=d", q, eq); check("qr r=d", r, er);
    fq(q, n, d); check("q", q, eq);
    pbz_set(q, n); fq(q, q, d); check("q alias n", q, eq);
    pbz_set(q, d); fq(q, n, q); check("q alias d", q, eq);
    fr(r, n, d); check("r", r, er);
    pbz_set(r, n); fr(r, r, d); check("r alias n", r, er);
    pbz_set(r, d); fr(r, n, r); check("r alias d", r, er);
    (void)gq_; (void)gr_;
    pbz_clears(q, r, NULL); mpz_clears(eq, er, NULL);
}

typedef unsigned long (*pqr_ui)(pbz_ptr, pbz_ptr, pbz_srcptr, unsigned long);
typedef unsigned long (*gqr_ui)(mpz_ptr, mpz_ptr, mpz_srcptr, unsigned long);
typedef unsigned long (*pq_ui)(pbz_ptr, pbz_srcptr, unsigned long);
typedef unsigned long (*gq_ui)(mpz_ptr, mpz_srcptr, unsigned long);
typedef unsigned long (*p_ui)(pbz_srcptr, unsigned long);
typedef unsigned long (*g_ui)(mpz_srcptr, unsigned long);

static void test_div_ui(const char *name, pqr_ui fqr, gqr_ui gqr_, pq_ui fq, gq_ui gq_, pq_ui fr, gq_ui gr_,
                        p_ui f, g_ui g, pbz_srcptr n, mpz_srcptr gn, unsigned long d) {
    pbz_t q, r; mpz_t eq, er;
    unsigned long x, y;
    pbz_inits(q, r, NULL); mpz_inits(eq, er, NULL);
    cur_op = name;
    y = gqr_(eq, er, gn, d);
    x = fqr(q, r, n, d); check_int(name, (long)x, (long)y); check(name, q, eq); check(name, r, er);
    pbz_set(q, n); x = fqr(q, r, q, d); check_int(name, (long)x, (long)y); check(name, q, eq); check(name, r, er);
    pbz_set(r, n); x = fqr(q, r, r, d); check(name, q, eq); check(name, r, er);
    x = fq(q, n, d); check_int("q_ui ret", (long)x, (long)gq_(eq, gn, d)); check("q_ui", q, eq);
    x = fr(r, n, d); check_int("r_ui ret", (long)x, (long)gr_(er, gn, d)); check("r_ui", r, er);
    check_int("div_ui", (long)f(n, d), (long)g(gn, d));
    pbz_clears(q, r, NULL); mpz_clears(eq, er, NULL);
}

static void t_div(void) {
    pbz_t n, d; mpz_t gn, gd;
    unsigned long u;
    pbz_inits(n, d, NULL); mpz_inits(gn, gd, NULL);
    gen(n, gn);
    gen_nonzero(d, gd);
    if (rndn(3) == 0) {   /* make n a multiple of d plus something small */
        pbz_t t; mpz_t gt;
        pbz_init(t); mpz_init(gt);
        gen(t, gt);
        mpz_mul(gn, gd, gt);
        if (rndn(2)) mpz_add_ui(gn, gn, rndn(3));
        from_gmp(n, gn);
        pbz_clear(t); mpz_clear(gt);
    }
    set_ctx(2, gn, gd);
    test_div("tdiv", pbz_tdiv_qr, mpz_tdiv_qr, pbz_tdiv_q, mpz_tdiv_q, pbz_tdiv_r, mpz_tdiv_r, n, d, gn, gd);
    test_div("fdiv", pbz_fdiv_qr, mpz_fdiv_qr, pbz_fdiv_q, mpz_fdiv_q, pbz_fdiv_r, mpz_fdiv_r, n, d, gn, gd);
    test_div("cdiv", pbz_cdiv_qr, mpz_cdiv_qr, pbz_cdiv_q, mpz_cdiv_q, pbz_cdiv_r, mpz_cdiv_r, n, d, gn, gd);
    {
        pbz_t r; mpz_t e;
        pbz_init(r); mpz_init(e);
        cur_op = "mod"; pbz_mod(r, n, d); mpz_mod(e, gn, gd); check(cur_op, r, e);
        pbz_set(r, d); pbz_mod(r, n, r); check("mod alias d", r, e);
        cur_op = "divisible_p"; check_int(cur_op, pbz_divisible_p(n, d), mpz_divisible_p(gn, gd));
        if (mpz_divisible_p(gn, gd)) {
            cur_op = "divexact"; pbz_divexact(r, n, d); mpz_divexact(e, gn, gd); check(cur_op, r, e);
        }
        pbz_clear(r); mpz_clear(e);
    }
    u = gen_ul();
    if (u == 0) u = 1 + rndn(1000);
    cur_ul = u;
    test_div_ui("tdiv_ui", pbz_tdiv_qr_ui, mpz_tdiv_qr_ui, pbz_tdiv_q_ui, mpz_tdiv_q_ui, pbz_tdiv_r_ui, mpz_tdiv_r_ui, pbz_tdiv_ui, mpz_tdiv_ui, n, gn, u);
    test_div_ui("fdiv_ui", pbz_fdiv_qr_ui, mpz_fdiv_qr_ui, pbz_fdiv_q_ui, mpz_fdiv_q_ui, pbz_fdiv_r_ui, mpz_fdiv_r_ui, pbz_fdiv_ui, mpz_fdiv_ui, n, gn, u);
    test_div_ui("cdiv_ui", pbz_cdiv_qr_ui, mpz_cdiv_qr_ui, pbz_cdiv_q_ui, mpz_cdiv_q_ui, pbz_cdiv_r_ui, mpz_cdiv_r_ui, pbz_cdiv_ui, mpz_cdiv_ui, n, gn, u);
    pbz_clears(n, d, NULL); mpz_clears(gn, gd, NULL);
}

typedef void (*p2e)(pbz_ptr, pbz_srcptr, pbn_bitcnt);
typedef void (*g2e)(mpz_ptr, mpz_srcptr, mp_bitcnt_t);

static void test_2exp(const char *name, p2e pf, g2e gf, pbz_srcptr a, mpz_srcptr ga, pbn_bitcnt k) {
    pbz_t r; mpz_t e;
    pbz_init(r); mpz_init(e);
    cur_op = name;
    gf(e, ga, k);
    pf(r, a, k); check(name, r, e);
    pbz_set(r, a); pf(r, r, k); check(name, r, e);
    pbz_clear(r); mpz_clear(e);
}

static void t_bits(void) {
    pbz_t a, r; mpz_t ga, e;
    pbn_bitcnt k;
    pbz_inits(a, r, NULL); mpz_inits(ga, e, NULL);
    gen(a, ga);
    k = rndn(2) ? rndn(PBN_LIMB_BITS * 2 + 2) : rndn((unsigned long)maxlimbs * PBN_LIMB_BITS * 2);
    if (rndn(4) == 0) k = (pbn_bitcnt)(rndn(6) * PBN_LIMB_BITS);
    cur_k = (long)k;
    set_ctx(1, ga);
    test_2exp("mul_2exp", pbz_mul_2exp, mpz_mul_2exp, a, ga, k);
    test_2exp("tdiv_q_2exp", pbz_tdiv_q_2exp, mpz_tdiv_q_2exp, a, ga, k);
    test_2exp("fdiv_q_2exp", pbz_fdiv_q_2exp, mpz_fdiv_q_2exp, a, ga, k);
    test_2exp("cdiv_q_2exp", pbz_cdiv_q_2exp, mpz_cdiv_q_2exp, a, ga, k);
    test_2exp("tdiv_r_2exp", pbz_tdiv_r_2exp, mpz_tdiv_r_2exp, a, ga, k);
    test_2exp("fdiv_r_2exp", pbz_fdiv_r_2exp, mpz_fdiv_r_2exp, a, ga, k);
    test_2exp("cdiv_r_2exp", pbz_cdiv_r_2exp, mpz_cdiv_r_2exp, a, ga, k);
    cur_op = "tstbit"; check_int(cur_op, pbz_tstbit(a, k), mpz_tstbit(ga, k));
    cur_op = "setbit"; pbz_set(r, a); mpz_set(e, ga); pbz_setbit(r, k); mpz_setbit(e, k); check(cur_op, r, e);
    cur_op = "clrbit"; pbz_set(r, a); mpz_set(e, ga); pbz_clrbit(r, k); mpz_clrbit(e, k); check(cur_op, r, e);
    cur_op = "combit"; pbz_set(r, a); mpz_set(e, ga); pbz_combit(r, k); mpz_combit(e, k); check(cur_op, r, e);
    pbz_clears(a, r, NULL); mpz_clears(ga, e, NULL);
}

static double gen_double(void) {
    double d;
    switch (rndn(5)) {
        case 0: d = (double)(long)rnd() / (double)(1 + rndn(1000)); break;
        case 1: d = ldexp((double)(rnd() >> 11), (int)rndn(400) - 60); break;
        case 2: d = (double)(long)rndn(100) - 50 + (rndn(2) ? 0.5 : 0.0); break;
        case 3: d = ldexp(1.0, (int)rndn(1024)) * (rndn(2) ? 1.0 : 1.75); if (d - d != 0) d = DBL_MAX; break;
        default: { uint64_t b = rnd(); memcpy(&d, &b, 8); if (d != d || d - d != 0) d = 1.5; } break;
    }
    return rndn(2) ? -d : d;
}

static void t_conv(void) {
    pbz_t a, r; mpz_t ga, e;
    double d = gen_double();
    pbz_inits(a, r, NULL); mpz_inits(ga, e, NULL);
    gen(a, ga);
    set_ctx(1, ga);
    cur_op = "get_d";
    {
        double x = pbz_get_d(a), y = mpz_get_d(ga);
        checks++;
        if (memcmp(&x, &y, sizeof x) != 0) {
            fprintf(stderr, "FAIL get_d: expected %a got %a\n", y, x);
            dump_ctx(); exit(1);
        }
    }
    cur_op = "set_d"; pbz_set_d(r, d); mpz_set_d(e, d); check(cur_op, r, e);
    cur_op = "cmp_d"; check_int(cur_op, sgn(pbz_cmp_d(a, d)), sgn(mpz_cmp_d(ga, d)));
    cur_op = "cmpabs_d"; check_int(cur_op, sgn(pbz_cmpabs_d(a, d)), sgn(mpz_cmpabs_d(ga, d)));
    cur_op = "cmp_d(exact)";
    {
        double x = mpz_get_d(ga);
        if (x - x == 0) check_int(cur_op, sgn(pbz_cmp_d(a, x)), sgn(mpz_cmp_d(ga, x)));
        check_int("cmp_d(inf)", sgn(pbz_cmp_d(a, HUGE_VAL)), sgn(mpz_cmp_d(ga, HUGE_VAL)));
        check_int("cmp_d(-inf)", sgn(pbz_cmp_d(a, -HUGE_VAL)), sgn(mpz_cmp_d(ga, -HUGE_VAL)));
    }

    /* strings */
    {
        static const int bases[] = { 10, 10, 10, 16, 2, 8, 3, 7, 36, 37, 62, -16, -36, 0, 1, -1, 5, 32, 60 };
        int base = bases[rndn(sizeof bases / sizeof bases[0])];
        char *s1, *s2;
        size_t sz;
        cur_op = "get_str";
        cur_k = base;
        s1 = pbz_get_str(NULL, base, a);
        s2 = mpz_get_str(NULL, base, ga);
        checks++;
        if (strcmp(s1, s2) != 0) {
            fprintf(stderr, "FAIL get_str base %d\n  exp %s\n  got %s\n", base, s2, s1);
            dump_ctx(); exit(1);
        }
        /* into caller buffer */
        {
            int b2 = base < 2 && base > -2 ? 10 : (base < 0 ? -base : base);
            char *buf = malloc(pbz_sizeinbase(a, b2) + 2);
            pbz_get_str(buf, base, a);
            if (strcmp(buf, s2) != 0) fail("get_str(buf)");
            free(buf);
        }
        /* sizeinbase: exact or one too big */
        if (base >= 2) {
            size_t digits = strlen(s2) - (s2[0] == '-');
            sz = pbz_sizeinbase(a, base);
            checks++;
            if (sz != digits && sz != digits + 1) {
                fprintf(stderr, "FAIL sizeinbase(%d): %zu for %zu digits\n", base, sz, digits);
                dump_ctx(); exit(1);
            }
            if ((base & (base - 1)) == 0) check_int("sizeinbase pow2", (long)sz, (long)mpz_sizeinbase(ga, base));
        }
        /* round trip */
        if (base >= 2 || base == 0) {
            int b = base;
            int rp, rg;
            if (base == 0) b = 0;
            cur_op = "set_str";
            rp = pbz_set_str(r, s2, b);
            rg = mpz_set_str(e, s2, b);
            check_int(cur_op, rp, rg);
            if (rg == 0) check(cur_op, r, e);
        }
        free(s1);
        free(s2);
    }
    pbz_clears(a, r, NULL); mpz_clears(ga, e, NULL);
}

static void t_set_str_fuzz(void) {
    static const char alphabet[] = "0123456789abcdefxyzABCXYZ -+\t\n_.";
    char buf[64];
    int len = (int)rndn(sizeof buf - 1), i, base, rp, rg;
    pbz_t r; mpz_t e;
    static const int bases[] = { 0, 0, 0, 2, 8, 10, 16, 36, 37, 62 };
    pbz_init(r); mpz_init(e);
    for (i = 0; i < len; i++) buf[i] = alphabet[rndn(sizeof alphabet - 1)];
    if (rndn(2) && len > 2) { buf[0] = '0'; buf[1] = "xXbB1"[rndn(5)]; }
    buf[len] = 0;
    base = bases[rndn(sizeof bases / sizeof bases[0])];
    nctx = 0;
    cur_op = "set_str(fuzz)";
    cur_k = base;
    pbz_set_ui(r, 12345); mpz_set_ui(e, 12345);
    rp = pbz_set_str(r, buf, base);
    rg = mpz_set_str(e, buf, base);
    if (rp != rg) { fprintf(stderr, "string: \"%s\"\n", buf); check_int(cur_op, rp, rg); }
    if (rg == 0) check(cur_op, r, e);
    checks++;
    pbz_clear(r); mpz_clear(e);
}

static void t_pow(void) {
    pbz_t a, b, m, r; mpz_t ga, gb, gm, e;
    unsigned long u;
    int save = maxlimbs;
    pbz_inits(a, b, m, r, NULL); mpz_inits(ga, gb, gm, e, NULL);

    /* pow_ui */
    maxlimbs = 6;
    gen(a, ga);
    u = rndn(40);
    if (mpz_sizeinbase(ga, 2) < 8) u = rndn(300);
    set_ctx(1, ga);
    cur_ul = u;
    cur_op = "pow_ui"; pbz_pow_ui(r, a, u); mpz_pow_ui(e, ga, u); check(cur_op, r, e);
    pbz_set(r, a); pbz_pow_ui(r, r, u); check("pow_ui alias", r, e);
    cur_op = "ui_pow_ui"; pbz_ui_pow_ui(r, u % 50, u); mpz_ui_pow_ui(e, u % 50, u); check(cur_op, r, e);

    /* powm */
    maxlimbs = save > 24 ? 24 : save;
    gen(a, ga);
    gen(b, gb);
    gen_nonzero(m, gm);
    if (mpz_sizeinbase(gb, 2) > 600) { mpz_tdiv_q_2exp(gb, gb, mpz_sizeinbase(gb, 2) - 600); from_gmp(b, gb); }
    set_ctx(3, ga, gb, gm);
    cur_op = "powm";
    if (mpz_sgn(gb) < 0) {
        mpz_t inv; mpz_init(inv);
        if (!mpz_invert(inv, ga, gm)) { mpz_neg(gb, gb); from_gmp(b, gb); }
        mpz_clear(inv);
    }
    mpz_powm(e, ga, gb, gm);
    pbz_powm(r, a, b, m); check(cur_op, r, e);
    pbz_set(r, a); pbz_powm(r, r, b, m); check("powm r=b", r, e);
    pbz_set(r, b); pbz_powm(r, a, r, m); check("powm r=e", r, e);
    pbz_set(r, m); pbz_powm(r, a, b, r); check("powm r=m", r, e);
    u = gen_ul() >> rndn(64);
    cur_ul = u;
    cur_op = "powm_ui"; pbz_powm_ui(r, a, u, m); mpz_powm_ui(e, ga, u, gm); check(cur_op, r, e);

    /* powm with odd moduli of 1-2 limbs (a code path of their own): moduli close to
       B or B^2 too, and bases close to the modulus */
    {
        pbz_t a2, m2; mpz_t ga2, gm2;
        pbz_inits(a2, m2, NULL); mpz_inits(ga2, gm2, NULL);
        maxlimbs = 2;
        gen_nonzero(m2, gm2);
        mpz_abs(gm2, gm2);
        if (rndn(3) == 0) {
            mpz_set_ui(gm2, 0);
            mpz_setbit(gm2, PBN_LIMB_BITS * (1 + rndn(2)));
            mpz_sub_ui(gm2, gm2, 1 + 2 * rndn(4));
        }
        mpz_setbit(gm2, 0);
        if (mpz_cmp_ui(gm2, 1) > 0) {
            if (rndn(2)) mpz_neg(gm2, gm2);
            switch (rndn(4)) {
                case 0:  gen(a2, ga2); break;
                case 1:  mpz_abs(ga2, gm2); mpz_sub_ui(ga2, ga2, 1 + rndn(3)); break;
                case 2:  mpz_abs(ga2, gm2); mpz_sub_ui(ga2, ga2, 1); mpz_neg(ga2, ga2); break;
                default: gen(a2, ga2); mpz_mul(ga2, ga2, gm2); mpz_add_ui(ga2, ga2, rndn(5)); break;
            }
            from_gmp(a2, ga2);
            from_gmp(m2, gm2);
            mpz_abs(gb, gb);
            if (mpz_sgn(gb) == 0) mpz_set_ui(gb, 1 + rndn(1000));
            from_gmp(b, gb);
            set_ctx(3, ga2, gb, gm2);
            cur_op = "powm (small m)";
            mpz_powm(e, ga2, gb, gm2);
            pbz_powm(r, a2, b, m2); check(cur_op, r, e);
            pbz_set(r, a2); pbz_powm(r, r, b, m2); check("powm (small m) r=b", r, e);
            pbz_set(r, m2); pbz_powm(r, a2, b, r); check("powm (small m) r=m", r, e);
        }
        pbz_clears(a2, m2, NULL); mpz_clears(ga2, gm2, NULL);
        maxlimbs = save > 24 ? 24 : save;
    }

    /* invert */
    cur_op = "invert";
    {
        int x = pbz_invert(r, a, m), y = mpz_invert(e, ga, gm);
        check_int(cur_op, x != 0, y != 0);
        if (y) check(cur_op, r, e);
    }
    maxlimbs = save;
    pbz_clears(a, b, m, r, NULL); mpz_clears(ga, gb, gm, e, NULL);
}

static void t_roots(void) {
    pbz_t a, s, r; mpz_t ga, es, er;
    pbz_inits(a, s, r, NULL); mpz_inits(ga, es, er, NULL);
    gen(a, ga);
    mpz_abs(ga, ga);
    if (rndn(3) == 0) { mpz_mul(ga, ga, ga); if (rndn(2)) mpz_sub_ui(ga, ga, rndn(2)); if (mpz_sgn(ga) < 0) mpz_set_ui(ga, 0); }
    if (rndn(8) == 0) {
        /* (2^k - 1) * 2^j: all-ones limbs over zero limbs (the root's s1*B^l + q carrying out) */
        unsigned long k = 32 * (1 + rndn(12)), j = rndn(2) ? k : rndn(400);
        mpz_set_ui(ga, 1); mpz_mul_2exp(ga, ga, k); mpz_sub_ui(ga, ga, 1); mpz_mul_2exp(ga, ga, j);
    }
    from_gmp(a, ga);
    set_ctx(1, ga);
    cur_op = "sqrtrem";
    mpz_sqrtrem(es, er, ga);
    pbz_sqrtrem(s, r, a); check(cur_op, s, es); check(cur_op, r, er);
    pbz_set(s, a); pbz_sqrtrem(s, r, s); check("sqrtrem s=a", s, es); check("sqrtrem s=a", r, er);
    pbz_set(r, a); pbz_sqrtrem(s, r, r); check("sqrtrem r=a", s, es); check("sqrtrem r=a", r, er);
    cur_op = "sqrt"; pbz_sqrt(s, a); check(cur_op, s, es);
    cur_op = "perfect_square_p"; check_int(cur_op, pbz_perfect_square_p(a), mpz_perfect_square_p(ga));
    pbz_clears(a, s, r, NULL); mpz_clears(ga, es, er, NULL);
}

static void t_prime(void) {
    pbz_t a, r; mpz_t ga, e;
    int save = maxlimbs;
    pbz_inits(a, r, NULL); mpz_inits(ga, e, NULL);
    maxlimbs = 6;
    gen(a, ga);
    if (rndn(2)) mpz_tdiv_q_2exp(ga, ga, rndn(mpz_sizeinbase(ga, 2) + 1));
    if (rndn(3) == 0) mpz_nextprime(ga, ga);
    if (rndn(5) == 0) { mpz_t p; mpz_init(p); mpz_nextprime(p, ga); mpz_mul(ga, ga, p); mpz_clear(p); }
    from_gmp(a, ga);
    set_ctx(1, ga);
    cur_op = "probab_prime_p";
    {
        int x = pbz_probab_prime_p(a, 25), y = mpz_probab_prime_p(ga, 25);
        check_int(cur_op, x, y);
    }
    if (mpz_sizeinbase(ga, 2) < 300) {
        cur_op = "nextprime"; pbz_nextprime(r, a); mpz_nextprime(e, ga); check(cur_op, r, e);
        pbz_set(r, a); pbz_nextprime(r, r); check("nextprime alias", r, e);
    }
    maxlimbs = save;
    pbz_clears(a, r, NULL); mpz_clears(ga, e, NULL);
}

static void t_fac(void) {
    pbz_t r; mpz_t e;
    unsigned long n = rndn(4) ? rndn(100) : rndn(3000);
    pbz_init(r); mpz_init(e);
    nctx = 0;
    cur_ul = n;
    cur_op = "fac_ui"; pbz_fac_ui(r, n); mpz_fac_ui(e, n); check(cur_op, r, e);
    pbz_clear(r); mpz_clear(e);
}

static void t_big_mul(void) {
    /* exercise Karatsuba / unbalanced paths specifically */
    pbz_t a, b, r; mpz_t ga, gb, e;
    unsigned long an = 20 + rndn(600), bn = 20 + rndn(rndn(2) ? 600 : 80);
    pbn_limb *p;
    unsigned long i;
    pbz_inits(a, b, r, NULL); mpz_inits(ga, gb, e, NULL);
    p = pbz_limbs_write(a, (pbn_size)an);
    for (i = 0; i < an; i++) p[i] = rndn(8) ? (pbn_limb)rnd() : (rndn(2) ? ~(pbn_limb)0 : 0);
    p[an - 1] |= 1;
    pbz_limbs_finish(a, (pbn_size)an);
    p = pbz_limbs_write(b, (pbn_size)bn);
    for (i = 0; i < bn; i++) p[i] = rndn(8) ? (pbn_limb)rnd() : (rndn(2) ? ~(pbn_limb)0 : 0);
    p[bn - 1] |= 1;
    pbz_limbs_finish(b, (pbn_size)bn);
    to_gmp(ga, a); to_gmp(gb, b);
    set_ctx(2, ga, gb);
    cur_op = "big mul"; pbz_mul(r, a, b); mpz_mul(e, ga, gb); check(cur_op, r, e);
    cur_op = "big sqr"; pbz_mul(r, a, a); mpz_mul(e, ga, ga); check(cur_op, r, e);
    cur_op = "big div"; pbz_tdiv_qr(r, a, r, b); mpz_tdiv_qr(e, ga, e, gb); check(cur_op, r, e); check(cur_op, a, ga);
    pbz_clears(a, b, r, NULL); mpz_clears(ga, gb, e, NULL);
}

static void t_edge(void) {
    pbz_t r; mpz_t e;
    pbz_init(r); mpz_init(e);
    nctx = 0;
    cur_op = "invert mod 1";
    {
        pbz_t a, m; mpz_t ga, gm;
        pbz_init_set_ui(a, 5); pbz_init_set_si(m, -1);
        mpz_init_set_ui(ga, 5); mpz_init_set_si(gm, -1);
        check_int(cur_op, pbz_invert(r, a, m) != 0, mpz_invert(e, ga, gm) != 0);
        if (mpz_invert(e, ga, gm)) check(cur_op, r, e);
        pbz_clear(a); pbz_clear(m); mpz_clear(ga); mpz_clear(gm);
    }
    cur_op = "init_set_str";
    {
        pbz_t x; mpz_t gx;
        int a1 = pbz_init_set_str(x, "  -0x1fffffffffffffffffffff", 0);
        int a2 = mpz_init_set_str(gx, "  -0x1fffffffffffffffffffff", 0);
        check_int(cur_op, a1, a2); check(cur_op, x, gx);
        pbz_clear(x); mpz_clear(gx);
    }
    pbz_clear(r); mpz_clear(e);
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 20000;
    unsigned long long seed = argc > 2 ? strtoull(argv[2], NULL, 10) : 0x1234567ull;
    unsigned long it;
    int i;
    rs = seed ? seed : 1;
    for (i = 0; i < 4; i++) mpz_init(ctx[i]);

    printf("pocketbignum %s fuzz: limb=%d bits%s, %lu iterations, seed %llu\n", pbn_version, PBN_LIMB_BITS,
#ifdef PBN_PORTABLE
           " (portable)",
#else
           "",
#endif
           iters, seed);
    t_edge();
    for (it = 0; it < iters; it++) {
        t_arith();
        t_ui();
        t_div();
        t_bits();
        t_conv();
        t_set_str_fuzz();
        if (it % 4 == 0) t_pow();
        if (it % 2 == 0) t_roots();
        if (it % 4 == 1) t_prime();
        if (it % 10 == 0) t_fac();
        if (it % 8 == 3) t_big_mul();
        if ((it + 1) % 5000 == 0) { printf("  %lu iterations, %llu checks ok\n", it + 1, checks); fflush(stdout); }
    }
    printf("OK: %llu checks passed\n", checks);
    return 0;
}
