# AIRBORNE-Pico

A headless, multi-device chronoamperometry logger for the **Raspberry Pi Pico**
(RP2040, MicroPython). It holds twelve two-terminal sensor devices (carbon-nanotube
films across interdigitated electrodes) at a constant potential and logs their
current over time to CSV on a microSD card — no display, no user interaction. It
starts on boot and runs unattended on battery.

This is a **MicroPython port of the AIRBORNE Raspberry Pi Zero W logger**, itself a
stripped-down derivative of the [CVGIT](https://github.com/GITUPCT/CVGIT) project
(see *Attribution* below). The measurement math and calibration are carried over
from the Zero W version unchanged; what changed is the platform underneath it.

> **Note on the name:** this runs on the **Raspberry Pi Pico** microcontroller. It
> is unrelated to the *PalmSens EmStat Pico*, which is a different product (a
> stand-alone potentiostat module programmed in MethodSCRIPT, not MicroPython).

---

## What it does

- Applies a constant **+0.5 V** bias to all 12 devices continuously (2-wire).
- Uses a single **LMP91000** analog front end; a **CD74HC4067** 16-channel analog
  multiplexer (driven by an **MCP23017** I²C GPIO expander) selects which device
  the transimpedance amplifier reads.
- Scans all 12 devices **once per second**. For each device it takes as many ADC
  reads as fit in a ~75 ms window and logs their mean (averaging out mains-frequency
  pickup as well as fast ADC noise).
- Writes one CSV row per scan to **two files on a microSD card**, flushing each row
  to the card immediately so an unexpected power loss costs at most the row in
  progress, never the whole run.
- Blinks the Pico's **onboard LED** as a status heartbeat (slow = running,
  fast = fault).
- **Auto-starts on boot** (via `main.py`) and stops cleanly on `Ctrl-C` in the REPL.

No GUI, plotting, `numpy`, or `matplotlib`.

---

## Hardware

- **Raspberry Pi Pico** (RP2040) running MicroPython
- **LMP91000EVM** potentiostat front end (I²C `0x48`) with onboard **ADC161S626** (SPI)
- **MCP23017** I²C GPIO expander (I²C `0x27`)
- **CD74HC4067** 16-channel analog multiplexer
- 12 two-terminal sensor devices on mux channels 0–11
- **microSD card module** (SPI) + a FAT32-formatted microSD card
- **5 V USB powerbank**, connected to the Pico's micro-USB port

**Power at a glance:** a standard 5 V USB powerbank plugs into the Pico's micro-USB.
That puts 5 V on the Pico's **VBUS** pin, which feeds the ADC's analog supply and the
microSD module (both need 5 V); the LMP91000, MCP23017, and mux run at 3.3 V from the
Pico's `3V3` pin. **No boost converter is needed.** Full wiring is in [`SETUP.md`](SETUP.md).

---

## Files

| File | Purpose |
|---|---|
| `var.py` | I²C/SPI setup and all hardware constants (addresses, bias, gain, series-R, pin map). |
| `settings.py` | Low-level helpers: LMP register writes, ADC read, mux select, current/resistance conversion. |
| `logger.py` | The main loop — the logger. |
| `main.py` | Boot launcher — runs the logger automatically on power-up. |
| `selftest.py` | Wiring check — run by hand before deploying (does not need the SD card). |
| `sdcard.py` | Standard MicroPython SD-card driver — **added separately**, see `SETUP.md`. |

---

## Quick start

