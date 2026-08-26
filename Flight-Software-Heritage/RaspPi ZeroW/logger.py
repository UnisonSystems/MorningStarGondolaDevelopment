#!/usr/bin/python3
#   Headless multi-device chronoamperometry logger
#   Based on the CVGIT project (Juan Aznar Poveda, Technical University of Cartagena)
# -*- coding: utf-8 -*-
#
# Holds every device at a constant +0.5 V and logs current over time. Twelve
# devices share one LMP91000 front end; a CD74HC4067 analog mux (driven through
# an MCP23017 over I2C) selects which device the transimpedance amplifier reads.
# One full 12-device scan per second, one raw ADC read per device, one CSV row
# per scan. No GUI, no plotting. Onboard LED blinks to show it is running.
#
# Tunable constants live at the top here and in var.py (bias, gain, addresses).

import os
import sys
import csv
import time
import signal
from datetime import datetime

from var import *
from settings import (init, readadc, raw_to_current, device_ohms,
                      mux_init, mux_select)

# ---------------------------------------------------------------------------
# Tunable run parameters
# ---------------------------------------------------------------------------
SCAN_INTERVAL = 1.0        # seconds between the START of each 12-device scan
SETTLE_S = 0.003           # pause after switching mux channel before reading (s)
AVG_WINDOW_S = 0.075       # per device: read back-to-back for this long, then average
#   Time-bounded averaging. For each device we settle, then read the ADC as fast
#   as the hardware allows for AVG_WINDOW_S and average ALL the reads taken. This
#   self-adapts to the board: however many reads fit, we use them (expect tens to
#   a couple hundred on a Zero W). The ~75 ms window spans several 50/60 Hz mains
#   cycles, so it averages out hum as well as fast ADC noise; the read COUNT within
#   it beats down random noise by ~sqrt(N). Budget: (SETTLE_S + AVG_WINDOW_S) x 12
#   ~= 0.94 s, leaving margin inside the 1 s interval. Raise AVG_WINDOW_S toward
#   ~0.08 to use more of the second; lower it if you want a faster scan cadence.

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Results")

# Terminal display (does not affect the CSV file). Prints an aligned table of raw
# values each scan so you can eyeball which channel reads what. Set PRINT_TO_TERMINAL
# False for silent running (e.g. under systemd).
PRINT_TO_TERMINAL = True
TERM_HEADER_EVERY = 20     # reprint the column header every N scans

# ---------------------------------------------------------------------------
# Onboard activity LED ("running" heartbeat). Cosmetic: every LED access is
# guarded so a wrong path or a permission issue can never stop the measurement.
# If the LED behaves backwards on your board (on when it should be off), swap
# LED_ON and LED_OFF below.
# ---------------------------------------------------------------------------
LED_ON = "1"
LED_OFF = "0"


class ActivityLED:
    def __init__(self):
        self.path = None
        self.saved_trigger = None
        for cand in ("/sys/class/leds/led0", "/sys/class/leds/ACT"):
            if os.path.isdir(cand):
                self.path = cand
                break
        if self.path:
            try:
                # Remember the current trigger so we can restore it on exit,
                # then take manual control of the LED.
                with open(self.path + "/trigger") as fh:
                    text = fh.read()
                # the active trigger is shown in [brackets]
                self.saved_trigger = "mmc0"
                for tok in text.split():
                    if tok.startswith("[") and tok.endswith("]"):
                        self.saved_trigger = tok[1:-1]
                self._write("trigger", "none")
            except Exception:
                self.path = None  # give up quietly; LED is non-essential

    def _write(self, node, value):
        try:
            with open(self.path + "/" + node, "w") as fh:
                fh.write(value)
        except Exception:
            pass

    def set(self, on):
        if self.path:
            self._write("brightness", LED_ON if on else LED_OFF)

    def restore(self):
        if self.path and self.saved_trigger:
            self._write("trigger", self.saved_trigger)


# ---------------------------------------------------------------------------
# Clean-shutdown handling. systemd sends SIGTERM on stop / at shutdown; Ctrl-C
# sends SIGINT. Either just flips the flag so the loop exits and the finally
# block closes the file properly.
# ---------------------------------------------------------------------------
RUNNING = True


def _stop(signum, frame):
    global RUNNING
    RUNNING = False


