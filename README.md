# emu68-pcie-library

Work in progress. Most of the code is not consistent, with gaps and not tested.
Though, it currently builds a little command line tool that:

- configures the PCIe controller and brings the link up
- enumerates bus 0, configures the PCIe bridge and enumerates it's bus

The end result is we actually talk to the VL805 XHCI controller on RPi4 :)