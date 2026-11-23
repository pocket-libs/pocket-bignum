/* Written purely against the GMP API; built with -Icompat so <gmp.h> is PocketBignum. */
#define PBN_IMPLEMENTATION
#include <gmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;

static void expect(const char *what, mpz_srcptr x, const char *dec) {
    char *s = mpz_get_str(NULL, 10, x);
    if (strcmp(s, dec) != 0) { printf("FAIL %s: got %s, want %s\n", what, s, dec); failures++; }
    free(s);
}

int main(void) {
    mpz_t a, b, c;
    mpz_inits(a, b, c, NULL);

    mpz_fac_ui(a, 30);
    expect("30!", a, "265252859812191058636308480000000");

    mpz_ui_pow_ui(a, 2, 127);
    mpz_sub_ui(a, a, 1);
    if (mpz_probab_prime_p(a, 25) != 1) { printf("FAIL M127 prime\n"); failures++; }

    mpz_set_str(b, "123456789012345678901234567890", 10);
    mpz_set_str(c, "-987654321098765432109876543210", 0);
    mpz_mul(a, b, c);
    expect("mul", a, "-121932631137021795226185032733622923332237463801111263526900");
    mpz_fdiv_qr(a, b, c, b);
    expect("fdiv q", a, "-9");
    expect("fdiv r", b, "123456780012345678001234567800");

    mpz_set_ui(a, 4);
    mpz_set_ui(b, 13);
    mpz_set_ui(c, 497);
    mpz_powm(a, a, b, c);
    expect("powm", a, "445");

    mpz_set_si(a, -12345);
    mpz_and(b, a, c);
    expect("and", b, "449");
    if (mpz_sgn(a) != -1 || !mpz_odd_p(a)) { printf("FAIL sgn/odd\n"); failures++; }

    printf("gmp_version (compat) = %s, %d-bit limbs\n", gmp_version, mp_bits_per_limb);
    mpz_clears(a, b, c, NULL);
    if (failures) return 1;
    printf("compat smoke OK\n");
    return 0;
}
