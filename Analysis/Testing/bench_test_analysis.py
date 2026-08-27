#!/usr/bin/env python3
"""
Parse flight_log.bin produced by the Pico production logger
and extract all fields into named variables / lists.

Plots every major dataset as a function of its timestamps
(1 Hz XY-T01, 20 Hz suite, 100 Hz BNO086) and exports all
figures into a single multi-page PDF: data_plots.pdf

Inside if __name__ == "__main__", assign
get_latest_flight_log("<path to device>") or <file name>.bin
to log_file.
"""

import struct
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.ticker import MaxNLocator, AutoMinorLocator

import subprocess

from pathlib import Path
import re

# ----------------------------------------------------------------------
# Record definitions (must match the C++ #pragma pack(1) structs exactly)
# ----------------------------------------------------------------------
REC_BNO086  = 1
REC_SUITE   = 2
REC_XYT01_1 = 3
REC_XYT01_2 = 4

# Little-endian formats
FMT_BNO   = "<BII13f"          # 1+4+4+52 = 61 bytes
FMT_SUITE = "<BII ffHfB ff ffB" # 1+4+4 +15 +8 +9 = 41 bytes
FMT_XY    = "<BIIf"            # 1+4+4+4 = 13 bytes

SIZE_BNO   = struct.calcsize(FMT_BNO)
SIZE_SUITE = struct.calcsize(FMT_SUITE)
SIZE_XY    = struct.calcsize(FMT_XY)

# ----------------------------------------------------------------------
# Containers for all extracted data
# ----------------------------------------------------------------------
# BNO086 (100 Hz)
bno_timestamp_us = []
bno_sequence     = []
bno_ax = []; bno_ay = []; bno_az = []
bno_gx = []; bno_gy = []; bno_gz = []
bno_mx = []; bno_my = []; bno_mz = []
bno_qw = []; bno_qx = []; bno_qy = []; bno_qz = []

# 20 Hz suite (shared timestamp)
timestamps_20Hz          = []
suite_sequence           = []
MAX31865_temperature     = []
MAX31865_resistance      = []
MAX31865_raw             = []
MAX31865_ratio           = []
MAX31865_fault           = []
MS5611_pressure          = []
MS5611_temperature       = []
SHT45_temperature        = []
SHT45_humidity           = []
SHT45_crc_ok             = []

# XY-T01 units (1 Hz each)
xy1_timestamp_us = []
xy1_sequence     = []
xy1_temperature  = []

xy2_timestamp_us = []
xy2_sequence     = []
xy2_temperature  = []


# ----------------------------------------------------------------------
# Parser
# ----------------------------------------------------------------------
def parse_log(filename="flight_log.bin"):
    data = Path(filename).read_bytes()
    offset = 0
    total = len(data)

    # Shared unwrap state for the uint32 µs counter
    prev_raw_ts = None
    ts_offset   = 0
    wrap_count  = 0

    while offset + 1 <= total:
        rec_type = data[offset]

        if rec_type == REC_BNO086:
            if offset + SIZE_BNO > total:
                break
            vals = struct.unpack_from(FMT_BNO, data, offset)
            offset += SIZE_BNO

            raw_ts = vals[1]
            if prev_raw_ts is not None and raw_ts < prev_raw_ts:
                ts_offset += 1 << 32
                wrap_count += 1
            unwrapped = raw_ts + ts_offset
            prev_raw_ts = raw_ts

            bno_timestamp_us.append(unwrapped)
            bno_sequence.append(vals[2])
            bno_ax.append(vals[3]);  bno_ay.append(vals[4]);  bno_az.append(vals[5])
            bno_gx.append(vals[6]);  bno_gy.append(vals[7]);  bno_gz.append(vals[8])
            bno_mx.append(vals[9]);  bno_my.append(vals[10]); bno_mz.append(vals[11])
            bno_qw.append(vals[12]); bno_qx.append(vals[13]); bno_qy.append(vals[14]); bno_qz.append(vals[15])

        elif rec_type == REC_SUITE:
            if offset + SIZE_SUITE > total:
                break
            vals = struct.unpack_from(FMT_SUITE, data, offset)
            offset += SIZE_SUITE

            raw_ts = vals[1]
            if prev_raw_ts is not None and raw_ts < prev_raw_ts:
                ts_offset += 1 << 32
                wrap_count += 1
            unwrapped = raw_ts + ts_offset
            prev_raw_ts = raw_ts

            timestamps_20Hz.append(unwrapped)
            suite_sequence.append(vals[2])
            MAX31865_temperature.append(vals[3])
            MAX31865_resistance.append(vals[4])
            MAX31865_raw.append(vals[5])
            MAX31865_ratio.append(vals[6])
            MAX31865_fault.append(vals[7])
            MS5611_pressure.append(vals[8])
            MS5611_temperature.append(vals[9])
            SHT45_temperature.append(vals[10])
            SHT45_humidity.append(vals[11])
            SHT45_crc_ok.append(vals[12])

        elif rec_type in (REC_XYT01_1, REC_XYT01_2):
            if offset + SIZE_XY > total:
                break
            vals = struct.unpack_from(FMT_XY, data, offset)
            offset += SIZE_XY

            raw_ts = vals[1]
            if prev_raw_ts is not None and raw_ts < prev_raw_ts:
                ts_offset += 1 << 32
                wrap_count += 1
            unwrapped = raw_ts + ts_offset
            prev_raw_ts = raw_ts

            if rec_type == REC_XYT01_1:
                xy1_timestamp_us.append(unwrapped)
                xy1_sequence.append(vals[2])
                xy1_temperature.append(vals[3])
            else:
                xy2_timestamp_us.append(unwrapped)
                xy2_sequence.append(vals[2])
                xy2_temperature.append(vals[3])

        else:
            break

    print(f"Parsed records:")
    print(f"  BNO086        : {len(bno_timestamp_us):6d}")
    print(f"  Suite (20 Hz) : {len(timestamps_20Hz):6d}")
    print(f"  XY-T01 #1     : {len(xy1_timestamp_us):6d}")
    print(f"  XY-T01 #2     : {len(xy2_timestamp_us):6d}")
    print(f"Timestamp wraps detected: {wrap_count}")

    # Quick sanity check for the user
    all_ts = (bno_timestamp_us + timestamps_20Hz +
              xy1_timestamp_us + xy2_timestamp_us)
    if all_ts:
        duration_min = (max(all_ts) - min(all_ts)) / 1_000_000.0 / 60.0
        print(f"Unwrapped duration   : {duration_min:.1f} minutes")


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------
def get_global_t0():
    """Earliest timestamp across all non-empty series (microseconds)."""
    candidates = []
    for lst in (bno_timestamp_us, timestamps_20Hz, xy1_timestamp_us, xy2_timestamp_us):
        if lst:
            candidates.append(min(lst))
    return min(candidates) if candidates else 0


