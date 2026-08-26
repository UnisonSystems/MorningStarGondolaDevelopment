# Headless 12-device chronoamperometry logger (Raspberry Pi Zero W, Pi OS Trixie)

Holds 12 CNT-on-interdigitated-electrode devices at a constant +0.5 V and logs
their current once per second to a CSV file. No screen, no GUI: it starts on
boot, blinks the onboard LED to show it is running, and writes crash-safely.

## Files

- `var.py`      - I2C/SPI setup + all register/hardware constants (bias, gain, addresses)
- `settings.py` - low-level helpers (LMP register writes, ADC read, mux select, conversion)
- `logger.py`   - the main loop (run this)
- `chrono-logger.service` - systemd unit for auto-start on boot

Put all files in one folder. The service expects `/home/airborne/Documents/AIRBORNE`;
edit the unit's two paths if you use a different location. See Install below.

## Hardware map (as wired)

- LMP91000EVM: I2C @ 0x48, SPI on SPI0/CE0. 2-WIRE jumper set = CE-RE shorted (2-electrode
               wiring). LMP register op-mode stays 3-lead amperometric: the potentiostat amp
               A1 still drives the shorted CE/RE node to hold the bias. 
- MCP23017:    I2C @ 0x27 (A2/A1/A0 all high). VCC = 3.3 V. Port A bits 0-3 -> mux S0-S3.
- CD74HC4067:  VCC = 3.3 V, EN tied to GND (always enabled). Common line -> LMP WE/TIA input.
               Channels 0-11 -> the 12 devices. All devices share the always-on 0.5 V bias.
- Bias/gain:   +0.5 V (REFCN 10011011, internal zero 20%), R_TIA = 2.75 kOhm. Both tunable in var.py.

## Connection map
Only connections that should need to made are to the Pi.  As shipped on 7/6/25 labels include final location

Color guide:

  Red      --> Pin 1  (3.3 V)
  
  Orange   --> Pin 2  (5 V)
  
  Green    --> Pin 3  (SDA)
  
  L.Blue   --> Pin 5  (SCL)
  
  Black    --> Pin 6  (GND)
  
  L.Green  --> Pin 19 (MOSI)
  
  D.Blue   --> Pin 21 (MISO)
  
  Purple   --> Pin 23 (SCLK)
  
  White    --> Pin 24 (CE0)

  Red/Black banana plugs go to WE and RE (tied to CE), respectively on LMP

## Install (get the code)

Clone the repository to the path the service file expects,
`/home/airborne/Documents/AIRBORNE` (clone as the `airborne` user so `~` resolves
there; if you put it elsewhere, update the two paths in `chrono-logger.service`):

    sudo apt install git
    mkdir -p ~/Documents
    cd ~/Documents
    git clone https://github.com/zac84-pittedu/AIRBORNE.git
    cd AIRBORNE

You now have `logger.py`, `settings.py`, `var.py`, `chrono-logger.service`, and
the docs; `Results/` is created automatically on the first run. To update later:

    cd ~/Documents/AIRBORNE && git pull

## One-time Pi setup

    sudo apt update
    sudo apt install python3-smbus python3-spidev i2c-tools

    sudo raspi-config     # Interface Options -> enable I2C AND SPI, then reboot
    sudo reboot

Confirm both chips are on the bus (with the LMP connected):

    sudo i2cdetect -y 1   # expect 0x27 (MCP) and 0x48 (LMP)

Note: numpy / matplotlib / tkinter are NOT needed by this headless version.

## Test run (before enabling the service)

    cd ~/Documents/AIRBORNE
    sudo python3 logger.py

It prints the output filename and starts scanning; the onboard LED blinks ~1 Hz.
Ctrl-C stops it cleanly (file closed, front end parked, LED restored). Check the
newest file in `Results/` and confirm two files (main + _detail) with one row per second.

## Auto-start on boot

    sudo cp chrono-logger.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable chrono-logger.service    # start at every boot
    sudo systemctl start  chrono-logger.service    # start now

    systemctl status chrono-logger.service         # check state
    journalctl -u chrono-logger.service -f         # watch its output live

To stop: `sudo systemctl stop chrono-logger.service` (this closes the file
cleanly). To disable auto-start: `sudo systemctl disable chrono-logger.service`.

## Output format

