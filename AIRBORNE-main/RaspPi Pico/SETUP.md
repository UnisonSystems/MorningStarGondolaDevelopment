# Setup — Headless 12-device chronoamperometry logger (Raspberry Pi Pico)

Holds 12 CNT-on-interdigitated-electrode devices at a constant +0.5 V and logs their
current once per second to CSV files on a microSD card. No screen, no GUI: it starts
on boot, blinks the onboard LED to show it is running, and writes crash-safely.

> **Which "Pico":** this is the **Raspberry Pi Pico** (RP2040 microcontroller running
> MicroPython), *not* the PalmSens EmStat Pico. If you were handed an EmStat Pico,
> none of this applies — that is a different instrument programmed in MethodSCRIPT.

This guide covers everything end to end: what you need, flashing MicroPython, getting
the scripts onto the board, wiring, a wiring self-test, running it, and viewing and
exporting the data.

---

## 1. What you need

**Hardware**
- Raspberry Pi Pico (standard RP2040 board)
- LMP91000EVM potentiostat front end (with its onboard ADC161S626)
- MCP23017 I²C GPIO expander
- CD74HC4067 16-channel analog multiplexer
- microSD card module (the common 5 V type with onboard regulator + level shifter)
- microSD card, formatted **FAT32** (≤ 32 GB works cleanly with the standard driver)
- **5 V USB powerbank** with a micro-USB cable to power the Pico (any standard USB
  powerbank — its output is 5 V). No boost converter is needed.
- Female–female jumper wires

