#!/usr/bin/env bash
# Verifies that every GMP/MPFR identifier used by Arturo's bignum code is provided
# by compat/gmp.h + compat/mpfr.h.  Usage: tools/check_arturo_surface.sh [path-to-arturo]
set -euo pipefail
cd "$(dirname "$0")/.."
ARTURO="${1:-$HOME/Developer/open/arturo-lang/arturo}"
SRC=("$ARTURO/src/helpers/bignums.nim" "$ARTURO/src/vm/env.nim")
OUT=build/arturo_surface.c
mkdir -p build
ids=$(grep -ohE '\b(mpz|mpq|mpfr|gmp)_[a-z0-9_]+' "${SRC[@]}" | sort -u)
# wrapper procs bound under a different C name ({.importc: "_mpz_cmp_si".} etc.)
renamed=$(for w in "$ARTURO/src/extras/gmp.nim" "$ARTURO/src/extras/mpfr.nim"; do
    { grep -oE '^func [a-z0-9_]+\*.*importc: *"[_a-z0-9]+"' "$w" || true; } | sed -E 's/^func ([a-z0-9_]+)\*.*importc: *"([_a-z0-9]+)"/\1 \2/'
  done | while read -r nim c; do if echo "$ids" | grep -qx "$nim"; then echo "$c"; fi; done | sort -u)
ids=$(printf "%s\n%s\n" "$ids" "$renamed" | grep -v '^$' | sort -u)
{
  echo '#define PBN_IMPLEMENTATION'
  echo '#include <mpfr.h>'
  echo '#include <stdio.h>'
  echo 'int main(void) {'
  for id in $ids; do
    case "$id" in
      mpz_t|mpq_t|mpz_ptr|mpq_ptr|mpz_srcptr|mpq_srcptr|mpfr_t|mpfr_ptr|mpfr_srcptr|mpfr_prec_t|mpfr_rnd_t|mpfr_exp_t|mpfr_sign_t)
        echo "    { $id *p = 0; (void)p; }" ;;
      mpz_sgn|mpz_odd_p|mpz_even_p)
        echo "    { mpz_t z; mpz_init(z); (void)$id(z); mpz_clear(z); }" ;;
      mpq_sgn|mpq_numref|mpq_denref)
        echo "    { mpq_t q; mpq_init(q); (void)$id(q); mpq_clear(q); }" ;;
      gmp_version)
        echo "    puts(gmp_version);" ;;
      *)
        echo "    (void)&$id;" ;;
    esac
  done
  echo '    puts("all identifiers used by Arturo resolve");'
  echo '    return 0;'
  echo '}'
} > "$OUT"
echo "identifiers used by Arturo: $(echo "$ids" | wc -l | tr -d ' ')"
cc -std=c99 -Wall -Icompat "$OUT" -o build/arturo_surface -lm
./build/arturo_surface