Two comma-separated files per run, one row per scan:

  MAIN   Results/chrono_YYYY-MM-DD_HHMMSS.csv  (simple):
    iso_timestamp, elapsed_s, dev00_current_uA, ... dev11_current_uA
    -> devNN_current_uA = SMU-equivalent current V_REPORT/R_device (V_REPORT=0.5 V),
       computed from the corrected resistance; relates to prior 0.5 V data. Blank if open.

  DETAIL Results/chrono_YYYY-MM-DD_HHMMSS_detail.csv:
    iso_timestamp, elapsed_s, dev00_raw, dev00_actual_uA, dev00_R_ohm, ... dev11_R_ohm
    -> raw ADC, actual (mux-taxed) loop current, and series-corrected R_ohm (blank if open).

Each device value per row is the MEAN of all ADC reads taken during a ~75 ms
window for that device (mean raw ADC and the current computed from it). The
number of reads averaged depends on the board's per-read speed (tens to a couple
hundred on a Zero W) and is printed once at startup. Individual reads are not
stored. Note when reporting: one point/device/second = mean of ~N reads.

- `elapsed_s` comes from a monotonic clock and is always reliable.
- `iso_timestamp` is wall-clock time (see clock caveat below).
- Each row is flushed and fsync'd to the SD card immediately, so an unexpected
  power loss costs at most the row in progress, never the whole run.

## Things to know / tune

- CURRENT SCALING: the raw->current formula is carried over from the original
  project and is accurate. With the internal zero now at 20% of VREF, zero current
  corresponds to raw ~= -26214 (VOUT resting at INT_ZERO_V = 0.5 V), where the
  formula returns ~0 uA; at 2.75 kOhm the scale is ~36 counts/uA. raw = 0 maps to
  VOUT = VREF = 2.5 V, a real ~727 uA point, not the baseline -- so there is no
  offset in the data and absolute current is trustworthy, not just relative change.
  Raw ADC is logged alongside current, so you can still recompute later. During
  bring-up it is worth checking the scaling against a known resistor.

- SERIES RESISTANCE: the mux + wiring add ~318 ohm in series with each device.
  settings.device_ohms() removes it per channel (R_device = V_EFF/I_loop -
  R_SERIES[ch]) and logs the result as devNN_R_ohm. V_EFF (~0.51 V) and the
  12-entry R_SERIES table are in var.py, from the resistor-ladder calibration;
  re-run and update them after any rewire, reflow, or mux swap. At the 200 ohm
  warm end this subtracts ~318 of ~518 ohm, so accuracy rides on R_SERIES
  stability -- a low-R_on mux (e.g. ADG706) would make the correction negligible.

- TIA GAIN: fixed at 2.75 kOhm, the LOWEST internal gain, chosen so VOUT clears
  the rail at the LMP's ~750 uA drive ceiling. Being the floor, you cannot drop
  gain further internally: if raw still pins near the rail, lower the bias
  (REFCN_BIAS_0V5) or fit an external R_TIA below 2.75 kOhm. If raw barely moves
  off the noise floor at the cold end, raise R_TIA (accepting a lower peak) or
  switch gains per scan. Keep TIA_SETTING and R_TIA consistent.

- TIMING: in logger.py. Each device is settled (SETTLE_S = 3 ms) then read
  back-to-back for AVG_WINDOW_S = 75 ms and all reads averaged. (SETTLE + WINDOW)
  x 12 ~= 0.94 s, leaving margin in the 1 s interval. The window length rejects
  50/60 Hz mains pickup; the read COUNT within it cuts random noise by ~sqrt(N).
  Raise AVG_WINDOW_S toward 0.08 to use more of the second; lower it for a faster
  cadence. Startup prints how many reads/device actually fit on your board -- if
  you want to check that number directly, time readadc() in a loop first.
  Devices are biased continuously, so SETTLE_S can likely drop toward 0.

- CLOCK: the Zero W has no real-time clock. Deployed without network, the
  wall-clock timestamp will be whatever fake-hwclock restored at boot until (if
  ever) NTP corrects it. This does NOT affect syncing with other sensors on the
  SAME Pi - they all share the same system clock, so relative alignment holds,
  and elapsed_s is exact regardless. Only ABSOLUTE wall time is uncertain. If
  you need trustworthy absolute timestamps offline, add a small RTC module.

- RESILIENCE: a hard crash triggers a systemd restart into a NEW file (small
  gap, no data lost). If you see transient I2C read glitches in the field, we
  can add per-read error handling so one bad read never interrupts a run - easy
  add, left out for now to keep the loop simple.
