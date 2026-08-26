# AIRBORNE

A headless, multi-device chronoamperometry logger for the Raspberry Pi. It holds
twelve two-terminal sensor devices (carbon-nanotube films across interdigitated
electrodes) at a constant potential and logs their current over time to CSV, with
no display and no user interaction — it starts on boot and runs unattended.

It is a substantially rewritten, stripped-down Python 3 port of the
[CVGIT](https://github.com/GITUPCT/CVGIT) project (see *Attribution* below), which
originally performed cyclic voltammetry on a single device through a GUI.

## What it does

- Applies a constant **+0.5 V** bias to all 12 devices continuously (2-wire).
- Uses a single **LMP91000** analog front end; a **CD74HC4067** 16-channel analog
  multiplexer (driven by an **MCP23017** I²C GPIO expander) selects which device
  the transimpedance amplifier reads.
- Scans all 12 devices **once per second**. For each device it takes as many ADC
  reads as fit in a ~75 ms window and logs their mean (spreading the reads over
  ~75 ms averages out mains-frequency pickup as well as fast ADC noise).
- Writes one CSV row per scan, flushing each row to the SD card immediately so an
  unexpected power loss costs at most the row in progress, never the whole run.
- Blinks the Pi's onboard LED as a "running" heartbeat (no GPIO pins used).
- Auto-starts on boot via a systemd unit and shuts down cleanly on `SIGTERM`/`SIGINT`.

No GUI, plotting, `numpy`, or `matplotlib` — only `smbus` and `spidev`.

## Hardware

- Raspberry Pi Zero W (Raspberry Pi OS Trixie / Python 3.13)
- LMP91000EVM potentiostat front end (I²C `0x48`) with onboard ADC161S626 (SPI)
- MCP23017 I²C GPIO expander (I²C `0x27`)
- CD74HC4067 16-channel analog multiplexer
- 12 two-terminal sensor devices on mux channels 0–11

All logic runs at 3.3 V; the LMP's analog supply (VA) is 5 V. On the shared I²C
bus you should see `0x27` (MCP), `0x48` (LMP), and `0x57` (the EVM's onboard
EEPROM, unused). Full wiring and one-time Pi configuration are in
[`SETUP.md`](SETUP.md).

### Connection summary

| Group | From (Pi / MCP) | To |
|---|---|---|
| Power | 3.3 V | MCP VCC, mux VCC, LMP 3.3 V |
| Power | 5 V | LMP VA |
| Ground | GND | common ground (all boards) |
| I²C | SDA (BCM2/pin 3), SCL (BCM3/pin 5) | LMP + MCP (shared bus) |
| SPI | MOSI/MISO/SCLK/CE0 (pins 19/21/23/24) | LMP (direct — cannot go via MCP) |
| Mux address | MCP PA0–PA3 | CD74HC4067 S0–S3 |
| Analog | mux common (SIG) | LMP working electrode (WE) |
| Analog | device return leads | shared CE/RE bias node (2-WIRE jumper set) |

## Files

| File | Purpose |
|---|---|
| `logger.py` | Main loop — run this. |
| `settings.py` | Low-level helpers: LMP register writes, ADC read, mux select, current conversion. |
| `var.py` | I²C/SPI setup and all hardware constants (addresses, bias, gain). |
| `chrono-logger.service` | systemd unit for auto-start on boot. |
| `SETUP.md` | Full wiring, Pi setup, install, and tuning notes. |

## Quick start

```bash
sudo apt install git python3-smbus python3-spidev i2c-tools
git clone https://github.com/zac84-pittedu/AIRBORNE.git ~/Documents/AIRBORNE
sudo raspi-config          # enable I2C and SPI, then reboot
sudo i2cdetect -y 1        # expect 0x27, 0x48, 0x57
cd ~/Documents/AIRBORNE
sudo python3 logger.py     # prints reads/device, LED blinks, writes Results/*.csv
```

See [`SETUP.md`](SETUP.md) for the systemd auto-start setup.

## Configuration

Key tunable constants:

- `var.py` — `R_TIA` / `TIA_SETTING` (transimpedance gain, 2.75 kΩ),
  `REFCN_BIAS_0V5` (applied potential, 0.5 V), `MCP_ADDR`, `N_DEVICES`, `V_EFF` / `R_SERIES` (mux series-R correction).
- `logger.py` — `SCAN_INTERVAL` (1 s), `AVG_WINDOW_S` (75 ms averaging window),
  `SETTLE_S` (post-switch settle).

At the lowest internal gain (2.75 kΩ), if the raw column still pins near the rail the applied bias is too high — lower it in `REFCN_BIAS_0V5`, or fit an external `R_TIA` below 2.75 kΩ. The `raw_to_current()` conversion is accurate: with the internal zero at 20% of VREF, zero current sits at raw ≈ -26214 (VOUT resting at 0.5 V), where the formula returns ≈ 0 µA, and the scale is ≈ 36 counts/µA. raw = 0 maps to VOUT = VREF = 2.5 V, a real ≈ 727 µA point, not the baseline — there is no offset in the readings. Raw ADC is logged alongside current, so you can always recompute. See `SETUP.md` for details.

## Output

Two comma-separated files per run, one row per scan:

**Main** — `Results/chrono_YYYY-MM-DD_HHMMSS.csv` (the simple one):

```
iso_timestamp, elapsed_s, dev00_current_uA, ... dev11_current_uA
```

`devNN_current_uA` here is the **SMU-equivalent current**, `V_REPORT / R_device` (V_REPORT = 0.5 V) computed from the mux-corrected resistance — the number that relates to prior 0.5 V measurements. Blank when a channel reads open.

**Detail** — `Results/chrono_YYYY-MM-DD_HHMMSS_detail.csv`:

```
iso_timestamp, elapsed_s, dev00_raw, dev00_actual_uA, dev00_R_ohm, ... dev11_R_ohm
```

`devNN_raw` is the mean ADC count, `devNN_actual_uA` the actual (mux-taxed) loop current, and `devNN_R_ohm` the series-corrected device resistance (`V_EFF / I - R_SERIES[ch]`), blank when open. Each device value is the **mean of all ADC reads taken during its ~75 ms window**
that second. `elapsed_s` (monotonic clock) is always reliable; `iso_timestamp` is
wall-clock and only as accurate as the Pi's clock (the Zero W has no RTC — see the
clock note in `SETUP.md`).

