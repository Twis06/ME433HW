#!/usr/bin/env python3
import argparse
import csv
import time

import serial


def parse_response(text):
    messages = []
    rows = []
    header_seen = False

    for line in text.splitlines():
        if line.startswith("index,adc,"):
            header_seen = True
            continue
        if header_seen:
            try:
                parsed = next(csv.reader([line]))
            except csv.Error:
                messages.append(line)
                continue
            if len(parsed) == 4:
                try:
                    rows.append(
                        {
                            "index": int(parsed[0]),
                            "adc": int(parsed[1]),
                            "desired_raw": int(parsed[2]),
                            "actual_raw": int(parsed[3]),
                        }
                    )
                    continue
                except ValueError:
                    pass
        messages.append(line)

    return messages, rows


def print_summary(text, stride, save_csv):
    messages, rows = parse_response(text)

    for message in messages:
        if message:
            print(message)

    if not rows:
        return

    if save_csv:
        with open(save_csv, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["index", "adc", "desired_current_thirds_ma", "actual_current_thirds_ma"])
            for row in rows:
                writer.writerow([row["index"], row["adc"], row["desired_raw"], row["actual_raw"]])
        print(f"saved_csv={save_csv}")

    adc_values = [row["adc"] for row in rows]
    desired_values = [row["desired_raw"] for row in rows]
    actual_values = [row["actual_raw"] for row in rows]

    print()
    print("Readable samples: current values are mA; position is raw ADC counts")
    print("  ms   position   desired_mA   actual_mA")
    for row in rows[::stride]:
        print(
            f"{row['index']:4d} {row['adc']:10d}"
            f" {row['desired_raw'] / 3.0:12.1f} {row['actual_raw'] / 3.0:11.1f}"
        )

    if ((len(rows) - 1) % stride) != 0:
        row = rows[-1]
        print(
            f"{row['index']:4d} {row['adc']:10d}"
            f" {row['desired_raw'] / 3.0:12.1f} {row['actual_raw'] / 3.0:11.1f}"
        )

    print()
    print(
        "Summary: "
        f"samples={len(rows)}, "
        f"duration_ms={rows[-1]['index'] + 1}, "
        f"position={adc_values[0]}->{adc_values[-1]}, "
        f"position_range={min(adc_values)}..{max(adc_values)}, "
        f"desired_mA={min(desired_values) / 3.0:.1f}..{max(desired_values) / 3.0:.1f}, "
        f"actual_mA={min(actual_values) / 3.0:.1f}..{max(actual_values) / 3.0:.1f}"
    )


def main():
    parser = argparse.ArgumentParser(description="Send the HW16 current-control trigger and print the response.")
    parser.add_argument("command", nargs="?", default="a", help="Command character to send, default: a")
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--read-seconds", type=float, default=1.5)
    parser.add_argument("--raw", action="store_true", help="Print raw device output")
    parser.add_argument("--stride", type=int, default=50, help="Readable table sample stride")
    parser.add_argument("--save-csv", help="Save full CSV response to this path")
    args = parser.parse_args()

    if len(args.command) != 1:
        raise SystemExit("command must be exactly one character")

    with serial.Serial(args.port, args.baud, timeout=0.1) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(args.command.encode("ascii"))

        chunks = []
        deadline = time.monotonic() + args.read_seconds
        while time.monotonic() < deadline:
            data = ser.read(256)
            if data:
                chunks.append(data.decode("utf-8", errors="replace"))

        text = "".join(chunks)
        if args.raw:
            print(text, end="")
        else:
            print_summary(text, max(args.stride, 1), args.save_csv)


if __name__ == "__main__":
    main()
