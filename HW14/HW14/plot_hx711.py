#!/usr/bin/env python3
import argparse
import sys
import time
from pathlib import Path


def pick_port(requested_port):
    if requested_port:
        return requested_port

    try:
        from serial.tools import list_ports
    except ImportError:
        print("pyserial is not installed. Run: uv sync")
        sys.exit(1)

    ports = list(list_ports.comports())
    pico_ports = []
    for port in ports:
        description = port.description or ""
        manufacturer = port.manufacturer or ""
        if (
            "Pico" in description
            or "Board CDC" in description
            or "USB Serial" in description
            or "Raspberry Pi" in manufacturer
        ):
            pico_ports.append(port.device)

    if len(pico_ports) == 1:
        return pico_ports[0]

    if ports:
        print("Available serial ports:")
        for port in ports:
            print(f"  {port.device}: {port.description}")
    else:
        print("No serial ports found.")

    print("Pass the Pico port explicitly, for example: uv run plot_hx711.py --port /dev/tty.usbmodemXXXX")
    sys.exit(1)


def collect_samples(port, baud, samples, ready_timeout):
    try:
        import serial
    except ImportError:
        print("pyserial is not installed. Run: uv sync")
        sys.exit(1)

    rows = []

    print(f"Opening {port}")
    print(f"Requested {samples} samples. Expected collection time is about {samples / 80.0:.1f}s at 80 Hz or {samples / 10.0:.1f}s at 10 Hz.")
    with serial.Serial(port, baud, timeout=0.2, write_timeout=1) as ser:
        ser.dtr = True
        ser.rts = True
        ser.reset_input_buffer()
        print("Waiting for Pico prompt")

        deadline = time.monotonic() + ready_timeout
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print(line)
            if "Enter sample count" in line:
                break

        print(f"Requesting {samples} samples")
        ser.write(f"{samples}\n".encode("ascii"))
        ser.flush()

        in_csv = False
        deadline = time.monotonic() + max(10.0, samples * 0.05)
        while True:
            if time.monotonic() > deadline:
                print("Timed out waiting for data from Pico.")
                print("If the Pico received the count but never prints BEGIN, HX711 DT is probably stuck high or the board is not flashed with the latest UF2.")
                sys.exit(1)

            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            if line.startswith("BEGIN"):
                print(line)
                in_csv = True
                continue
            if line == "time_ms,raw,filtered":
                continue
            if line == "END":
                break
            if not in_csv:
                continue

            parts = line.split(",")
            if len(parts) != 3:
                continue

            try:
                rows.append((int(parts[0]), int(parts[1]), int(parts[2])))
            except ValueError:
                continue

            if len(rows) % 100 == 0:
                print(f"Read {len(rows)} samples")

    if not rows:
        print("No data received from Pico.")
        sys.exit(1)

    return rows


def save_csv(rows, csv_path):
    with csv_path.open("w", encoding="utf-8") as csv_file:
        csv_file.write("time_ms,raw,filtered\n")
        for time_ms, raw, filtered in rows:
            csv_file.write(f"{time_ms},{raw},{filtered}\n")


def plot_results(rows, output_dir, show):
    try:
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("Missing plotting dependency. Run: uv sync")
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    data = np.array(rows, dtype=float)
    time_ms = data[:, 0]
    raw = data[:, 1]
    filtered = data[:, 2]

    time_s = (time_ms - time_ms[0]) / 1000.0
    sample_period_s = np.median(np.diff(time_s))
    sample_rate_hz = 1.0 / sample_period_s
    nyquist_hz = sample_rate_hz / 2.0

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(time_s, raw, label="Raw", linewidth=1.0)
    ax.plot(time_s, filtered, label="IIR filtered", linewidth=1.5)
    ax.set_title(f"HX711 Force Sensor Data, fs={sample_rate_hz:.1f} Hz")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("HX711 raw count")
    ax.grid(True)
    ax.legend()
    fig.tight_layout()
    data_png = output_dir / "hx711_data.png"
    fig.savefig(data_png, dpi=200)

    raw_ac = raw - np.mean(raw)
    filtered_ac = filtered - np.mean(filtered)
    freqs = np.fft.rfftfreq(len(raw_ac), d=sample_period_s)
    raw_fft = np.abs(np.fft.rfft(raw_ac)) / len(raw_ac)
    filtered_fft = np.abs(np.fft.rfft(filtered_ac)) / len(filtered_ac)

    fig_fft, ax_fft = plt.subplots(figsize=(10, 5))
    ax_fft.plot(freqs, raw_fft, label="Raw FFT", linewidth=1.0)
    ax_fft.plot(freqs, filtered_fft, label="IIR filtered FFT", linewidth=1.5)
    ax_fft.set_title(f"HX711 FFT, Nyquist={nyquist_hz:.1f} Hz")
    ax_fft.set_xlabel("Frequency (Hz)")
    ax_fft.set_ylabel("Magnitude")
    ax_fft.set_xlim(0, nyquist_hz)
    ax_fft.grid(True)
    ax_fft.legend()
    fig_fft.tight_layout()
    fft_png = output_dir / "hx711_fft.png"
    fig_fft.savefig(fft_png, dpi=200)

    csv_path = output_dir / "hx711_data.csv"
    save_csv(rows, csv_path)

    print(f"Sample rate: {sample_rate_hz:.2f} Hz")
    print(f"Nyquist frequency: {nyquist_hz:.2f} Hz")
    print(f"Saved {data_png}")
    print(f"Saved {fft_png}")
    print(f"Saved {csv_path}")

    if show:
        plt.show()
    else:
        plt.close("all")


def main():
    parser = argparse.ArgumentParser(description="Collect HX711 data from the Pico and plot data plus FFT.")
    parser.add_argument("-n", "--samples", type=int, default=256, help="Number of samples to collect.")
    parser.add_argument("--port", help="Serial port, for example /dev/tty.usbmodemXXXX")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate. USB CDC ignores this, but pyserial requires it.")
    parser.add_argument("--output-dir", type=Path, default=Path("plots"), help="Directory for PNG and CSV outputs.")
    parser.add_argument("--ready-timeout", type=float, default=3.0, help="Seconds to wait for the Pico prompt before sending.")
    parser.add_argument("--show", action="store_true", help="Show the plots after saving them.")
    args = parser.parse_args()

    if args.samples < 2:
        print("Collect at least 2 samples.")
        sys.exit(1)

    port = pick_port(args.port)
    rows = collect_samples(port, args.baud, args.samples, args.ready_timeout)
    plot_results(rows, args.output_dir, args.show)


if __name__ == "__main__":
    main()