## Attribution and license

This project is a derivative work of **CVGIT** by Juan Aznar-Poveda, José Antonio
López-Pastor, José Francisco Beltrán-Sánchez, and Antonio-Javier García-Sánchez
(Technical University of Cartagena, Group of Telematic Engineering), described in:

> J. Aznar-Poveda, J. A. López-Pastor, A.-J. García-Sánchez, J. García-Haro, and
> T. Fernández-Otero, "A COTS-Based Portable System to Conduct Accurate Substance
> Concentration Measurements," *Sensors* 2018, 18(2), 539.
> https://doi.org/10.3390/s18020539

Original repository: https://github.com/GITUPCT/CVGIT

CVGIT is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**.
As a derivative, AIRBORNE is also released under **AGPL-3.0**; see the `LICENSE`
file in this repository.

### Summary of changes from the original CVGIT

- Ported from Python 2 to Python 3 (Raspberry Pi OS Trixie / Python 3.13).
- Removed the Tkinter GUI, live matplotlib plotting, and the `numpy` dependency.
- Replaced cyclic voltammetry with constant-potential chronoamperometry at 0.5 V.
- Added support for 12 devices via a CD74HC4067 multiplexer driven by an MCP23017
  over I²C (direct `smbus` register writes; no additional library).
- Added time-bounded per-device averaging, onboard-LED heartbeat, timestamped CSV
  logging with per-row fsync, clean signal handling, and a systemd unit for
  unattended auto-start.

Maintained by **binglepingle**.
