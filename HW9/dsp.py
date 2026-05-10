import csv

import matplotlib.pyplot as plt
import numpy as np


with open("data/sigA.csv", "r") as f:
    reader = csv.reader(f)
    data = np.array(list(reader), dtype=float)
t1 = data[:, 0]
y1 = data[:, 1]

with open("data/sigB.csv", "r") as f:
    reader = csv.reader(f)
    data = np.array(list(reader), dtype=float)
t2 = data[:, 0]
y2 = data[:, 1]

with open("data/sigC.csv", "r") as f:
    reader = csv.reader(f)
    data = np.array(list(reader), dtype=float)
t3 = data[:, 0]
y3 = data[:, 1]

with open("data/sigD.csv", "r") as f:
    reader = csv.reader(f)
    data = np.array(list(reader), dtype=float)
t4 = data[:, 0]
y4 = data[:, 1]

signals = [
    ("sigA", t1, y1),
    ("sigB", t2, y2),
    ("sigC", t3, y3),
    ("sigD", t4, y4),
]

for name, t, y in signals:
    dt = t[1] - t[0]
    Fs = 1.0 / dt
    n = len(y)
    k = np.arange(n)
    T = n / Fs
    frq = k / T
    frq = frq[range(int(n / 2))]
    Y = np.fft.fft(y) / n
    Y = Y[range(int(n / 2))]
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))

    ax1.plot(t, y, "b")
    ax1.set_title(f"{name}: Signal vs Time")
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Amplitude")

    ax2.loglog(frq[1:], abs(Y[1:]))
    ax2.set_title(f"{name}: FFT")
    ax2.set_xlabel("Freq (Hz)")
    ax2.set_ylabel("|Y(freq)|")
    plt.tight_layout()
    plt.savefig(f"signal_and_fft_{name}.png", dpi=300, bbox_inches="tight")
    plt.close(fig)


# moving average function
def moving_average(signal, window_size):
    if window_size <= 0:
        raise ValueError("window_size must be positive")
    if window_size > len(signal):
        raise ValueError("window_size cannot exceed signal length")

    output = []
    for i in range(len(signal) - window_size + 1):
        window = signal[i : i + window_size]
        mean = np.mean(window)
        output.append(mean)
    return np.array(output)


# for signalA, try different window sizes X
window_sizes = [100, 2000, 5000, 8000]

# Plot all smoothed versions on one figure
plt.figure(figsize=(10, 6))
for window_size in window_sizes:
    smoothed_signal = moving_average(y1, window_size)
    plt.plot(t1[window_size - 1 :], smoothed_signal, label=f"Window Size: {window_size}")
plt.title("Signal A Smoothed with Moving Average (Various Window Sizes)")
plt.xlabel("Time (s)")
plt.ylabel("Amplitude")
plt.legend()
plt.grid()
plt.tight_layout()
plt.savefig("signalA_smoothed_all_windows.png", dpi=300, bbox_inches="tight")
plt.close()


# window size = 2000. unfiltered data in black, the filtered in red, and the number of data points averaged in the title, as well as your code.

window_size = 2000
smoothed_signal = moving_average(y1, window_size)
plt.figure(figsize=(10, 6))
plt.plot(t1, y1, "k", label="Unfiltered Signal")
plt.plot(t1[window_size - 1 :], smoothed_signal, "r", label=f"Smoothed Signal (Window Size: {window_size})")
plt.title(f"Signal A Smoothed with Moving Average (Window Size: {window_size})")
plt.xlabel("Time (s)")
plt.ylabel("Amplitude")
plt.legend()
plt.grid()
plt.tight_layout()
plt.savefig("signalA_smoothed_window_2000.png", dpi=300, bbox_inches="tight")
plt.close()

for name, t, y in signals:
    smoothed_signal = moving_average(y, window_size)
    plt.figure(figsize=(10, 6))
    plt.plot(t, y, "k", label="Unfiltered Signal")
    plt.plot(t[window_size - 1 :], smoothed_signal, "r", label="Filtered Signal")
    plt.title(f"{name} Moving Average Filter ({window_size} Data Points Averaged)")
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude")
    plt.legend()
    plt.grid()
    plt.tight_layout()
    plt.savefig(f"{name}_maf_{window_size}.png", dpi=300, bbox_inches="tight")
    plt.close()

# low-pass filter with IIR 

# new_average[i] = A * new_average[i-1] + B * signal[i].

def low_pass_filter(signal, A, B):
    if (A < 0 or A > 1) or (B < 0 or B > 1):
        raise ValueError("A and B must be between 0 and 1")
    if not np.isclose(A + B, 1.0):
        raise ValueError("A and B must sum to 1")
    output = np.zeros_like(signal)
    for i in range(len(signal)):
        if i == 0:
            output[i] = signal[i]  # Initialize with the first value
        else:
            output[i] = A * output[i - 1] + B * signal[i]
    return output


# IIR low-pass plots for each CSV
iir_points_averaged = 2000
A = (iir_points_averaged - 1) / iir_points_averaged
B = 1 / iir_points_averaged