1. Flash MicroPython onto the Pico (hold BOOTSEL, plug in USB, drag the `.uf2` on).
2. Install the SD driver: `mpremote mip install sdcard` (or via Thonny's package manager).
3. Copy `var.py`, `settings.py`, `logger.py`, `main.py`, `selftest.py` onto the Pico.
4. Wire per the table in `SETUP.md` — note the SD module's **VCC goes to 5 V, not 3.3 V**.
5. Test the wiring: in the REPL, run `import selftest` (expect I²C `0x48` and `0x27`,
   and sensible raw values).
6. It now runs on its own: on every power-up, `main.py` launches the logger and
   writes to `/sd/Results/`.

See [`SETUP.md`](SETUP.md) for the full walk-through, including viewing and exporting data.

---

## Configuration

Key tunable constants:

- **`var.py`** — `TIA_SETTING` / `R_TIA` (transimpedance gain, 2.75 kΩ),
  `REFCN_BIAS_0V5` (applied potential, 0.5 V), `MCP_ADDR`, `N_DEVICES`,
  `V_EFF` / `R_SERIES` (mux series-resistance correction), and the GP pin map.
- **`logger.py`** — `SCAN_INTERVAL_MS` (1000), `AVG_WINDOW_MS` (75 ms averaging
  window), `SETTLE_MS` (post-switch settle), `LED_PIN` (25).

The `raw_to_current()` conversion is identical to the Zero W version and remains
accurate: with the internal zero at 20% of VREF, zero current sits at raw ≈ −26214
(VOUT resting at 0.5 V), and the scale is ≈ 36 counts/µA. Raw ADC is logged alongside
current so you can always recompute. See `SETUP.md` for the tuning notes.

---

## Output

Two comma-separated files per run, written to `/sd/Results/`, one row per scan. Runs
are **numbered** (`chrono_0000`, `chrono_0001`, …) so a reboot never overwrites an
earlier run — important because a bare Pico has no real-time clock to date the files.

**Main** — `chrono_NNNN.csv` (the simple one):

```
iso_timestamp, elapsed_s, dev00_current_uA, ... dev11_current_uA
```

`devNN_current_uA` is the **SMU-equivalent current**, `V_REPORT / R_device`
(V_REPORT = 0.5 V) computed from the mux-corrected resistance — the number that
relates to prior 0.5 V measurements. Blank when a channel reads open.

**Detail** — `chrono_NNNN_detail.csv`:

```
iso_timestamp, elapsed_s, dev00_raw, dev00_actual_uA, dev00_R_ohm, ... dev11_R_ohm
```

`devNN_raw` is the mean ADC count, `devNN_actual_uA` the actual (mux-taxed) loop
current, and `devNN_R_ohm` the series-corrected device resistance, blank when open.
Each device value is the mean of all ADC reads taken during its ~75 ms window.

- `elapsed_s` comes from a monotonic tick counter and is **always exact**, and all 12
  channels share it, so relative timing and cross-channel alignment are reliable.
- `iso_timestamp` is boot-relative wall-clock unless you add a real-time clock —
  see the CLOCK note in `SETUP.md`.

---

## Attribution and license

This project is a derivative work of **CVGIT** by Juan Aznar-Poveda, José Antonio
López-Pastor, José Francisco Beltrán-Sánchez, and Antonio-Javier García-Sánchez
(Technical University of Cartagena, Group of Telematic Engineering), described in:

> J. Aznar-Poveda, J. A. López-Pastor, A.-J. García-Sánchez, J. García-Haro, and
> T. Fernández-Otero, "A COTS-Based Portable System to Conduct Accurate Substance
> Concentration Measurements," *Sensors* 2018, 18(2), 539.
> https://doi.org/10.3390/s18020539

Original CVGIT repository: https://github.com/GITUPCT/CVGIT
AIRBORNE (Raspberry Pi Zero W) repository: https://github.com/zac84-pittedu/AIRBORNE

CVGIT is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. As
a derivative, this port is also released under **AGPL-3.0**; see the `LICENSE` file.

### Changes from the Zero W (AIRBORNE) version

- Ported from CPython 3 to **MicroPython** for the RP2040.
- `smbus`/`spidev` → `machine.I2C` / `machine.SPI`; manual chip-select for the ADC.
- systemd auto-start → **`main.py`** (MicroPython runs it on boot).
- Onboard-LED heartbeat via `machine.Pin(25)` instead of the Linux `/sys/class/leds` path.
- Clean shutdown via `try/except KeyboardInterrupt` instead of the `signal` module.
- CSV written by hand (no `csv` module); timing via `time.ticks_ms` (no `monotonic`).
- Data logged to a **microSD module on SPI1** instead of the Pi's boot SD card.
- Runs **numbered** instead of timestamp-named, since the Pico has no real-time clock.
- Power split: 3.3 V (Pico `3V3` pin) for the LMP/MCP/mux, 5 V (from the Pico's VBUS,
  i.e. the micro-USB powerbank) for the ADC and SD module — no boost converter.
