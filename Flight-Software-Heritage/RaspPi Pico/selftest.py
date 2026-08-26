# selftest.py  --  wiring check to run BEFORE the logger.
#
# Copy this + var.py + settings.py onto the Pico, then from the MicroPython REPL:
#     import selftest
# It scans the I2C bus (you should see the LMP and the MCP), configures the front
# end, and reads every mux channel once, printing raw ADC / current / resistance.
# No SD card needed for this test.

import time
import var
from settings import (init, readadc, raw_to_current, device_ohms,
                      mux_init, mux_select)
from var import (LOCKWR, TIA_SETTING, REFCN_BIAS_0V5,
                 MODECN_OP_MODE_3LEADAMPC, N_DEVICES, MCP_ADDR)

found = var.i2c.scan()
print("I2C devices found:", [hex(a) for a in found])
print("  expect 0x48 (LMP91000) and 0x{:02x} (MCP23017)".format(MCP_ADDR))
if 0x48 not in found:
    print("  !! LMP91000 (0x48) not seen -- check SDA/SCL, power, grounds.")
if MCP_ADDR not in found:
    print("  !! MCP23017 (0x{:02x}) not seen -- check its address straps / wiring."
          .format(MCP_ADDR))

mux_init()
init(LOCKWR, TIA_SETTING, REFCN_BIAS_0V5, MODECN_OP_MODE_3LEADAMPC)
time.sleep_ms(50)

print("\n ch      raw   current_uA   R_ohm")
for dev in range(N_DEVICES):
    mux_select(dev)
    time.sleep_ms(5)
    raw = readadc()
    _v, i_uA = raw_to_current(raw)
    r = device_ohms(i_uA, dev)
    print(" {:2d}   {:7d}   {:9.2f}   {}".format(
        dev, raw, i_uA, "open" if r is None else "{:.0f}".format(r)))

print("\nIf raw values move sensibly as you change what's on each channel, and "
      "the two I2C addresses show up, the wiring is good.")