def to_minutes(ts_list, t0):
    """Convert list of absolute µs timestamps to relative minutes from t0."""
    return [(t - t0) / 1_000_000.0 / 60.0 for t in ts_list]


def apply_style(ax):
    """Consistent grid + tick locators for all plots."""
    ax.grid(True, which="major", alpha=0.4)
    ax.grid(True, which="minor", alpha=0.15)
    ax.xaxis.set_major_locator(MaxNLocator(nbins=10, prune=None))
    ax.xaxis.set_minor_locator(AutoMinorLocator())
    ax.yaxis.set_major_locator(MaxNLocator(nbins=8))
    ax.yaxis.set_minor_locator(AutoMinorLocator())


def save_fig(fig, pdf):
    """Save figure to the PDF and close it to free memory."""
    if pdf is not None:
        pdf.savefig(fig)
    plt.close(fig)

def get_latest_flight_log(directory) -> str:
    """
    Return the full path to the flight log file with the highest numeric prefix
    matching the pattern NNNN_flight_log.bin.
    """
    dir_path = Path(directory)
    if not dir_path.is_dir():
        raise NotADirectoryError(f"{directory} is not a valid directory")
    
    pattern = re.compile(r"^(\d+)_flight_log\.bin$")
    candidates = []
    
    for file_path in dir_path.iterdir():
        if file_path.is_file():
            match = pattern.match(file_path.name)
            if match:
                candidates.append((int(match.group(1)), file_path))
    
    if not candidates:
        raise FileNotFoundError(f"No matching flight log files found in {directory}")
    
    # Select the file that has the largest number
    latest_path = max(candidates, key=lambda x: x[0])[1]
    return str(latest_path)

