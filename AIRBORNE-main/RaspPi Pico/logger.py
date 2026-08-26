#!/usr/bin/env micropython
# logger.py  --  headless 12-device chronoamperometry logger (Raspberry Pi Pico)
#
# MicroPython port of the AIRBORNE logger.py (was Raspberry Pi Zero W / Python 3).
# Holds all 12 devices at +0.5 V and logs current once per second to two CSV files
# on an SD card. No GUI. The onboard LED blinks as a "running" heartbeat. Launched
# automatically on boot by main.py.
#
# What changed from the Zero W version, and why:
#   smbus / spidev            -> machine.I2C / machine.SPI    (in var.py / settings.py)
#   systemd auto-start        -> main.py (MicroPython runs it on boot)
#   /sys/class/leds LED        -> machine.Pin(25) onboard LED
#   signal (SIGTERM/SIGINT)   -> try/except KeyboardInterrupt (Ctrl-C in the REPL)
#   csv module                -> rows written by hand (",".join)
#   datetime                  -> time.localtime()  (see CLOCK note)
#   time.monotonic            -> time.ticks_ms + accumulated elapsed (wrap-safe)
#   os.fsync per row          -> f.flush() per row (MicroPython FAT flush = f_sync)
#   data to the Pi's own SD    -> data to an SD-card module on SPI1 (see var.py)

import os
import time
from machine import Pin, SPI

import var
from var import (LOCKWR, LOCKRO, TIA_SETTING, REFCN_BIAS_0V5,
                 MODECN_OP_MODE_3LEADAMPC, MODECN_OP_MODE_DEEPSLEEP,
                 TIACN_TIAG_35_0_RLOAD_010, N_DEVICES, V_REPORT,
                 SD_SCK, SD_MOSI, SD_MISO, SD_CS)
from settings import (init, readadc, raw_to_current, device_ohms,
                      mux_init, mux_select)

# ---------------------------------------------------------------------------
# Tunable run parameters (milliseconds; the Zero W version used seconds)
# ---------------------------------------------------------------------------
SCAN_INTERVAL_MS = 1000    # start-to-start spacing of each 12-device scan
SETTLE_MS        = 3       # pause after switching mux channel before reading
AVG_WINDOW_MS    = 75      # per device: read back-to-back this long, then average
#   (SETTLE_MS + AVG_WINDOW_MS) x 12 ~= 936 ms, leaving margin in the 1 s interval.
#   The window spans several 50/60 Hz mains cycles (rejects hum); the read COUNT
#   inside it beats down random noise ~sqrt(N). Startup prints reads/device.

OUTPUT_DIR = "/sd/Results"
SD_MOUNT   = "/sd"

PRINT_TO_TERMINAL = True    # aligned table over USB serial; harmless if untethered
TERM_HEADER_EVERY = 20

LED_PIN = 25                # plain Pico onboard LED (a Pico W would use Pin("LED"))

# ---------------------------------------------------------------------------
# CLOCK: a bare Pico has no real-time clock, so time.localtime() counts from a
# fixed epoch at power-on -- iso_timestamp is boot-relative, not true wall time,
# until you set machine.RTC() or add a DS3231. elapsed_s is ALWAYS exact. Run
# files are numbered, not time-stamped, so a reboot never overwrites a prior run
# even without a real clock.
# ---------------------------------------------------------------------------

RUNNING = True


def iso_now():
    t = time.localtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}".format(
        t[0], t[1], t[2], t[3], t[4], t[5])


class LED:
    def __init__(self, pin):
        try:
            self.p = Pin(pin, Pin.OUT, value=0)
        except Exception:
            self.p = None

    def set(self, on):
        if self.p:
            try:
                self.p.value(1 if on else 0)
            except Exception:
                pass

    def error_blink(self):
        # Fast blink forever = a startup fault (no SD card, bad wiring, etc.).
        while True:
            self.set(True);  time.sleep_ms(100)
            self.set(False); time.sleep_ms(100)


def mount_sd():
    # Needs the standard MicroPython "sdcard.py" driver on the board.
    import sdcard
    spi_sd = SPI(1, baudrate=1000000,
                 sck=Pin(SD_SCK), mosi=Pin(SD_MOSI), miso=Pin(SD_MISO))
    cs_sd = Pin(SD_CS, Pin.OUT)
    sd = sdcard.SDCard(spi_sd, cs_sd)
    os.mount(sd, SD_MOUNT)


def ensure_dir(path):
    try:
        os.mkdir(path)
    except OSError:
        pass  # already exists


def next_run_index(results_dir):
    # New, unused run number so reboots never overwrite an earlier run.
    try:
        files = os.listdir(results_dir)
    except OSError:
        return 0
    mx = -1
    for fn in files:
        if fn.startswith("chrono_") and fn.endswith(".csv") and "_detail" not in fn:
            try:
                mx = max(mx, int(fn[7:-4]))
            except ValueError:
                pass
    return mx + 1


# Per-row durability. On MicroPython's FAT filesystem, f.flush() maps to FatFs
# f_sync(), which writes the file's data, directory entry and FAT to the card --
# so a power cut costs at most the row in progress. os.sync() (if present) adds a
# belt-and-suspenders block-device flush.
_HAS_SYNC = hasattr(os, "sync")


def durable(files):
    for f in files:
        f.flush()
    if _HAS_SYNC:
        os.sync()


