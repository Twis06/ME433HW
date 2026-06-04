#!/usr/bin/env python3
import argparse
import csv
import sys
import time


def parse_args():
    parser = argparse.ArgumentParser(description="Trigger HW16 current-control run and plot the result.")
    parser.add_argument("port", help="Serial port, for example /dev/ttyACM0 or COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--samples", type=int, default=400, help="Number of CSV samples to collect")
    parser.add_argument("--timeout", type=float, default=10.0, help="Read timeout in seconds")
    parser.add_argument("--save", help="Optional path to save the plot image")
    return parser.parse_args()


def collect_run(args):
    try:
        import serial
    except ModuleNotFoundError as exc:
        raise SystemExit("Missing dependency: install pyserial with 'python3 -m pip install -r requirements.txt'") from exc

    rows = []
    deadline = time.monotonic() + args.timeout
    header_seen = False

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(b"a")

        while time.monotonic() < deadline and len(rows) < args.samples:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue

            if line.startswith("index,adc,"):
                header_seen = True
                continue

            if not header_seen:
                continue

            try:
                parsed = next(csv.reader([line]))
                if len(parsed) != 4:
                    continue
                rows.append(
                    {
                        "index": int(parsed[0]),
                        "adc": int(parsed[1]),
                        "desired_raw": int(parsed[2]),
                        "actual_raw": int(parsed[3]),
                    }
                )
            except (ValueError, csv.Error):
                continue

    return rows


def plot_rows(rows, save_path=None):
    try:
        import matplotlib.pyplot as plt
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Missing dependency: install matplotlib with 'python3 -m pip install -r requirements.txt'"
        ) from exc

    index = [row["index"] for row in rows]
    desired_ma = [row["desired_raw"] / 3.0 for row in rows]
    actual_ma = [row["actual_raw"] / 3.0 for row in rows]
    adc = [row["adc"] for row in rows]

    fig, ax_current = plt.subplots()
    ax_current.plot(index, desired_ma, label="desired current")
    ax_current.plot(index, actual_ma, label="actual current")
    ax_current.set_xlabel("sample")
    ax_current.set_ylabel("current (mA)")
    ax_current.grid(True)

    ax_adc = ax_current.twinx()
    ax_adc.plot(index, adc, color="tab:gray", alpha=0.35, label="ADC")
    ax_adc.set_ylabel("ADC counts")

    lines, labels = ax_current.get_legend_handles_labels()
    adc_lines, adc_labels = ax_adc.get_legend_handles_labels()
    ax_current.legend(lines + adc_lines, labels + adc_labels, loc="best")
    fig.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=160)
    plt.show()


def main():
    args = parse_args()
    rows = collect_run(args)
    if not rows:
        print("No CSV samples received. Check the serial port and that the board is flashed.", file=sys.stderr)
        return 1

    print(f"Collected {len(rows)} samples")
    plot_rows(rows, args.save)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