# ----------------------------------------------------------------------
# Plot functions (each produces one or more PDF pages)
# ----------------------------------------------------------------------
def plot_xy_temperatures(pdf, t0):
    if not xy1_timestamp_us and not xy2_timestamp_us:
        print("No XY-T01 data found — skipping.")
        return

    fig, ax = plt.subplots(figsize=(12, 5))

    if xy1_timestamp_us:
        t1 = to_minutes(xy1_timestamp_us, t0)
        ax.plot(t1, xy1_temperature, label="XY-T01 #1", linewidth=1.4)
    if xy2_timestamp_us:
        t2 = to_minutes(xy2_timestamp_us, t0)
        ax.plot(t2, xy2_temperature, label="XY-T01 #2", linewidth=1.4)

    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Temperature (°C)")
    ax.set_title("XY-T01 Temperature vs Time (1 Hz)")
    ax.legend()
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_max31865_temperature(pdf, t0):
    if not timestamps_20Hz or not MAX31865_temperature:
        print("No MAX31865 temperature data — skipping.")
        return

    t = to_minutes(timestamps_20Hz, t0)
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, MAX31865_temperature, color="tab:red", linewidth=1.3, label="MAX31865")
    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Temperature (°C)")
    ax.set_title("MAX31865 RTD Temperature vs Time (20 Hz)")
    ax.legend()
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_ms5611(pdf, t0):
    """MS5611 pressure (top) + temperature (bottom) sharing the x-axis."""
    if not timestamps_20Hz or not MS5611_pressure:
        print("No MS5611 data — skipping.")
        return

    t = to_minutes(timestamps_20Hz, t0)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 7), sharex=True)

    ax1.plot(t, MS5611_pressure, color="tab:green", linewidth=1.3, label="MS5611 Pressure")
    ax1.set_ylabel("Pressure (hPa / mbar)")
    ax1.set_title("GY-63 / MS5611 Pressure & Temperature vs Time (20 Hz)")
    ax1.legend(loc="upper right")
    apply_style(ax1)

    ax2.plot(t, MS5611_temperature, color="tab:orange", linewidth=1.3, label="MS5611 Temperature")
    ax2.set_xlabel("Time (minutes)")
    ax2.set_ylabel("Temperature (°C)")
    ax2.legend(loc="upper right")
    apply_style(ax2)

    fig.tight_layout()
    save_fig(fig, pdf)


def plot_sht45(pdf, t0):
    """SHT45 temperature (top) + humidity (bottom) sharing the x-axis."""
    if not timestamps_20Hz or not SHT45_temperature:
        print("No SHT45 data — skipping.")
        return

    t = to_minutes(timestamps_20Hz, t0)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 7), sharex=True)

    ax1.plot(t, SHT45_temperature, color="tab:cyan", linewidth=1.3, label="SHT45 Temperature")
    ax1.set_ylabel("Temperature (°C)")
    ax1.set_title("SHT45 Temperature & Humidity vs Time (20 Hz)")
    ax1.legend(loc="upper right")
    apply_style(ax1)

    ax2.plot(t, SHT45_humidity, color="tab:purple", linewidth=1.3, label="SHT45 Humidity")
    ax2.set_xlabel("Time (minutes)")
    ax2.set_ylabel("Relative Humidity (%)")
    ax2.legend(loc="upper right")
    apply_style(ax2)

    fig.tight_layout()
    save_fig(fig, pdf)


def plot_bno_accel(pdf, t0):
    if not bno_timestamp_us:
        print("No BNO086 accelerometer data — skipping.")
        return

    t = to_minutes(bno_timestamp_us, t0)
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, bno_ax, label="ax", linewidth=1.0)
    ax.plot(t, bno_ay, label="ay", linewidth=1.0)
    ax.plot(t, bno_az, label="az", linewidth=1.0)
    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Acceleration")
    ax.set_title("BNO086 Accelerometer vs Time (100 Hz)")
    ax.legend()
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_bno_gyro(pdf, t0):
    if not bno_timestamp_us:
        print("No BNO086 gyroscope data — skipping.")
        return

    t = to_minutes(bno_timestamp_us, t0)
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, bno_gx, label="gx", linewidth=1.0)
    ax.plot(t, bno_gy, label="gy", linewidth=1.0)
    ax.plot(t, bno_gz, label="gz", linewidth=1.0)
    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Angular rate")
    ax.set_title("BNO086 Gyroscope vs Time (100 Hz)")
    ax.legend()
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_bno_mag(pdf, t0):
    if not bno_timestamp_us:
        print("No BNO086 magnetometer data — skipping.")
        return

    t = to_minutes(bno_timestamp_us, t0)
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, bno_mx, label="mx", linewidth=1.0)
    ax.plot(t, bno_my, label="my", linewidth=1.0)
    ax.plot(t, bno_mz, label="mz", linewidth=1.0)
    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Magnetic field")
    ax.set_title("BNO086 Magnetometer vs Time (100 Hz)")
    ax.legend()
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_bno_quat(pdf, t0):
    if not bno_timestamp_us:
        print("No BNO086 quaternion data — skipping.")
        return

    t = to_minutes(bno_timestamp_us, t0)
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, bno_qw, label="qw", linewidth=1.0)
    ax.plot(t, bno_qx, label="qx", linewidth=1.0)
    ax.plot(t, bno_qy, label="qy", linewidth=1.0)
    ax.plot(t, bno_qz, label="qz", linewidth=1.0)
    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Quaternion component")
    ax.set_title("BNO086 Quaternion vs Time (100 Hz)")
    ax.legend()
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_all_temperatures(pdf, t0):
    """Overlay every temperature channel for a quick thermal overview."""
    has_any = (
        xy1_timestamp_us or xy2_timestamp_us or
        (timestamps_20Hz and (MAX31865_temperature or MS5611_temperature or SHT45_temperature))
    )
    if not has_any:
        print("No temperature data found — skipping all-temperatures plot.")
        return

    fig, ax = plt.subplots(figsize=(12, 6))

    if xy1_timestamp_us:
        t = to_minutes(xy1_timestamp_us, t0)
        ax.plot(t, xy1_temperature, label="XY-T01 #1 (1 Hz)", linewidth=1.6, color="tab:blue")
    if xy2_timestamp_us:
        t = to_minutes(xy2_timestamp_us, t0)
        ax.plot(t, xy2_temperature, label="XY-T01 #2 (1 Hz)", linewidth=1.6, color="tab:orange")
    if timestamps_20Hz and MAX31865_temperature:
        t = to_minutes(timestamps_20Hz, t0)
        ax.plot(t, MAX31865_temperature, label="MAX31865 RTD (20 Hz)", linewidth=1.1, color="tab:red", alpha=0.85)
    if timestamps_20Hz and MS5611_temperature:
        t = to_minutes(timestamps_20Hz, t0)
        ax.plot(t, MS5611_temperature, label="MS5611 (20 Hz)", linewidth=1.1, color="tab:green", alpha=0.85)
    if timestamps_20Hz and SHT45_temperature:
        t = to_minutes(timestamps_20Hz, t0)
        ax.plot(t, SHT45_temperature, label="SHT45 (20 Hz)", linewidth=1.1, color="tab:cyan", alpha=0.85)

    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Temperature (°C)")
    ax.set_title("All Temperature Channels vs Time")
    ax.legend(loc="best", fontsize=9)
    apply_style(ax)
    fig.tight_layout()
    save_fig(fig, pdf)


