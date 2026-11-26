# PocketBignum

[![CI](https://github.com/pocket-libs/pocket-bignum/actions/workflows/ci.yml/badge.svg)](https://github.com/pocket-libs/pocket-bignum/actions/workflows/ci.yml)

Small, fast, portable arbitrary-precision arithmetic in a single C99 header:
integers, rationals and correctly rounded binary floats. Exposes the subset of
GMP + MPFR that [Arturo](https://arturo-lang.io) uses, with drop-in `gmp.h` /
`mpfr.h` facades. MIT licensed. No dependencies.

## Usage

```c
#define PBN_IMPLEMENTATION      // in exactly one .c file (or just compile pocketbignum.c)
#include "pocketbignum.h"       // native names: pbz_* (int), pbq_* (rational), pbf_* (float)
```

As a GMP/MPFR replacement, put `compat/` on the include path; `<gmp.h>` and
`<mpfr.h>` then map `mpz_*`, `mpq_*`, `mpfr_*` to PocketBignum, with identical
struct layouts and field names (`__mpz_struct`, `__mpq_struct`, `__mpfr_struct`).

Options: `PBN_STATIC`, `PBN_LIMB_BITS=32|64`, `PBN_PORTABLE` (no intrinsics / `__int128`),
`PBN_NO_ASM` (no assembly kernels), `PBN_ABORT(msg)`. Every algorithm threshold
(`PBN_MUL_TOOM3_THRESHOLD`, `PBN_MUL_FFT_THRESHOLD`, ...) can be overridden with `-D`.

## Scope

| Layer | Functions |
|---|---|
| `mpz` | ~115: arithmetic, all t/f/c divisions, 2exp ops, bitwise (two's complement), pow/powm/invert, gcd/lcm, sqrt, fac, primality (BPSW), nextprime, conversions, strings |
| `mpq` | ~30: canonicalize, add/sub/mul/div, inv/neg/abs, mul/div_2exp, cmp*, get/set_d, get/set_str, num/den access |
| `mpfr` | ~50: init/clear/prec, set/get (d, si, ui, z, str), cmp*, fits_uint_p, add/sub/mul/div (+ `_z`, `_d`), pow, pow_z, predicates. All 5 rounding modes, correct rounding, ternary values, exponent range [1-2^30, 2^30-1] like MPFR |

Semantics follow GMP/MPFR precisely, including edge behaviour Arturo can observe:
`mpz_probab_prime_p` 2/1/0 cutoffs, `mpq_set_si(0, d)` → 0/1, `mpfr_get_str` digit
counts and MPFR's double-rounding of ties, `mpfr_set_str` syntax (`@inf@`, `nan(...)`,
`0x...p`, `@` exponents), signed zeros, pow special cases (C99 Annex F).

## Testing

```sh
make test                  # everything below
make test64 test32 testportable   # mpz fuzz vs GMP (64-bit limbs, 32-bit limbs, pure C)
make testqf                # mpq fuzz vs GMP + mpfr fuzz vs MPFR, same three builds
make testcompat            # program written against <gmp.h> names
make testthresholds        # fuzz with tiny thresholds: Karatsuba, Toom-3 & divide-and-conquer
                           # radix conversion on almost every operation
make bench                 # timings vs GMP / MPFR
make bench-ruby            # timings vs Ruby's Integer (needs ruby)
make bench-small           # small numbers (1-3 limbs), the way Arturo uses them
tools/tune.sh [name...]    # threshold sweeps: each threshold timed with a few values
```

Fuzzers run under ASan + UBSan and compare every result, ternary value and aliasing
variant against GMP 6.3 / MPFR 4.2.2 (needed only for testing: `GMP=/prefix`).

## Performance (vs GMP 6.3 / MPFR 4.2.2)

Ratio = PocketBignum's time / GMP's or MPFR's (lower is better, 1.0× = same speed),
medians of 5 runs on CI (Linux, 64-bit limbs; amd64: AMD EPYC, arm64: Neoverse-N2).

| | amd64 | arm64 |
|---|---|---|
| mpz add (1–16 limbs) | 0.8–1.0× | 1.0–1.5× |
| mpz add (64–256 limbs) | 1.4–1.6× | 1.8–2.0× |
| mpz mul, sqr (1–2 limbs) | 0.8–1.0× | 0.4–1.0× |
| mpz mul, sqr (4–64 limbs) | 1.3–2.0× | 0.9–1.5× |
| mpz mul, sqr (128–4096 limbs) | 1.6–2.0× | 1.5–1.9× |
| mpz mul, sqr (7000–100000 limbs, FFT) | 1.3–2.0× | 1.4–1.9× |
| mpz division (2n by n limbs) | 1.1–1.6× | 1.1–1.5× |
| mpz division (quotient much shorter than the divisor) | 2.0× | 1.8–1.9× |
| mpz gcd | 0.7× (1 limb) – 1.9× | 1.0–1.9× |
| mpz powm (1–2 limb moduli) | 0.8–0.9× | 0.5–0.9× |
| mpz powm (4–32 limb moduli), prime_p | 1.3–2.1× | 1.0–1.3× |
| mpz fac_ui | 0.2× (n = 20) – 2.0× | 0.3–2.0× |
| mpz sqrt | 0.7× (1 limb) – 1.6× | 0.4–1.6× |
| mpz get_str, set_str (base 10, 4–1024 limbs) | 1.1–1.7× | 1.1–1.6× |
| mpq (small & ~120-bit parts) add, mul, div, cmp, get_str | 0.4–1.2× | 0.4–1.2× |
| mpq get_d | 1.1–1.6× | 1.3–1.4× |
| mpfr 53-bit add, sub, mul, cmp, set_d, get_d, get_str, set_str | 0.4–1.1× | 0.6–1.3× |
| mpfr 53-bit div, add_d | 1.2–1.3× | 1.2× |
| mpfr 256-bit add, sub | 2.7–3.8× | 2.0–3.6× |
| mpfr 256-bit mul, div | 1.4× | 1.1–1.2× |
| mpfr pow, pow_z | 1.8–2.8× | 1.7–2.3× |
| **all 103 benchmarks (geometric mean)** | **1.19×** | **1.15×** |

`make bench` runs the same benchmarks locally. Inside Arturo (release builds, whole
scripts), it's within 0.8–1.2× of GMP for everyday work (arithmetic loops, powmod,
primality, conversions, small rationals), and 2–3.4× slower with numbers of tens of
thousands of limbs (`3^3000000`, `factorial 150000`) or long sums of big fractions.

## Limitations

- **Very large numbers**: multiplication above a few thousand limbs is a
  number-theoretic transform modulo three primes (GMP has Schönhage–Strassen and
  tuned assembly), 1.3–2× slower; there's no Newton division either (division is
  divide & conquer at every size), so dividing or printing numbers of ~10⁵ limbs is
  2–3× slower than GMP
- **Mid-size multiplication** (300–2000 limbs): up to Toom-4 (no Toom-6/8), 1.5–1.8×
- **Assembly**: only the `mul_1` / `addmul_1` / `submul_1` kernels, on x86-64 (with
  ADX, detected at run time) and arm64, with GCC or Clang; everything else, MSVC,
  32-bit and `PBN_PORTABLE` builds are plain C (add / sub of long numbers is 1.4–2×
  slower than GMP's)
- **Thresholds** are tuned for the CI runners' CPUs (amd64, arm64); others get
  sensible defaults (`tools/tune.sh` re-tunes them)
- **Floats**: no same-precision fast path for multi-limb add/sub (#23); `mpfr_pow`
  needs double-limb kernels to reach MPFR's speed (#24)
- **Errors** (division by zero, NaN to integer) call `PBN_ABORT` instead of raising
  SIGFPE (#22)
- **Memory functions** (`pbn_set_memory_functions`) are global: set them before any
  thread uses the library
- **Not implemented** (unused by Arturo): `mpf_*`, `mpn_*`, random numbers,
  `mpz_import/export`, I/O streams, other MPFR transcendentals

See also the [open issues](https://github.com/pocket-libs/pocket-bignum/issues).
