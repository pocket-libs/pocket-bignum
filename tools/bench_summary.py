#!/usr/bin/env python3
"""
Aggregates several runs of build/bench and build/bench_qf into Markdown tables
(e.g. for $GITHUB_STEP_SUMMARY): per row, the median time of each side, the
median ratio (pbn / reference: lower is better, 1.00 = parity) and the min-max
spread of the ratio across runs, plus the geometric mean of all median ratios.

    tools/bench_summary.py [--title TITLE] [--info LINE ...] DIR

DIR holds bench.<n>.txt and bench_qf.<n>.txt, one file per run.
"""

import argparse
import glob
import math
import os
import re
import statistics
import sys

NUM = re.compile(r"^-?\d+(\.\d+)?$")


def parse(path, with_size):
    """-> (header line, [(section, op, size, pbn_ns, ref_ns, ratio)])"""
    header, section, rows = "", "", []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            s = line.strip()
            if not s:
                continue
            if s.startswith("pocketbignum "):
                header = s
                continue
            if s.startswith("op "):
                continue
            m = re.match(r"^--\s*(.*?)\s*--$", s)
            if m:
                section = m.group(1)
                continue
            parts = s.split()
            need = 5 if with_size else 4
            if len(parts) < need or not parts[-1].endswith("x"):
                continue
            ratio_s, ref_s, pbn_s = parts[-1][:-1], parts[-2], parts[-3]
            if not (NUM.match(ratio_s) and NUM.match(ref_s) and NUM.match(pbn_s)):
                continue
            if with_size:
                if not parts[-4].isdigit():
                    continue
                size, op = parts[-4], " ".join(parts[:-4])
            else:
                size, op = "", " ".join(parts[:-3])
            rows.append((section, op, size, float(pbn_s), float(ref_s), float(ratio_s)))
    return header, rows


def aggregate(files, with_size):
    header, order, data = "", [], {}
    for path in files:
        h, rows = parse(path, with_size)
        header = header or h
        for section, op, size, p, r, q in rows:
            key = (section, op, size)
            if key not in data:
                order.append(key)
                data[key] = []
            data[key].append((p, r, q))
    return header, order, data


def fmt_ns(x):
    return f"{x:,.1f}" if x < 1000 else f"{x:,.0f}"


def table(title, header, order, data, with_size, ref_name):
    out = [f"### {title}", ""]
    if header:
        out += [f"`{header}`", ""]
    cols = ["op"] + (["limbs"] if with_size else []) + ["pbn ns", f"{ref_name} ns", "ratio", "ratio range"]
    out.append("| " + " | ".join(cols) + " |")
    out.append("|" + "|".join(["---"] + (["---:"] * (len(cols) - 1))) + "|")
    ratios, section = [], None
    for key in order:
        sec, op, size = key
        if sec != section:
            section = sec
            if sec:
                out.append("| **" + sec + "** |" + " |" * (len(cols) - 1))
        vals = data[key]
        p = statistics.median(v[0] for v in vals)
        r = statistics.median(v[1] for v in vals)
        qs = [v[2] for v in vals]
        q = statistics.median(qs)
        ratios.append(q)
        mark = " ✅" if q <= 1.0 else ""
        row = [op] + ([size] if with_size else []) + [
            fmt_ns(p), fmt_ns(r), f"{q:.2f}x{mark}", f"{min(qs):.2f}–{max(qs):.2f}"]
        out.append("| " + " | ".join(row) + " |")
    if ratios:
        gm = math.exp(sum(math.log(max(x, 1e-9)) for x in ratios) / len(ratios))
        faster = sum(1 for x in ratios if x <= 1.0)
        out += ["", f"Geometric mean of ratios: **{gm:.2f}x** · at parity or faster: {faster}/{len(ratios)}", ""]
    return out, ratios


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--title", default="PocketBignum benchmarks")
    ap.add_argument("--info", action="append", default=[])
    a = ap.parse_args()

    zf = sorted(glob.glob(os.path.join(a.dir, "bench.*.txt")))
    qf = sorted(glob.glob(os.path.join(a.dir, "bench_qf.*.txt")))
    if not zf and not qf:
        sys.exit(f"no benchmark outputs in {a.dir}")

    out = [f"## {a.title}", ""]
    for line in a.info:
        out.append(f"- {line}")
    runs = max(len(zf), len(qf))
    out += [f"- runs: {runs} (medians shown; ratio = pbn / reference, lower is better)", ""]

    allr = []
    if zf:
        h, o, d = aggregate(zf, True)
        t, r = table("Integers (mpz) vs GMP", h, o, d, True, "gmp")
        out += t
        allr += r
    if qf:
        h, o, d = aggregate(qf, False)
        t, r = table("Rationals & floats (mpq, mpfr) vs GMP / MPFR", h, o, d, False, "ref")
        out += t
        allr += r
    if allr:
        gm = math.exp(sum(math.log(max(x, 1e-9)) for x in allr) / len(allr))
        out += [f"**Overall geometric mean: {gm:.2f}x** over {len(allr)} benchmarks", ""]
    print("\n".join(out))


if __name__ == "__main__":
    main()
