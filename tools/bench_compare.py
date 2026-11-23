#!/usr/bin/env python3
"""
Compares PocketBignum timings of two builds (e.g. a PR and its base) measured in
the same job, runs interleaved: per row, the median time of each build, and the
speedup (base / head: > 1 means the head is faster), plus geometric means.

    tools/bench_compare.py DIR [--title TITLE] [--info LINE ...] [--threshold 0.05]

DIR holds, for each benchmark program P and run n:
    base.P.n.txt  head.P.n.txt
P = bench / bench_qf (the GMP/MPFR micro-benchmarks: the pbn column is used),
or anything printing "<op> <size> <ns>" lines (e.g. bench_ops, bench_small).
"""

import argparse
import glob
import math
import os
import re
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench_summary import parse as parse_gmp_bench  # noqa: E402


def parse_simple(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and re.match(r"^\d+$", p[1]):
                try:
                    rows.append(("", p[0], p[1], float(p[2])))
                except ValueError:
                    pass
    return rows


def load(path, prog):
    """-> [(section, op, size, pbn_ns)]"""
    if prog == "bench":
        return [(s, o, z, p) for s, o, z, p, _, _ in parse_gmp_bench(path, True)[1]]
    if prog == "bench_qf":
        return [(s, o, z, p) for s, o, z, p, _, _ in parse_gmp_bench(path, False)[1]]
    return parse_simple(path)


def collect(d, side, prog):
    order, data = [], {}
    for path in sorted(glob.glob(os.path.join(d, f"{side}.{prog}.*.txt"))):
        for sec, op, size, ns in load(path, prog):
            key = (sec, op, size)
            if key not in data:
                order.append(key)
                data[key] = []
            data[key].append(ns)
    return order, data


def fmt(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:,.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:,.2f} µs"
    return f"{ns:,.1f} ns"


def gmean(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


TITLES = {
    "bench": "Integers (tests/bench.c)",
    "bench_qf": "Rationals & floats (tests/bench_qf.c)",
    "bench_ops": "Large operations (tests/bench_ops.c)",
    "bench_small": "Small numbers, Arturo-style (tests/bench_small.c)",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--title", default="PocketBignum: PR vs base")
    ap.add_argument("--info", action="append", default=[])
    ap.add_argument("--threshold", type=float, default=0.05,
                    help="mark rows whose speedup differs from 1 by more than this")
    a = ap.parse_args()

    progs = sorted({os.path.basename(p).split(".")[1] for p in glob.glob(os.path.join(a.dir, "head.*.txt"))},
                   key=lambda p: (list(TITLES).index(p) if p in TITLES else 99, p))
    out = [f"## {a.title}", ""]
    for line in a.info:
        out.append(f"- {line}")
    out += ["- speedup = base time / PR time (> 1: the PR is faster); medians of interleaved runs", ""]

    overall, regressions = [], []
    for prog in progs:
        horder, head = collect(a.dir, "head", prog)
        _, base = collect(a.dir, "base", prog)
        keys = [k for k in horder if k in base]
        if not keys:
            continue
        with_size = any(k[2] for k in keys)
        out += [f"### {TITLES.get(prog, prog)}", ""]
        cols = ["op"] + (["size"] if with_size else []) + ["base", "PR", "speedup"]
        out.append("| " + " | ".join(cols) + " |")
        out.append("|" + "|".join(["---"] + ["---:"] * (len(cols) - 1)) + "|")
        sps, section = [], None
        for sec, op, size in keys:
            if sec != section:
                section = sec
                if sec:
                    out.append(f"| **{sec}** |" + " |" * (len(cols) - 1))
            b = statistics.median(base[(sec, op, size)])
            h = statistics.median(head[(sec, op, size)])
            sp = b / h if h > 0 else float("nan")
            sps.append(sp)
            mark = ""
            if sp >= 1 + a.threshold:
                mark = " 🟢"
            elif sp <= 1 - a.threshold:
                mark = " 🔴"
                regressions.append(f"{TITLES.get(prog, prog)}: {op} {size}".strip())
            row = [op] + ([size] if with_size else []) + [fmt(b), fmt(h), f"{sp:.2f}x{mark}"]
            out.append("| " + " | ".join(row) + " |")
        out += ["", f"Geometric mean speedup: **{gmean(sps):.3f}x** over {len(sps)} rows", ""]
        overall += sps

    if overall:
        out.append(f"**Overall geometric mean speedup: {gmean(overall):.3f}x** over {len(overall)} rows")
        out.append("")
    if regressions:
        out.append(f"Rows slower by more than {a.threshold:.0%}: " + ", ".join(regressions))
        out.append("")
    print("\n".join(out))


if __name__ == "__main__":
    main()
