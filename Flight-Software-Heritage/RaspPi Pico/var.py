# var.py  --  MicroPython (Raspberry Pi Pico) port of the AIRBORNE var.py
#
# Hardware config + constants for the headless 12-device chronoamperometry
# logger. Opens the shared I2C bus (LMP91000 + MCP23017) and the ADC's SPI bus,
# and defines every register / calibration value used by settings.py and
# logger.py. Ported from the Raspberry Pi Zero W version; the calibration
# numbers (bias, gain, series-R) are carried over UNCHANGED.
#
# ---------------------------------------------------------------------------
# WIRING -- set these GP numbers to match how the board is actually wired.
# The RP2040 has two I2C and two SPI peripherals; the SD card MUST live on the
# *other* SPI bus (SPI1) so it never contends with the ADC on SPI0.
#
#   Signal        Bus     Pico pin   Peripheral
#   -----------   -----   --------   -----------------------------------
#   SDA           I2C0    GP8        LMP91000 (0x48) + MCP23017 (0x27)
#   SCL           I2C0    GP9        (shared bus)
#   ADC SCLK      SPI0    GP18       LMP91000EVM ADC161S626
#   ADC MOSI      SPI0    GP19
#   ADC MISO      SPI0    GP16
#   ADC CS        gpio    GP17       (was CE0 on the Pi; toggled manually now)
#   SD  SCLK      SPI1    GP10       SD card module   <-- used by logger.py
#   SD  MOSI      SPI1    GP11
#   SD  MISO      SPI1    GP12
#   SD  CS        gpio    GP13
#
# POWER (standard 5 V USB powerbank -> Pico micro-USB; NO boost converter needed):
#   5 V rail = VBUS (pin 40, the micro-USB input voltage):
#       ADC analog supply (EVM 5 V pin)  AND  SD module VCC.
#     - the ADC161S626 needs 4.5-5.5 V on its analog supply.
#     - the SD module in use is the 5 V type, so its VCC comes from 5 V too, NOT
#       3.3 V (at 3.3 V its regulator browns the card out).
#   3.3 V rail = Pico 3V3(OUT) pin:  LMP91000, MCP23017, CD74HC4067 mux.
# ---------------------------------------------------------------------------

from machine import Pin, I2C, SPI

# ---- I2C: shared by the LMP91000 and the MCP23017 mux driver ----
I2C_ID  = 0
SDA_PIN = 8
SCL_PIN = 9
i2c = I2C(I2C_ID, sda=Pin(SDA_PIN), scl=Pin(SCL_PIN), freq=400000)

address = 0x48             # LMP91000 (analog front end)

# ---- SPI0: ADC161S626 on the LMP91000EVM ----
# The Pi used spidev with spi.mode = 1  ->  CPOL=0, CPHA=1  (polarity=0, phase=1).
# Chip-select is a plain GPIO toggled by hand (the kernel used to manage CE0).
ADC_SCK, ADC_MOSI, ADC_MISO, ADC_CS = 18, 19, 16, 17
spi = SPI(0, baudrate=1000000, polarity=0, phase=1,
          sck=Pin(ADC_SCK), mosi=Pin(ADC_MOSI), miso=Pin(ADC_MISO))
cs = Pin(ADC_CS, Pin.OUT, value=1)

# ---- SD card SPI bus pins (the bus + mount live in logger.py; the pin numbers
#      are kept here so all wiring is defined in one place). Keep OFF SPI0.
#      Module VCC = 5 V from VBUS (see POWER note above), GND to common ground. ----
SD_SCK, SD_MOSI, SD_MISO, SD_CS = 10, 11, 12, 13

# ===========================================================================
# LMP91000 register configuration  (unchanged from the Zero W version)
# ===========================================================================
LOCKWR = '00000000'        # unlock TIACN/REFCN for writing
LOCKRO = '00000001'        # lock them (read-only)

# Transimpedance gain: lowest internal gain, 2.75 kOhm, so VOUT stays under the
# rail at the LMP's ~750 uA drive ceiling.
TIACN_TIAG_2_75_RLOAD_010 = '00000100'   # R_TIA = 2.75 kOhm, R_LOAD = 10 ohm
TIACN_TIAG_7_00_RLOAD_010 = '00001100'   # R_TIA = 7 kOhm
TIACN_TIAG_35_0_RLOAD_010 = '00010100'   # R_TIA = 35 kOhm (idle/deep-sleep config)
TIA_SETTING = TIACN_TIAG_2_75_RLOAD_010  # <-- active gain (keep in sync with R_TIA)

# Constant applied bias: +0.5 V, internal zero at 20% of VREF (0.5 V).
#   byte 10011011: REF_SOURCE=1(ext), INT_Z=00(20%), BIAS_SIGN=1(+), BIAS=1011(20%)
REFCN_BIAS_0V5 = '10011011'

MODECN_OP_MODE_3LEADAMPC = '00000011'    # 3-lead amperometric (2-wire via jumper)
MODECN_OP_MODE_DEEPSLEEP = '00000000'    # idle / shutdown

# ===========================================================================
# ADC -> current conversion constants
# ===========================================================================
VREF = 2.5                 # LMP91000 reference (V)
VA = 5.0                   # single analog supply (V)
ADC_BITS = 16
BR = (2 ** ADC_BITS) - 1                 # 65535
SPAN = VA - (VREF / (2 ** ADC_BITS))     # full-scale span
INT_ZERO_V = 0.20 * VREF                 # internal-zero rest voltage (0.5 V)
R_TIA = 2750               # ohms; MUST match TIA_SETTING's gain

# ===========================================================================
# Series-resistance correction (mux R_on + wiring), per SETUP.md calibration
# ===========================================================================
V_EFF = 0.512              # effective loop bias (V) at 0.5 V nominal
I_FLOOR_UA = 0.5           # below this a channel is treated as open
V_REPORT = 0.5             # voltage the main log reports current at (SMU-equivalent)
R_SERIES = [435, 447, 441, 446, 431, 437,   # ch0-5
            439,                             # ch6
            433,                             # ch7
            435,                             # ch8
            431, 443, 432]                   # ch9-11

# ===========================================================================
# MCP23017 I2C GPIO expander -> CD74HC4067 mux address lines
# ===========================================================================
MCP_ADDR = 0x27            # A2/A1/A0 all high
MCP_IODIRA = 0x00          # Port A direction register (0 = output)
MCP_GPIOA = 0x12           # Port A output register
N_DEVICES = 12             # devices on mux channels 0..11
