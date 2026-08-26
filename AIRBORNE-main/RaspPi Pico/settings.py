# settings.py  --  MicroPython (Raspberry Pi Pico) port of the AIRBORNE settings.py
#
# Low-level helpers: write LMP91000 registers over I2C, read the ADC over SPI,
# drive the MCP23017 mux, and convert a raw ADC count to current / resistance.
# The conversion math is byte-for-byte the same as the Zero W version; only the
# bus calls change (smbus -> machine.I2C, spidev -> machine.SPI).

from var import (i2c, spi, cs, address,
                 SPAN, BR, VREF, INT_ZERO_V, R_TIA,
                 V_EFF, I_FLOOR_UA, R_SERIES,
                 MCP_ADDR, MCP_IODIRA, MCP_GPIOA)


# ---------------------------------------------------------------------------
# LMP91000 register access (I2C)
#   bus.write_byte_data(addr, reg, val)  ->  i2c.writeto_mem(addr, reg, bytes([val]))
# ---------------------------------------------------------------------------
def write(value, reg):
    i2c.writeto_mem(address, reg, bytes([value]))


def init(LOCK, TIACN, REFCN, MODECN):
    # LOCK must be written first to enable TIACN/REFCN writes.
    write(int(LOCK, 2), 1)
    write(int(TIACN, 2), 16)
    write(int(REFCN, 2), 17)
    write(int(MODECN, 2), 18)


# ---------------------------------------------------------------------------
# ADC161S626 read (SPI) -> signed 16-bit count
#   Reads 3 bytes (24 clocks); the first 2 leading bits are dropped and the next
#   16 form the two's-complement result -- the original bin_r[2:18] slice. CS is
#   asserted by hand for the transfer (the kernel used to handle CE0). NOTE: the
#   Pi read returned a mutable list, so the original mutated it in place; MicroPython
#   spi.read() returns immutable bytes, so the string is built directly instead.
# ---------------------------------------------------------------------------
def readadc():
    cs.value(0)
    r = spi.read(3)
    cs.value(1)
    bin_r = "{0:08b}".format(r[0]) + "{0:08b}".format(r[1]) + "{0:08b}".format(r[2])
    bin_r = bin_r[2:18]
    if bin_r[0] == '1':                       # negative (two's complement)
        aux = bin_r.replace('1', '2').replace('0', '1').replace('2', '0')
        adcout = -int(aux, 2) - 1
    else:
        adcout = int(bin_r, 2)
    return adcout


def raw_to_current(raw):
    # V = (raw*SPAN/BR) + VREF ;  I = (V - INT_ZERO_V)/R_TIA * 1e6
    volts = (raw * SPAN) / BR + VREF
    current_uA = ((volts - INT_ZERO_V) / R_TIA) * 1000000
    return volts, current_uA


def device_ohms(loop_uA, channel):
    # R_device = V_EFF / I_loop - R_SERIES[channel]. None if below the open floor.
    if loop_uA is None or loop_uA < I_FLOOR_UA:
        return None
    return V_EFF / (loop_uA * 1e-6) - R_SERIES[channel]


# ---------------------------------------------------------------------------
# MCP23017 mux control (I2C)
# ---------------------------------------------------------------------------
def mux_init():
    # Port A all outputs (we drive only the low 4 bits -> mux S0..S3).
    i2c.writeto_mem(MCP_ADDR, MCP_IODIRA, bytes([0x00]))


def mux_select(channel):
    # Write the channel number into the Port A low nibble.
    i2c.writeto_mem(MCP_ADDR, MCP_GPIOA, bytes([channel & 0x0F]))
