#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def read_csv(path):
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))

    time_ms = np.array([float(r["time_ms"]) for r in rows])
    raw = np.array([float(r["raw"]) for r in rows])
    return (time_ms - time_ms[0]) / 1000.0, raw


def high_pass(raw, fs, cutoff_hz):
    dt = 1.0 / fs
    tau = 1.0 / (2.0 * np.pi * cutoff_hz)
    alpha = dt / (tau + dt)

    baseline = np.empty_like(raw)
    baseline[0] = raw[0]
    for i in range(1, len(raw)):
        baseline[i] = baseline[i - 1] + alpha * (raw[i] - baseline[i - 1])

    return raw - baseline


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=Path, default=Path("plots/hx711_data.csv"))
    parser.add_argument("--cutoff", type=float, default=1.0)
    parser.add_argument("--output", type=Path, default=Path("plots/hx711_highpass.png"))
    args = parser.parse_args()

    time_s, raw = read_csv(args.csv)
    fs = 1.0 / np.median(np.diff(time_s))
    filtered = high_pass(raw, fs, args.cutoff)
    filtered -= np.mean(filtered)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    plt.figure(figsize=(10, 5))
    plt.plot(time_s, raw - np.mean(raw), label="raw, centered", alpha=0.45)
    plt.plot(time_s, filtered, label=f"high-pass {args.cutoff:g} Hz")
    plt.xlabel("Time (s)")
    plt.ylabel("HX711 counts")
    plt.title("HX711 High-Pass Filtered Data")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(args.output, dpi=200)

    print(f"fs = {fs:.2f} Hz")
    print(f"raw std = {np.std(raw, ddof=1):.1f} counts")
    print(f"filtered std = {np.std(filtered, ddof=1):.1f} counts")
    print(f"saved {args.output}")


if __name__ == "__main__":
    main()
