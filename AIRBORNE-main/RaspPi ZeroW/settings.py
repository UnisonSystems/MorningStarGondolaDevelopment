#   Based on original CVGIT settings.py by Juan Aznar Poveda
#   Technical University of Cartagena, GIT
#   Trimmed + ported to Python 3 for headless multi-device chronoamperometry.
# -*- coding: utf-8 -*-
#
# Low-level helpers: write LMP91000 registers over I2C, read the ADC over SPI,
# select a mux channel via the MCP23017, and convert a raw ADC count to current.

from var import *


# ---------------------------------------------------------------------------
# LMP91000 register access (I2C)
# ---------------------------------------------------------------------------
def write(value, reg):
    # Write byte `value` into LMP91000 register `reg`.
    bus.write_byte_data(address, reg, value)


def init(LOCK, TIACN, REFCN, MODECN):
    # Configure the LMP91000. Pass the binary-string constants from var.py;
    # they are converted to ints here. LOCK must be written first to enable
    # TIACN/REFCN writes.
    write(int(LOCK, 2), 1)
    write(int(TIACN, 2), 16)
    write(int(REFCN, 2), 17)
    write(int(MODECN, 2), 18)


# ---------------------------------------------------------------------------
# ADC161S626 read (SPI) -> signed 16-bit count
# ---------------------------------------------------------------------------
def readadc():
    # Read the ADC161S626 digital output and return a signed integer count.
    # The first 3 bits are discarded per the ADC datasheet (bin_r[2:18]).
    r = spi.readbytes(8)
    bin_r = r
    bin_r[0] = "{0:08b}".format(r[0])
    bin_r[1] = "{0:08b}".format(r[1])
    bin_r[2] = "{0:08b}".format(r[2])
    bin_r = bin_r[0] + bin_r[1] + bin_r[2]
    bin_r = bin_r[2:18]
    if bin_r[0] == '1':
        aux = bin_r.replace('1', '2').replace('0', '1').replace('2', '0')
        adcout = -int(aux, 2) - 1
    else:
        adcout = int(bin_r, 2)
    return adcout


def raw_to_current(raw):
    # Convert a raw ADC count to current in microamps, using the LMP91000
    # equations: V = (raw*SPAN/BR) + VREF ;  I = (V - INT_ZERO_V)/R_TIA * 1e6
    # INT_ZERO_V is the internal-zero rest voltage (20% of VREF = 0.5 V): VOUT
    # sits there at zero current, so it is the baseline subtracted here.
    volts = (raw * SPAN) / BR + VREF
    current_uA = ((volts - INT_ZERO_V) / R_TIA) * 1000000
    return volts, current_uA


def device_ohms(loop_uA, channel):
    # Series-resistance correction. loop_uA is the measured loop current from
    # raw_to_current(); the mux + wiring add R_SERIES[channel] ohms in series, so
    # for an ohmic device the true device resistance is V_EFF/I - R_SERIES[ch].
    # Returns ohms, or None when current is below I_FLOOR_UA (open / no device).
    if loop_uA is None or loop_uA < I_FLOOR_UA:
        return None
    return V_EFF / (loop_uA * 1e-6) - R_SERIES[channel]


# ---------------------------------------------------------------------------
# MCP23017 mux control (I2C)
# ---------------------------------------------------------------------------
def mux_init():
    # Set MCP23017 Port A as outputs (all 8 bits; we only drive the low 4).
    bus.write_byte_data(MCP_ADDR, MCP_IODIRA, 0x00)


def mux_select(channel):
    # Route CD74HC4067 channel `channel` (0..15) to the common line by writing
    # the channel number into the MCP Port A low nibble (S0..S3).
    bus.write_byte_data(MCP_ADDR, MCP_GPIOA, channel & 0x0F)