for name, t, y in signals:
    filtered_signal = low_pass_filter(y, A, B)
    plt.figure(figsize=(10, 6))
    plt.plot(t, y, "k", label="Unfiltered Signal")
    plt.plot(t, filtered_signal, "r", label="Filtered Signal")
    plt.title(
        f"{name} IIR Low-Pass Filter (A={iir_points_averaged - 1}/{iir_points_averaged}, "
        f"B=1/{iir_points_averaged})"
    )
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude")
    plt.legend()
    plt.grid()
    plt.tight_layout()
    plt.savefig(f"{name}_iir_lowpass_{iir_points_averaged}.png", dpi=300, bbox_inches="tight")
    plt.close()

# FIR filter 
def create_fir_weights(Fs, cutoff_hz, bandwidth_hz, window_type):
    if cutoff_hz <= 0 or cutoff_hz >= Fs / 2:
        raise ValueError("cutoff_hz must be between 0 and the Nyquist frequency")
    if bandwidth_hz <= 0:
        raise ValueError("bandwidth_hz must be positive")

    taps_estimate = int(np.ceil(2 * Fs / bandwidth_hz))
        
    if taps_estimate % 2 == 0:
        taps_estimate += 1

    n = np.arange(taps_estimate) - (taps_estimate - 1) / 2
    weights = 2 * cutoff_hz / Fs * np.sinc(2 * cutoff_hz * n / Fs)

    window = np.ones(taps_estimate)

    weights = weights * window
    weights = weights / np.sum(weights)
    return weights


def fir_filter(signal, weights):
    return np.convolve(signal, weights, mode="same")


def signal_fft(t, y):
    dt = t[1] - t[0]
    n = len(y)
    frq = np.fft.rfftfreq(n, d=dt)
    Y = np.abs(np.fft.rfft(y) / n)
    return frq, Y


# try a few FIR low-pass settings on signal A
fir_trials = [
    {"cutoff_hz": 35, "bandwidth_hz": 15, "window_type": "hann"},
    {"cutoff_hz": 40, "bandwidth_hz": 20, "window_type": "hamming"},
    {"cutoff_hz": 50, "bandwidth_hz": 20, "window_type": "blackman"},
]

Fs1 = 1.0 / (t1[1] - t1[0])
for trial in fir_trials:
    cutoff_hz = trial["cutoff_hz"]
    bandwidth_hz = trial["bandwidth_hz"]
    window_type = trial["window_type"]
    weights = create_fir_weights(Fs1, cutoff_hz, bandwidth_hz, window_type)
    filtered_signal = fir_filter(y1, weights)
    frq_unfiltered, Y_unfiltered = signal_fft(t1, y1)
    frq_filtered, Y_filtered = signal_fft(t1, filtered_signal)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    ax1.plot(t1, y1, "k", label="Unfiltered Signal")
    ax1.plot(t1, filtered_signal, "r", label="Filtered Signal")
    ax1.set_title(
        f"sigA FIR Trial ({len(weights)} weights, {window_type}, "
        f"fc={cutoff_hz} Hz, bw={bandwidth_hz} Hz)"
    )
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Amplitude")
    ax1.legend()
    ax1.grid()

    ax2.loglog(frq_unfiltered[1:], Y_unfiltered[1:], "k", label="Unfiltered FFT")
    ax2.loglog(frq_filtered[1:], Y_filtered[1:], "r", label="Filtered FFT")
    ax2.set_xlabel("Freq (Hz)")
    ax2.set_ylabel("|Y(freq)|")
    ax2.legend()
    ax2.grid()

    plt.tight_layout()
    plt.savefig(
        f"sigA_fir_trial_{window_type}_{cutoff_hz}Hz_{bandwidth_hz}Hz.png",
        dpi=300,
        bbox_inches="tight",
    )
    plt.close(fig)


# chosen FIR low-pass design for all CSVs
fir_cutoff_hz = 40
fir_bandwidth_hz = 20
fir_window_type = "hamming"

for name, t, y in signals:
    Fs = 1.0 / (t[1] - t[0])
    weights = create_fir_weights(Fs, fir_cutoff_hz, fir_bandwidth_hz, fir_window_type)
    filtered_signal = fir_filter(y, weights)
    frq_unfiltered, Y_unfiltered = signal_fft(t, y)
    frq_filtered, Y_filtered = signal_fft(t, filtered_signal)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    ax1.plot(t, y, "k", label="Unfiltered Signal")
    ax1.plot(t, filtered_signal, "r", label="Filtered Signal")
    ax1.set_title(
        f"{name} FIR Low-Pass ({len(weights)} weights, {fir_window_type}, "
        f"fc={fir_cutoff_hz} Hz, bw={fir_bandwidth_hz} Hz)"
    )
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Amplitude")
    ax1.legend()
    ax1.grid()

    ax2.loglog(frq_unfiltered[1:], Y_unfiltered[1:], "k", label="Unfiltered FFT")
    ax2.loglog(frq_filtered[1:], Y_filtered[1:], "r", label="Filtered FFT")
    ax2.set_xlabel("Freq (Hz)")
    ax2.set_ylabel("|Y(freq)|")
    ax2.legend()
    ax2.grid()

    plt.tight_layout()
    plt.savefig(f"{name}_fir_lowpass.png", dpi=300, bbox_inches="tight")
    plt.close(fig)
