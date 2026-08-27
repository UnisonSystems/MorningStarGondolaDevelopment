# Morning Star Gondola Development

Avionics, mechanical parts, bench tests, and post-flight analysis for the Morning Star high-altitude balloon gondola.

Current flight computer: Raspberry Pi Pico (RP2040) running the Earle Philhower Arduino-Pico core. Radio is a Tiny4FSK + Shield on a dedicated I2C bus. This repository does **not** contain Tiny4FSK firmware.

License: MIT (see `LICENSE`). Copyright 2026 Unison Systems.

## Layout

| Path | What it is |
|---|---|
| [`Flight-Software/`](Flight-Software/) | **Prototype Pico flight software.** Untested. Experiment with this. |
| [`Flight-Software-Heritage/`](Flight-Software-Heritage/) | MIT-Pico.ino flew in 2026. Also includes test scripts for novel sensors.|
| [`Benchtop-Testing/`](Benchtop-Testing/) | Single-sensor and combined Arduino sketches used to qualify hardware.|
| [`Analysis/`](Analysis/) | Python tools for flight logs and bench logs. Run generate_report.py on the cleaned flight log, DATALOG.CSV. Run test_main.ino to generate NNNN_flight_log.bin. Run bench_test_analysis.py with NNNN_flight_log.bin (try 0000_flight_log.bin as an example).|
| [`Hardware/`](Hardware/) | STL files for 3D-printed hardware. |

## Flight software

Source: [`Flight-Software/MIT-Pico/`](Flight-Software/MIT-Pico/).

Wiring and power: [`Flight-Software/pico_wiring.txt`](Flight-Software/pico_wiring.txt), [`Flight-Software/power_wiring.txt`](Flight-Software/power_wiring.txt).

| Item | Detail |
|---|---|
| Board | Raspberry Pi Pico |
| Core | Earle F. Philhower Arduino-Pico |
| Libraries | Adafruit BNO08x, Adafruit MAX31865, Adafruit SHT4x, Rob Tillaart MS5611, `SD` / `Wire` / `SPI` |
| On-board log | Packed binary records on MicroSD (`NNNN_flight_log.bin`) |
| Downlink | Pico is I2C peripheral `0x09` on GP2/GP3. Tiny4FSK is the controller and owns the 20 s packet cadence. |
| Run time | Until power loss or manual deactivation. GP10 MOSFET enable is asserted in `setup()` and held. |

Sensors logged to MicroSD:

- BNO086 at 100 Hz (SD only; not downlinked)
- MAX31865 + GY-63 MS5611 + SHT45 at 20 Hz
- XY-T01 #1 and #2 whenever a UART line parses (~1 Hz)

I2C snapshot to Tiny4FSK (16 bytes, little-endian): MAX temp, SHT45 temp, MS5611 temp, XY-T01 #1 temp, XY-T01 #2 temp, SHT45 RH, MS5611 pressure.

Open the `MIT-Pico` folder as an Arduino sketch (folder name must match `MIT-Pico.ino`). Build and flash with the Philhower Pico board package.

## Heritage firmware

[`Flight-Software-Heritage/`](Flight-Software-Heritage/) keeps the earlier payload software:

- `MIT-Onsite-Test/MIT-Pico` — Pico sketch from the CNT-era flight (LMP91000 array + Tiny4FSK slave)
- `MIT-Onsite-Test/i2c-scan` — I2C bus scanner
- `RaspPi Pico` / `RaspPi ZeroW` — MicroPython loggers from earlier gondola revisions

These are reference only. Pin maps and sensor lists do not match the current airframe.

## Bench tests

[`Benchtop-Testing/`](Benchtop-Testing/) is one sketch per check. Each `test_*` directory is its own Arduino sketch.

| Sketch | Purpose |
|---|---|
| `test_BNO086` | BNO086 on I2C0 |
| `test_GY63MS5611` | MS5611 |
| `test_MAX31865` | MAX31865 RTD |
| `test_SHT45` | SHT45 |
| `test_XYT01` | XY-T01 UART + heater commands |
| `test_MicroSD` | ADA254 card |
| `test_*_x_MicroSD` | That sensor plus SD logging |
| `test_main` | Combined logger used as the behavioral spec for current flight software |

## Analysis

- [`Analysis/Flight/`](Analysis/Flight/) — flight-log report generation (`generate-report.py`)
- [`Analysis/Testing/`](Analysis/Testing/) — bench-log helper (`bench_test_analysis.py`)

Use a virtualenv and the `requirements.txt` next to the flight script.

## Hardware

[`Hardware/`](Hardware/) is printable flight structure: Pico / sensor receivers, Tiny4FSK receivers, battery box, GoPro bay, switch housing, CNT cage (heritage), M3 washers.

## What this repo is not

- Not Tiny4FSK / Horus encoder source
- Not GoPro firmware
- Not a place to commit MicroSD flight binaries or USB-serial captures (see `.gitignore`)
