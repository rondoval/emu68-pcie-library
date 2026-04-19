# emu68-pcie-library

PCIe bus support for AmigaOS running under [Emu68](https://github.com/michalsc/Emu68) on
Raspberry Pi 4 and Compute Module 4 (CM4), both using the BCM2711 SoC.

---

## Overview


`bcmpcie.library` initialises the Broadcom STB PCIe controller, brings the link
up, enumerates the bus hierarchy, allocates BARs and supports both traditional PCI INTx
interrupt lines and MSI.  It is actively used by `emu68-xhci-driver` to reach the VIA VL805
USB 3.0 host controller soldered onto every Raspberry Pi 4.

`bcmpcie.library` is an AmigaOS dynamic library opened by name — multiple drivers can open
it concurrently.

An `openpci.library` compatibility shim is also provided for software that opens the library
by that name.  It forwards the classic openpci API but does not expose the BCM2711-specific
extensions.  See [developer-guide.md](developer-guide.md) for details.

> **Tested hardware:** VIA VL805 USB 3.0 host controller only.  Other PCIe devices may
> work but have not been validated.

---

## Installation

Copy the following files to your Amiga (or Emu68 SD card):

| File | Destination |
|---|---|
| `bcmpcie.library` | `LIBS:` |
| `openpci.library` | `LIBS:` (optional — see note below) |
| `lspci` | `C:` (optional, diagnostic tool) |

> **Note:** `openpci.library` is a compatibility shim for existing software that opens
> it by name.  It is optional and should **not** be installed on systems that already
> have a real PCI expansion board (Mediator, Prometheus, G-REX, …).

`gic400.library` must also be present in `LIBS:` at runtime — it is required for MSI
interrupt support.  Install it from the `emu68-gic400-library` package.

---

## lspci — bus diagnostic tool

`lspci` is a standalone AmigaOS CLI tool that opens `bcmpcie.library`, initialises the
PCIe controller, and prints every device it discovers on the bus.

```
C:lspci
```

Typical output on a Raspberry Pi 4:

```
[00:00.0] 0604: 14e4:2711  Broadcom BCM2711 PCIe Bridge
[01:00.0] 0c03: 1106:3483  VIA VL805 USB 3.0 Host Controller
```

Use `lspci` to verify that:
- The PCIe link is up (if no devices appear, the link did not train).
- BAR assignments are non-zero (a zero base address means auto-config did not run).
- The VL805 (or your target device) is visible at the expected BDF.

---

## Repository layout

```
bcmpcie.library/    Full bcmpcie.library implementation
  include/          Public headers (openpci.h, pcitags.h, pci_constants.h, pci_ids.h)
  src/              Library source (lifecycle, API dispatch, region/interrupt/DMA helpers)

openpci.library/    Compatibility shim: forwards openpci API to bcmpcie.library

pcie/               Shared internal PCIe engine (not a public API)
  src/
    pcie_brcmstb.c          Broadcom STB PCIe controller driver (ported from Linux)
    pcie_brcmstb_msi.c      MSI interrupt controller support
    vl805_reset.c           BCM2711 mailbox helper for reloading VL805 firmware
    pcie_msi.c              MSI vector management
    pci_probe.c             Bus and device enumeration
    pci_auto.c              BAR / resource auto-configuration
    pci_bar.c               BAR management helpers
    pci_capability.c        PCI capability list traversal
    pci_io.c                Config-space read/write primitives
    pci_lookup.c            Vendor / device ID look-up table
    pci_int.c               Traditional PCI INTx interrupt line management
    pci_util.c              Miscellaneous utilities

sfd/                bcmpcie.sfd — SFD file defining all library functions and LVOs

lspci/              Standalone AmigaOS CLI diagnostic tool
```

---

## Dependencies

| Package | Where | Purpose |
|---|---|---|
| `Emu68Common` | `emu68-common` | Pool allocators, `_SNPrintf`, shared utilities |
| `GIC400` | `emu68-gic400-library` | ARM GIC-400 interrupt controller – required for MSI |
| `mailbox` | `mailbox.resource` | Mailbox proto headers used by the VL805 firmware reload helper |

Both must be installed before building this project.

---

## Building

```sh
cmake -S . -B build \
   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake \
   -DCMAKE_PREFIX_PATH=/path/to/emu68-driver-stack \
   -DCMAKE_INSTALL_PREFIX=/path/to/emu68-driver-stack
cmake --build build
cmake --install build
```

Recommended workflow: install `devicetree.resource`, `mailbox.resource`, `emu68-common`, `emu68-gic400-library`, and this package into the same prefix.

If you keep dependencies in separate install trees instead, set `CMAKE_PREFIX_PATH` to the `mailbox.resource`, `emu68-common`, and `emu68-gic400-library` install prefixes.

The CMake package `Emu68PCIe` is then importable by downstream projects:

```cmake
find_package(Emu68PCIe REQUIRED)
target_link_libraries(my_driver PRIVATE Emu68PCIe::pcie)
```

---

## Roadmap

- [ ] Stabilise and document the public API (see [developer-guide.md](developer-guide.md))
- [ ] Validate with additional PCIe devices beyond the VL805
- [ ] Test multi-driver bus sharing beyond the current xhci.device use case