def main():
    led = LED(LED_PIN)

    # --- mount the SD card -------------------------------------------------
    try:
        mount_sd()
    except Exception as e:
        print("SD mount failed:", e)
        print("Check: card inserted; VCC on 5 V (NOT 3.3 V); wiring to SPI1 "
              "(SCK GP{}, MOSI GP{}, MISO GP{}, CS GP{}); sdcard.py on the board."
              .format(SD_SCK, SD_MOSI, SD_MISO, SD_CS))
        led.error_blink()          # never returns

    ensure_dir(OUTPUT_DIR)

    # --- configure the analog front end ------------------------------------
    try:
        mux_init()
        init(LOCKWR, TIA_SETTING, REFCN_BIAS_0V5, MODECN_OP_MODE_3LEADAMPC)
    except OSError as e:
        print("Front-end init failed (LMP @0x48 / MCP @0x{:02x} on I2C):"
              .format(var.MCP_ADDR), e)
        led.error_blink()          # never returns

    # --- open the two output files -----------------------------------------
    idx = next_run_index(OUTPUT_DIR)
    fname_main = "{}/chrono_{:04d}.csv".format(OUTPUT_DIR, idx)
    fname_detail = "{}/chrono_{:04d}_detail.csv".format(OUTPUT_DIR, idx)

    hdr_main = ["iso_timestamp", "elapsed_s"]
    hdr_detail = ["iso_timestamp", "elapsed_s"]
    for d in range(N_DEVICES):
        hdr_main.append("dev{:02d}_current_uA".format(d))
        hdr_detail += ["dev{:02d}_raw".format(d),
                       "dev{:02d}_actual_uA".format(d),
                       "dev{:02d}_R_ohm".format(d)]

    f_main = open(fname_main, "w")
    f_detail = open(fname_detail, "w")
    f_main.write(",".join(hdr_main) + "\n")
    f_detail.write(",".join(hdr_detail) + "\n")
    durable((f_main, f_detail))
    print("Logging to", fname_main)
    print("       and", fname_detail)

    # --- main loop ---------------------------------------------------------
    last = time.ticks_ms()
    elapsed_ms = 0
    reported = False
    scan = 0

    try:
        while RUNNING:
            cycle_start = time.ticks_ms()
            # wrap-safe accumulated elapsed (per-scan deltas are ~1 s, always valid)
            elapsed_ms += time.ticks_diff(cycle_start, last)
            last = cycle_start
            elapsed_s = elapsed_ms / 1000.0
            ts = iso_now()

            row_main = [ts, "{:.3f}".format(elapsed_s)]
            row_detail = [ts, "{:.3f}".format(elapsed_s)]
            raws = []
            ohms = []
            reads_dev0 = 0

            for dev in range(N_DEVICES):
                mux_select(dev)
                if SETTLE_MS > 0:
                    time.sleep_ms(SETTLE_MS)
                # Read back-to-back for AVG_WINDOW_MS and average all reads taken.
                acc = 0
                n = 0
                w0 = time.ticks_ms()
                while time.ticks_diff(time.ticks_ms(), w0) < AVG_WINDOW_MS:
                    acc += readadc()
                    n += 1
                raw_mean = acc / n if n else 0
                _v, current_uA = raw_to_current(raw_mean)
                r_ohm = device_ohms(current_uA, dev)
                i_corr = None if (r_ohm is None or r_ohm <= 0) else V_REPORT / r_ohm * 1e6

                row_main.append("" if i_corr is None else "{:.4f}".format(i_corr))
                row_detail.append(str(round(raw_mean)))
                row_detail.append("{:.4f}".format(current_uA))
                row_detail.append("" if r_ohm is None else "{:.1f}".format(r_ohm))
                raws.append(round(raw_mean))
                ohms.append(r_ohm)
                if dev == 0:
                    reads_dev0 = n

            if not reported:
                print("~{} reads/device averaged per {} ms window"
                      .format(reads_dev0, AVG_WINDOW_MS))
                reported = True

            f_main.write(",".join(row_main) + "\n")
            f_detail.write(",".join(row_detail) + "\n")
            durable((f_main, f_detail))

            if PRINT_TO_TERMINAL:
                if scan % TERM_HEADER_EVERY == 0:
                    print("{:>7}  {}".format("elapsed", "  ".join(
                        "{:^12}".format("dev{:02d}".format(d)) for d in range(N_DEVICES))))
                    print("{:>7}  {}".format("(s)", "  ".join(
                        "{:^12}".format("raw|ohm") for _ in range(N_DEVICES))))
                cells = "  ".join(
                    "{:>6d}|{:>5}".format(
                        raws[d], "open" if ohms[d] is None else "{:.0f}".format(ohms[d]))
                    for d in range(N_DEVICES))
                print("{:>7.1f}  {}".format(elapsed_s, cells))
            scan += 1

            led.set(scan % 2)

            dt = time.ticks_diff(time.ticks_ms(), cycle_start)
            if dt < SCAN_INTERVAL_MS:
                time.sleep_ms(SCAN_INTERVAL_MS - dt)

    except KeyboardInterrupt:
        pass
    finally:
        try:
            for f in (f_main, f_detail):
                f.flush()
                f.close()
            if _HAS_SYNC:
                os.sync()
        except Exception:
            pass
        # Park the front end in deep sleep.
        try:
            init(LOCKRO, TIACN_TIAG_35_0_RLOAD_010, REFCN_BIAS_0V5,
                 MODECN_OP_MODE_DEEPSLEEP)
        except Exception:
            pass
        led.set(False)
        try:
            os.umount(SD_MOUNT)
        except Exception:
            pass
        print("Stopped cleanly.")


if __name__ == "__main__":
    main()
