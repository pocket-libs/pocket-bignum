#!/usr/bin/env bash
# Threshold sweeps: the operation timed at sizes around each threshold, built with a few
# candidate values of it. Times are relative to the current value (the first candidate);
# the last line is the geometric mean over the sizes (lower is better).
#
#     tools/tune.sh [experiment...]      (GMP_PREFIX, RUNNER from the environment)
set -u
GMP_PREFIX=${GMP_PREFIX:-/usr}
RUNNER=${RUNNER:-}
CFL="-O3 -std=c99 -DNDEBUG -I$GMP_PREFIX/include"
LIBS="-L$GMP_PREFIX/lib -lgmp -lm"
mkdir -p build/tune

# name | op | threshold | candidates (current first) | sizes | extra flags
EXPERIMENTS='
fft      | mul     | PBN_MUL_FFT_THRESHOLD     | 7000 2000 3000 4000 5000 6000     | 1500 2000 2500 3000 3500 4000 4500 5000 5500 6000 6500 7000 8000 9000
sqrfft   | sqr     | PBN_SQR_FFT_THRESHOLD     | 7000 2000 3000 4000 5000 6000     | 1500 2000 2500 3000 3500 4000 4500 5000 5500 6000 6500 7000 8000 9000
'

echo "$EXPERIMENTS" | while IFS='|' read -r name op thr cands sizes extra; do
    name=$(echo $name); [ -z "$name" ] && continue
    if [ $# -gt 0 ]; then case " $* " in *" $name "*) ;; *) continue ;; esac; fi
    op=$(echo $op); thr=$(echo $thr); extra=${extra:-}
    ok=""
    for v in $cands; do
        if cc $CFL $extra -D$thr=$v tests/tune.c -o build/tune/t$v $LIBS; then ok="$ok $v"; rm -f build/tune/r$v.txt; else echo "$name: $thr=$v doesn't build"; fi
    done
    # interleaved (size by size, candidates in turn, ROUNDS rounds), best time kept
    for n in $sizes; do
        for round in $(seq ${ROUNDS:-3}); do
            for v in $ok; do $RUNNER ./build/tune/t$v $op $n >> build/tune/x$v.txt; done
        done
        for v in $ok; do sort -k2 -g build/tune/x$v.txt | head -1 >> build/tune/r$v.txt; rm -f build/tune/x$v.txt; done
    done
    echo "== $name ($op, $thr)"
    ( cd build/tune && awk -v cands="$ok" '
        BEGIN { nc = split(cands, c, " ") }
        END {
            printf "%8s", "n"; for (j = 1; j <= nc; j++) printf " %7s", c[j]; printf " %10s\n", "gmp/cur"
            for (i = 1; i <= ns; i++) {
                printf "%8d", size[i]
                for (j = 1; j <= nc; j++) { r = t[c[j], i] / t[c[1], i]; lg[j] += log(r); printf " %7.3f", r }
                printf " %10.2f\n", t[c[1], i] / g[i]
            }
            printf "%8s", "geomean"; for (j = 1; j <= nc; j++) printf " %7.3f", exp(lg[j] / ns); printf "\n"
        }
        FNR == 1 { k = 0; v = substr(FILENAME, 2); sub(/\.txt$/, "", v) }
        { k++; t[v, k] = $2; if (v == c[1]) { size[k] = $1; g[k] = $3; ns = k } }
    ' $(for v in $ok; do echo r$v.txt; done) )
done
