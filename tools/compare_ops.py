#!/usr/bin/env python3
"""
Joins the outputs of tests/bench_ops.c and tests/bench_ops.rb (or any two
programs printing "<op> <limbs> <ns>") into one table.

    tools/compare_ops.py PBN_OUTPUT OTHER_OUTPUT [--names pbn,ruby] [--markdown]

ratio = pbn time / other time (lower is better, 1.00 = parity)
"""

import argparse
import sys


def load(path):
    rows, order = {}, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if len(p) != 3:
                continue
            key = (p[0], int(p[1]))
            if key not in rows:
                order.append(key)
            rows[key] = float(p[2])
    return rows, order


def fmt(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.2f} µs"
    return f"{ns:.0f} ns"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pbn")
    ap.add_argument("other")
    ap.add_argument("--names", default="pbn,ruby")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    n1, n2 = a.names.split(",")
    r1, order = load(a.pbn)
    r2, _ = load(a.other)

    if a.markdown:
        print(f"| op | limbs | {n1} | {n2} | ratio |")
        print("|---|---:|---:|---:|---:|")
    else:
        print(f"{'op':10s} {'limbs':>6s} {n1:>12s} {n2:>12s} {'ratio':>8s}")
    for key in order:
        if key not in r2:
            continue
        t1, t2 = r1[key], r2[key]
        q = t1 / t2 if t2 else float("inf")
        if a.markdown:
            mark = " ✅" if q <= 1.0 else ""
            print(f"| {key[0]} | {key[1]} | {fmt(t1)} | {fmt(t2)} | {q:.2f}x{mark} |")
        else:
            print(f"{key[0]:10s} {key[1]:6d} {fmt(t1):>12s} {fmt(t2):>12s} {q:7.2f}x")


if __name__ == "__main__":
    sys.exit(main())
