#   Based on original CVGIT var.py by Juan Aznar Poveda
#   Technical University of Cartagena, GIT
#   Trimmed + ported to Python 3 for headless multi-device chronoamperometry.
# -*- coding: utf-8 -*-
#
# This file holds the low-level configuration: it opens the SPI (ADC) and I2C
# (LMP91000 + MCP23017) links and defines the register constants used elsewhere.
# The cyclic-voltammetry bias/sweep tables from the original project have been
# removed; a constant-potential (chronoamperometry) measurement does not need them.

# On Raspberry Pi OS install "python3-smbus" (provides "smbus"). Fall back to
# smbus2 (pip install smbus2) if the classic module is absent; the interface used
# here (read_byte_data / write_byte_data) is identical between the two.
try:
    import smbus
except ImportError:
    import smbus2 as smbus

import spidev
import time
import sys

# ---------------------------------------------------------------------------
# SPI: ADC161S626 on the LMP91000EVM
# ---------------------------------------------------------------------------
spi = spidev.SpiDev()
spi.open(0, 0)              # SPI0, CE0 (physical pin 24), matches the EVM wiring
spi.mode = 1
spi.max_speed_hz = 1000000

# ---------------------------------------------------------------------------
# I2C: shared bus 1 carries BOTH the LMP91000 and the MCP23017 mux driver
# ---------------------------------------------------------------------------
bus = smbus.SMBus(1)

address = 0x48             # LMP91000 I2C address (analog front end)

# ---- LMP91000 register configuration --------------------------------------
LOCKWR = '00000000'        # unlock TIACN/REFCN for writing
LOCKRO = '00000001'        # lock them (read-only)

# Transimpedance gain (R_TIA). We use the LOWEST internal gain, 2.75 kOhm, so
# VOUT stays below the 3.3 V rail at the LMP's ~750 uA output-drive ceiling.
# Change TIA_SETTING (and R_TIA below) together to re-pick the gain.
TIACN_TIAG_2_75_RLOAD_010 = '00000100'   # R_TIA = 2.75 kOhm, R_LOAD = 10 ohm
TIACN_TIAG_7_00_RLOAD_010 = '00001100'   # R_TIA = 7 kOhm,  R_LOAD = 10 ohm
TIACN_TIAG_35_0_RLOAD_010 = '00010100'   # R_TIA = 35 kOhm (idle/deep-sleep config)
TIA_SETTING = TIACN_TIAG_2_75_RLOAD_010  # <-- tunable: the active gain

# Constant applied bias: +0.5 V (20% of VREF), internal zero at 20% of VREF (0.5 V).
# Chosen to match prior 0.5 V characterization data. Current is one-directional
# (oxidation only), so VOUT swings up from the 0.5 V rest point. Measured warm-end
# peak is ~420 uA loop (room temp; devices only cool from here), well under the
# 750 uA drive ceiling. REFCN byte 10011011 =
#   bit7  REF_SOURCE = 1    (external VREF)
#   6:5   INT_Z      = 00   (20% -> 0.5 V zero)
#   bit4  BIAS_SIGN  = 1    (positive, VWE > VRE)
#   3:0   BIAS       = 1011 (20% -> 0.5 V)
REFCN_BIAS_0V5 = '10011011'              # <-- +0.5 V constant potential, 20% zero

# Operation mode: 3-lead amperometric cell (used here in 2-wire via the EVM jumper)
MODECN_OP_MODE_3LEADAMPC = '00000011'
MODECN_OP_MODE_DEEPSLEEP = '00000000'    # idle / shutdown state

# ---- ADC -> current conversion constants ----------------------------------
VREF = 2.5                 # LMP91000 reference voltage (V)
VA = 5.0                   # single analog supply (V)
ADC_BITS = 16
BR = (2 ** ADC_BITS) - 1   # max decimal for 16-bit code (65535)
SPAN = VA - (VREF / (2 ** ADC_BITS))   # full-scale span used in the voltage eqn
INT_ZERO_V = 0.20 * VREF   # internal-zero rest voltage (20% of VREF = 0.5 V)
R_TIA = 2750               # <-- tunable: ohms, MUST match TIA_SETTING's gain

# ---------------------------------------------------------------------------
# Series-resistance correction (mux R_on + wiring)
# ---------------------------------------------------------------------------
# The analog mux adds ~318 ohm of on-resistance (at 3.3 V) in series with each
# device, so the device sees less than the applied bias. For an ohmic device the
# true resistance is recovered downstream (settings.device_ohms) as
#     R_device = V_EFF / I_loop - R_SERIES[channel]
# Constants below come from a resistor-ladder calibration (see SETUP.md); re-run
# that ladder and update them after any rewire, reflow, or mux swap.
V_EFF = 0.512              # effective loop bias (V) at 0.5 V nominal; MEASURED by
                           # a two-point solve (2200 + 470 ohm on every channel)
                           # and confirmed against an SMU'd 986.7-ohm resistor
                           # (rig read 994 ohm). Includes a fixed loop offset.
I_FLOOR_UA = 0.5           # below this a channel is treated as open (no device)
V_REPORT = 0.5             # voltage the main log reports current at: I = V_REPORT
                           # / R_device (SMU-equivalent, to match prior 0.5 V data)
R_SERIES = [435, 447, 441, 446, 431, 437,   # ch0-5   (0.5 V two-point solve)
            439,                             # ch6
            433,                             # ch7
            435,                             # ch8
            431, 443, 432]                   # ch9-11

# ---------------------------------------------------------------------------
# MCP23017 I2C GPIO expander -> drives the CD74HC4067 mux address lines
# ---------------------------------------------------------------------------
MCP_ADDR = 0x27            # confirmed via i2cdetect (A2/A1/A0 all shorted high)
MCP_IODIRA = 0x00          # Port A direction register (0 = output)
MCP_GPIOA = 0x12           # Port A output register

# Mux wiring: MCP Port A bits 0..3 -> CD74HC4067 address lines S0..S3 (S0 = LSB).
# The value written to the GPIOA low nibble selects the '4067 channel, so
# device N (0..11) maps directly to channel N with this straight bit order.
N_DEVICES = 12             # 12 devices on channels 0..11 of the 16-channel mux
