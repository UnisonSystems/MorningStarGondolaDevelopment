# main.py  --  runs automatically when the Pico powers up.
#
# This is the Pico's equivalent of the systemd auto-start on the Zero W: whatever
# is in main.py executes on boot. Keeping it a one-liner means all the real logic
# stays in logger.py, and you can still stop/inspect from the REPL.
#
# To run the wiring self-test instead of logging, comment out the two lines below
# and, from the REPL (e.g. Thonny), type:  import selftest

import logger
logger.main()