def plot_max31865_details(pdf, t0):
    """MAX31865 resistance + ratio on twin y-axes; annotate if faults present."""
    if not timestamps_20Hz or not (MAX31865_resistance or MAX31865_ratio):
        print("No MAX31865 resistance/ratio data — skipping.")
        return

    t = to_minutes(timestamps_20Hz, t0)
    fig, ax1 = plt.subplots(figsize=(12, 5))

    color_res = "tab:brown"
    if MAX31865_resistance:
        ax1.plot(t, MAX31865_resistance, color=color_res, linewidth=1.2, label="Resistance")
        ax1.set_ylabel("Resistance (Ω)", color=color_res)
        ax1.tick_params(axis="y", labelcolor=color_res)

    ax2 = ax1.twinx()
    color_ratio = "tab:purple"
    if MAX31865_ratio:
        ax2.plot(t, MAX31865_ratio, color=color_ratio, linewidth=1.2, label="Ratio", alpha=0.8)
        ax2.set_ylabel("Ratio", color=color_ratio)
        ax2.tick_params(axis="y", labelcolor=color_ratio)

    ax1.set_xlabel("Time (minutes)")

    # Fault summary in title if any non-zero faults
    n_faults = sum(1 for f in MAX31865_fault if f) if MAX31865_fault else 0
    title = "MAX31865 Resistance & Ratio vs Time (20 Hz)"
    if n_faults:
        title += f"  —  {n_faults} fault flag(s) set"
    ax1.set_title(title)

    # Combined legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="best")

    apply_style(ax1)
    # Don't apply full style to twin to avoid conflicting locators; just grid on primary
    ax1.grid(True, which="major", alpha=0.4)
    ax1.grid(True, which="minor", alpha=0.15)

    fig.tight_layout()
    save_fig(fig, pdf)


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------
if __name__ == "__main__":
    # log_file = get_latest_flight_log("<path to device>") # If the file is on an external device.
    log_file = "0000_flight_log.bin" # If the file is in your working directory.
    print(f"""
         Plotting data from {log_file}...
         """)
    if not Path(log_file).is_file():
        print(f"WARNING: '{log_file}' not found — producing empty lists / empty PDF.")
        # Leave containers empty so the rest of the pipeline still runs cleanly
    else:
        parse_log(log_file)

    t0 = get_global_t0()
    print(f"Global t0 (µs): {t0}")

    pdf_path = "data_plots.pdf"
    with PdfPages(pdf_path) as pdf:
        # Overview first
        plot_all_temperatures(pdf, t0)
        # Detailed sensor groups
        plot_ms5611(pdf, t0)                 # pressure + temperature dual
        plot_sht45(pdf, t0)                  # temperature + humidity dual
        plot_max31865_details(pdf, t0)       # resistance + ratio twin
        # High-rate IMU
        plot_bno_accel(pdf, t0)
        plot_bno_gyro(pdf, t0)
        plot_bno_mag(pdf, t0)