**Software (on your computer)**
- [Thonny](https://thonny.org) — the easiest way to flash, copy files, and see output.
  (Command-line alternative: `mpremote`, installed with `pip install mpremote`.)
- The MicroPython firmware `.uf2` for the Raspberry Pi Pico, from
  https://micropython.org/download/RPI_PICO/

---

## 2. Wiring

The RP2040 has two I²C and two SPI peripherals. The ADC lives on **SPI0** and the SD
card on **SPI1**, so the two never contend. Pin numbers below match the `var.py` pin
map — change them in one place if you rewire.

### Digital connections to the Pico

| Signal | Pico GP (physical pin) | Connects to |
|---|---|---|
| I²C SDA | GP8 (pin 11) | LMP91000 SDA **and** MCP23017 SDA (shared bus) |
| I²C SCL | GP9 (pin 12) | LMP91000 SCL **and** MCP23017 SCL (shared bus) |
| ADC SCLK | GP18 (pin 24) | LMP91000EVM SCLK |
| ADC MOSI | GP19 (pin 25) | LMP91000EVM MOSI |
| ADC MISO | GP16 (pin 21) | LMP91000EVM MISO |
| ADC CS | GP17 (pin 22) | LMP91000EVM CS (was CE0 on the Pi) |
| SD SCLK | GP10 (pin 14) | SD module SCK |
| SD MOSI | GP11 (pin 15) | SD module MOSI |
| SD MISO | GP12 (pin 16) | SD module MISO |
| SD CS | GP13 (pin 17) | SD module CS |

### Power

The powerbank plugs into the Pico's **micro-USB** port. That puts the powerbank's 5 V
onto the Pico's **VBUS** pin, and the Pico regulates its own 3.3 V on the `3V3(OUT)` pin
— so both rails come from the one USB connection, with no boost converter.

- **Powerbank** → Pico **micro-USB** port.
- **Pico `VBUS`** (pin 40) → LMP91000EVM **5 V** pin (the ADC's analog supply)
  **and** the microSD module **VCC**.
- **Pico `3V3(OUT)`** (pin 36) → LMP91000 3.3 V, MCP23017 VCC, CD74HC4067 VCC.
- **Common GND** → tie every board's ground together (Pico GND, e.g. pin 38).

> **The one mistake that will silently break it:** the microSD module you have is the
> 5 V type — its onboard regulator needs ~5 V in. Powering its VCC from 3.3 V browns
> out the card and it will not mount. Its VCC must come from **VBUS (5 V)**. The SPI
> data lines are still 3.3 V from the Pico; only VCC is 5 V.

> **Note on VBUS:** it carries 5 V only while the micro-USB is powered (by the
> powerbank in the field, or by your computer while programming) — that's exactly when
> you need it. You won't have the powerbank and a computer on the port at once, and
> both supply the same 5 V, so nothing special is required when switching between them.

### Analog / electrode side (unchanged from the AIRBORNE build)

The mux common line goes to the LMP91000 working-electrode (TIA) input; the 12 device
return leads share the CE/RE bias node with the EVM's 2-WIRE jumper set. MCP Port A
bits 0–3 drive the CD74HC4067 address lines S0–S3, so device N maps to mux channel N.
This is identical to the Zero W hardware — if that was already built, leave it as is.

---

## 3. Flash MicroPython onto the Pico

1. Unplug the Pico. Hold the **BOOTSEL** button and plug it into USB while holding.
2. It appears as a USB drive named **RPI-RP2**.
3. Drag the MicroPython `.uf2` file onto that drive. The Pico reboots automatically,
   now running MicroPython. (The drive disappears — that's expected.)

In Thonny, choose **Run → Configure interpreter → MicroPython (Raspberry Pi Pico)** and
select the Pico's serial port. You should get a `>>>` prompt in the Shell pane.

---

## 4. Get the scripts onto the Pico ("importing to MicroPython")

MicroPython keeps a small filesystem on the Pico's flash; you copy `.py` files onto it
and `import` works exactly as on a PC. There is no compile step.

### 4a. The SD-card driver (`sdcard.py`)

The logger needs the standard MicroPython SD driver. Get it one of these ways:

- **Thonny:** Tools → Manage packages → search `sdcard` → install, **or**
- **Command line:** `mpremote mip install sdcard`

Either places `sdcard.py` on the board. (If neither works, the file is a single,
well-known module in the official MicroPython drivers directory — copy it over manually.)

### 4b. The project scripts

Copy these five files to the **root** of the Pico's filesystem:

`var.py`, `settings.py`, `logger.py`, `main.py`, `selftest.py`

- **In Thonny:** open each file, then **File → Save as… → Raspberry Pi Pico**, keeping
  the same name. (Or use the Files pane: right-click a local file → *Upload to /*.)
- **Command line:** `mpremote cp var.py settings.py logger.py main.py selftest.py :`

When done, the Pico's filesystem should contain:
`var.py  settings.py  logger.py  main.py  selftest.py  sdcard.py`

---

## 5. Prepare the SD card

Format the microSD card as **FAT32** on your computer, then insert it into the module.
The logger creates `/sd/Results/` automatically on first run; you don't need to make it.

---

## 6. Test the wiring before deploying

With the Pico connected to Thonny, at the `>>>` prompt type:

```python
import selftest
```

Expected: it lists the I²C devices (you want **`0x48`** and **`0x27`** to appear), then
reads all 12 mux channels and prints raw ADC / current / resistance for each. If the two
addresses show up and the raw values change sensibly as you change what's on the
channels, the wiring and the whole measurement path are good. (This test does not touch
the SD card.)

If an address is missing, re-check SDA/SCL, power, and grounds. See Troubleshooting below.

---

## 7. Run it

Once the self-test passes, normal operation is automatic:

- **On boot:** when the Pico powers up, `main.py` runs and launches the logger. It mounts
  the SD card, opens a new numbered pair of files in `/sd/Results/`, and begins scanning.
- **Onboard LED:** **slow blink (~0.5 Hz) = running normally.** **Fast blink = a startup
  fault** (no card, bad wiring, or SD VCC on the wrong rail) — your at-a-glance field check.
- **To run it manually / watch it live** while tethered: in the Thonny Shell, press
  `Ctrl-C` to interrupt whatever is running, then type `import logger; logger.main()` —
  or just reset the board. The live per-second table prints to the Shell over USB.
- **To stop cleanly:** press `Ctrl-C` in the Shell. It closes the files, parks the front
  end in deep sleep, and unmounts the card.

For unattended battery deployment, just power it from the pack — no computer needed. It
logs from the moment it boots.

---

## 8. Viewing data

**Live, while connected by USB:** open the Thonny Shell (or any serial terminal on the
Pico's port). Each second the logger prints an aligned table of every channel's raw value
and corrected resistance, with a header reprinted periodically. This is a display only —
it does not change what's written to the card.

**The stored CSV files** live on the card at `/sd/Results/chrono_NNNN.csv` (main) and
`chrono_NNNN_detail.csv` (detail), a fresh numbered pair per run.

The simplest way to read them is to power down, pop the microSD out, and open it on a
computer (it's plain FAT32). You can also read them over USB — see Exporting below.

---

## 9. Exporting data

**Easiest — pull the card:** power off the Pico, remove the microSD, put it in a
computer's card reader, and copy the `chrono_*.csv` files. They open directly in Excel,
LibreOffice, or `pandas.read_csv(...)`.

**Over USB (without removing the card):** because the logger owns the card while running,
first stop it with `Ctrl-C` in the Shell, then mount the card in the REPL and copy files
off. At the `>>>` prompt:

```python
import os, sdcard
from machine import Pin, SPI
import var
spi = SPI(1, baudrate=1_000_000, sck=Pin(var.SD_SCK), mosi=Pin(var.SD_MOSI), miso=Pin(var.SD_MISO))
os.mount(sdcard.SDCard(spi, Pin(var.SD_CS)), "/sd")
print(os.listdir("/sd/Results"))
```

Then, from a terminal on your computer:

```
mpremote fs cp :/sd/Results/chrono_0000.csv .
mpremote fs cp :/sd/Results/chrono_0000_detail.csv .
```

(Thonny's Files pane can also browse `/sd/Results` and download files once the card is
mounted as above.)

**Which file to use:** the **main** file (`chrono_NNNN.csv`) has the SMU-equivalent
current per device (`V_REPORT / R_device`), the number that lines up with prior 0.5 V
measurements. The **detail** file adds raw ADC counts, the actual loop current, and the
series-corrected resistance, so you can re-derive everything or debug a channel. Blank
cells mean that channel read open.

---

## 10. Clock / timestamps (important caveat)

A bare Pico has **no real-time clock**, so `time.localtime()` starts from a fixed date at
each power-up. That means:

- **`elapsed_s` is always exact** (from a monotonic counter) and is shared by all 12
  channels, so relative timing within a run and cross-channel alignment are trustworthy.
- **`iso_timestamp` is boot-relative**, not true wall-clock time, until something sets the
  clock. Runs are numbered rather than time-named so this never causes file collisions.

If you need real absolute timestamps offline, add a small **DS3231 RTC module** (battery-
backed, a few dollars) and set the clock at boot. The logger is written so this is an easy
addition — ask if you want it wired into `main.py`.

---

## 11. Tuning notes

- **Averaging window** (`AVG_WINDOW_MS`, default 75 ms in `logger.py`): each device is
  settled (`SETTLE_MS`, 3 ms) then read back-to-back for the window and all reads averaged.
  `(SETTLE + WINDOW) × 12 ≈ 0.94 s`, inside the 1 s scan. The window spans several
  50/60 Hz mains cycles to reject hum; the read count within it beats down random noise by
  ~√N. Startup prints how many reads/device actually fit on this board. Since devices are
  biased continuously, `SETTLE_MS` can likely drop toward 0.
- **TIA gain** (`TIA_SETTING` / `R_TIA`, default 2.75 kΩ, the lowest internal gain): chosen
  so VOUT clears the rail at the LMP's ~750 µA drive ceiling. If raw pins near the rail,
  lower the bias (`REFCN_BIAS_0V5`) or fit an external R_TIA below 2.75 kΩ. If raw barely
  moves off the noise floor at the cold end, raise R_TIA (accepting a lower peak). Keep
  `TIA_SETTING` and `R_TIA` consistent.
- **Series resistance** (`V_EFF`, `R_SERIES` in `var.py`): the mux + wiring add ~few-hundred
  ohms in series per channel; `device_ohms()` removes it per channel. Re-run the
  resistor-ladder calibration and update these after any rewire, reflow, or mux swap.
- **Current scaling** (`raw_to_current()`): unchanged and accurate. Zero current is at
  raw ≈ −26214 (VOUT resting at 0.5 V); raw = 0 maps to VOUT = 2.5 V, a real ~727 µA point.
  Raw ADC is logged so you can always recompute. Worth a sanity check against a known
  resistor during bring-up.

---

## 12. Power and battery life

The Pico system draws roughly **0.15–0.2 W** (≈ 35–40 mA equivalent) — the sensor chips
are negligible; the board dominates. From a typical 10,000 mAh powerbank (~30 Wh usable
after the pack's 3.7 V→5 V USB conversion) that's on the order of **a week** of continuous
logging. The loop keeps the Pico busy ~90% of each second doing ADC reads, so there's
little idle headroom; if you ever need longer runtime, shrinking `AVG_WINDOW_MS` and
sleeping between scans is the lever. Measure your actual rig with an inline USB power
meter before committing to a battery size — these are estimates.

---

## 13. Troubleshooting

- **LED blinks fast, nothing logs.** A startup fault. Most often the SD card: check it's
  inserted, formatted FAT32, that its **VCC is on 5 V (not 3.3 V)**, that the SPI lines go
  to GP10–13, and that `sdcard.py` is on the board. The Shell prints the specific reason
  if you connect over USB.
- **Self-test doesn't show `0x48` or `0x27`.** I²C wiring/power issue: check SDA→GP8,
  SCL→GP9, that both chips have 3.3 V and share ground, and that the LMP is getting its
  5 V analog supply.
- **Raw values look stuck or railed.** See the TIA gain / bias notes in §11.
- **Files start over at `chrono_0000` unexpectedly.** Expected if the card was reformatted
  or swapped; numbering is derived from the files present in `/sd/Results/`.
- **Timestamps all show the same date.** Expected without an RTC — see §10; use `elapsed_s`.

---

*Derivative of CVGIT (Aznar-Poveda et al., Technical University of Cartagena) and the
AIRBORNE Raspberry Pi Zero W logger. Released under AGPL-3.0 — see `LICENSE` and the
README for full attribution.*