signal.signal(signal.SIGTERM, _stop)
signal.signal(signal.SIGINT, _stop)


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # --- configure hardware -------------------------------------------------
    try:
        mux_init()
        init(LOCKWR, TIA_SETTING, REFCN_BIAS_0V5, MODECN_OP_MODE_3LEADAMPC)
    except OSError as e:
        # Almost always "device not connected / I2C error".
        print("Hardware init failed (check LMP91000 @0x48 and MCP23017 "
              "@0x{:02x} on I2C): {}".format(MCP_ADDR, e), file=sys.stderr)
        return 1

    led = ActivityLED()

    # --- open output files, write headers ----------------------------------
    # Two comma-separated files per run:
    #   main   -> timestamp, elapsed, and per device the SMU-equivalent current
    #             (V_REPORT / corrected R_device); relates to prior 0.5 V data.
    #   detail -> timestamp, elapsed, and per device raw, actual loop current, R_ohm.
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    fname_main = os.path.join(OUTPUT_DIR, "chrono_{}.csv".format(stamp))
    fname_detail = os.path.join(OUTPUT_DIR, "chrono_{}_detail.csv".format(stamp))

    header_main = ["iso_timestamp", "elapsed_s"]
    header_detail = ["iso_timestamp", "elapsed_s"]
    for d in range(N_DEVICES):
        header_main.append("dev{:02d}_current_uA".format(d))
        header_detail += ["dev{:02d}_raw".format(d), "dev{:02d}_actual_uA".format(d),
                          "dev{:02d}_R_ohm".format(d)]

    f_main = open(fname_main, "w", newline="")
    f_detail = open(fname_detail, "w", newline="")
    w_main = csv.writer(f_main)
    w_detail = csv.writer(f_detail)
    w_main.writerow(header_main)
    w_detail.writerow(header_detail)
    for fh in (f_main, f_detail):
        fh.flush()
        os.fsync(fh.fileno())
    print("Logging to {}\n     and {}".format(fname_main, fname_detail))

    t0 = time.monotonic()
    led_state = False
    reported_reads = False
    scan_count = 0

    try:
        while RUNNING:
            cycle_start = time.monotonic()
            ts = datetime.now().isoformat(timespec="milliseconds")
            elapsed = time.monotonic() - t0

            row_main = [ts, "{:.3f}".format(elapsed)]
            row_detail = [ts, "{:.3f}".format(elapsed)]
            raws = []
            currents = []
            ohms = []
            for dev in range(N_DEVICES):
                mux_select(dev)
                if SETTLE_S > 0:
                    time.sleep(SETTLE_S)
                # Read back-to-back for AVG_WINDOW_S and average all reads taken.
                acc = 0
                n = 0
                w0 = time.monotonic()
                while time.monotonic() - w0 < AVG_WINDOW_S:
                    acc += readadc()
                    n += 1
                raw_mean = acc / n if n else 0
                _volts, current_uA = raw_to_current(raw_mean)
                r_ohm = device_ohms(current_uA, dev)
                i_corr = None if (r_ohm is None or r_ohm <= 0) else V_REPORT / r_ohm * 1e6
                raws.append(round(raw_mean))
                currents.append(current_uA)
                ohms.append(r_ohm)
                # main: SMU-equivalent current computed from the corrected resistance
                row_main.append("" if i_corr is None else "{:.4f}".format(i_corr))
                # detail: raw, actual loop current, corrected device resistance
                row_detail.append(round(raw_mean))
                row_detail.append("{:.4f}".format(current_uA))
                row_detail.append("" if r_ohm is None else "{:.1f}".format(r_ohm))
                if dev == 0:
                    reads_dev0 = n

            # One-time note of how many reads actually fit per device on this board.
            if not reported_reads:
                print("~{} reads/device averaged per {:.0f} ms window"
                      .format(reads_dev0, AVG_WINDOW_S * 1000))
                reported_reads = True

            w_main.writerow(row_main)
            w_detail.writerow(row_detail)
            # Push both rows to physical storage so an unexpected power loss
            # costs at most the current second, never the whole run.
            for fh in (f_main, f_detail):
                fh.flush()
                os.fsync(fh.fileno())

            # Aligned terminal printout for easy reading (does not affect the CSV).
            # Each device shows  raw|R_ohm (mux-corrected); header every TERM_HEADER_EVERY scans.
            if PRINT_TO_TERMINAL:
                if scan_count % TERM_HEADER_EVERY == 0:
                    labels = "  ".join("{:^12}".format("dev{:02d}".format(d))
                                       for d in range(N_DEVICES))
                    units = "  ".join("{:^12}".format("raw|ohm")
                                      for _ in range(N_DEVICES))
                    print("{:>7}  {}".format("elapsed", labels))
                    print("{:>7}  {}".format("(s)", units))
                cells = "  ".join(
                    "{:>6.0f}|{:>5}".format(
                        raws[d],
                        "open" if ohms[d] is None else "{:.0f}".format(ohms[d]))
                    for d in range(N_DEVICES))
                print("{:>7.1f}  {}".format(elapsed, cells))
            scan_count += 1

            led_state = not led_state
            led.set(led_state)

            # Sleep the remainder of the interval. The recorded elapsed_s /
            # timestamp reflect the ACTUAL scan time, so minor drift is harmless.
            dt = time.monotonic() - cycle_start
            if dt < SCAN_INTERVAL:
                time.sleep(SCAN_INTERVAL - dt)
    finally:
        try:
            for fh in (f_main, f_detail):
                fh.flush()
                os.fsync(fh.fileno())
                fh.close()
        except Exception:
            pass
        # Park the front end in deep sleep and hand the LED back.
        try:
            init(LOCKRO, TIACN_TIAG_35_0_RLOAD_010,
                 REFCN_BIAS_0V5, MODECN_OP_MODE_DEEPSLEEP)
        except Exception:
            pass
        led.set(False)
        led.restore()
        print("Stopped cleanly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
