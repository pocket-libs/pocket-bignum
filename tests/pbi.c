/* pbi (small-or-big integers) vs pbz: values around the int64 boundary & multi-limb ones */
#define PBN_IMPLEMENTATION
#include "../pocketbignum.h"
#include <stdio.h>
#include <stdlib.h>

static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static long checks = 0, fails = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 10) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* a random value, as both a pbi and a pbz */
static pbi gen(pbz_ptr z) {
    int64_t near[] = { INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1, 0, 1, -1, (int64_t)1 << 62, -((int64_t)1 << 62) };
    switch (rnd() % 4) {
        case 0: { int64_t v = (int64_t)((uint64_t)near[rnd() % 9] + (rnd() % 5) - 2);   /* wraps around the limits */ pbz_set_si(z, (long)v);
                  if (sizeof(long) < 8) { pbz_set_si(z, (long)(v >> 32)); pbz_mul_2exp(z, z, 32); pbz_add_ui(z, z, (unsigned long)(uint32_t)v); }
                  return pbi_from_si(v); }
        case 1: { int64_t v = (int64_t)rnd() >> (rnd() % 64);
                  pbz_set_si(z, (long)(v >> 32)); pbz_mul_2exp(z, z, 32); pbz_add_ui(z, z, (unsigned long)(uint32_t)v);
                  return pbi_from_si(v); }
        default: {
            int n = 1 + (int)(rnd() % 6), i;
            pbn_limb *d = pbz_limbs_write(z, n);
            for (i = 0; i < n; i++) d[i] = (pbn_limb)rnd();
            if (rnd() % 3 == 0) d[n - 1] = 0;                 /* sometimes shorter */
            if (rnd() % 4 == 0) d[0] = 0;
            while (n > 0 && d[n - 1] == 0) n--;
            pbz_limbs_finish(z, rnd() & 1 ? -n : n);
            return pbi_from_pbz(z);
        }
    }
}

static int fits64(pbz_srcptr z) {
    pbz_t lo, hi;
    int r;
    pbz_init(lo); pbz_init(hi);
    pbz_set_si(lo, -1); pbz_mul_2exp(lo, lo, 63);              /* -2^63 */
    pbz_set_ui(hi, 1);  pbz_mul_2exp(hi, hi, 63); pbz_sub_ui(hi, hi, 1);
    r = pbz_cmp(z, lo) >= 0 && pbz_cmp(z, hi) <= 0;
    pbz_clear(lo); pbz_clear(hi);
    return r;
}

static void check_eq(pbi x, pbz_srcptr z, const char *what, long it) {
    pbz_t t;
    pbz_init(t);
    pbi_get_pbz(t, x);
    CHECK(pbz_cmp(t, z) == 0, "%s: wrong value (iteration %ld)", what, it);
    CHECK(pbi_small_p(x) == fits64(z), "%s: not canonical (iteration %ld)", what, it);
    pbz_clear(t);
}

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 200000, it;
    if (argc > 2) rs ^= (uint64_t)atol(argv[2]) * 0x9E3779B97F4A7C15ull;
    for (it = 0; it < iters; it++) {
        pbz_t za, zb, zr;
        pbi a, b, r, c;
        pbz_init(za); pbz_init(zb); pbz_init(zr);
        a = gen(za); b = gen(zb);
        check_eq(a, za, "from", it);

        r = pbi_add(a, b); pbz_add(zr, za, zb);
        check_eq(r, zr, "add", it);

        c = pbi_add(r, b);                                        /* chained, possibly big + small */
        pbz_add(zr, zr, zb);
        check_eq(c, zr, "add2", it);

        CHECK(pbi_cmp(a, b) == (pbz_cmp(za, zb) > 0) - (pbz_cmp(za, zb) < 0), "cmp (iteration %ld)", it);
        CHECK(pbi_sgn(a) == pbz_sgn(za), "sgn (iteration %ld)", it);

        pbi_retain(r); pbi_release(r);                            /* shared, then dropped once */
        pbi_release(a); pbi_release(b); pbi_release(r); pbi_release(c);
        pbz_clear(za); pbz_clear(zb); pbz_clear(zr);
    }
    if (fails) { printf("FAIL: %ld of %ld checks\n", fails, checks); return 1; }
    printf("OK: %ld checks passed\n", checks);
    return 0;
}
