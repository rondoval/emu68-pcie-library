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
it concurrently.  It is built ROM-able (it contains no writable `.data`/`.bss`), so it can
be placed in `LIBS:` or embedded in a ROM image without modification.

An `openpci.library` compatibility shim is also provided for software that opens the library
by that name.  It forwards the classic openpci API but does not expose the BCM2711-specific
extensions.  In practice this is intended for tools and libraries such as
`identify.library` and other software that already expects the openpci ABI.
It has not yet been validated against a broad range of existing third-party PCI
drivers, so treat it as a compatibility aid rather than a drop-in replacement
for every historical openpci deployment.  See [developer-guide.md](developer-guide.md)
for API details and shim limitations.

This package also ships its own `lspci` command for bring-up and diagnostics, so
you can inspect the enumerated bus directly on an Emu68 system without relying
on external openpci tooling.

**Compatibility warning**
This library was till this day statically linked into xhci.device versions 3.x, thus making xhci.device initialize the bus without any arbitration.
As a result, this dynamic version of the library cannot be used while xhci.device versions 3.x and older is loaded.
If an older statically linked `xhci.device` is resident at the same time, both
copies can try to own the controller, enumerate the same devices, and program
interrupt or BAR state independently.  The result is undefined behaviour:
duplicate initialisation, conflicting interrupt setup, and devices being driven
through stale or mismatched bus state.  Upgrade to an `xhci.device` that opens
`bcmpcie.library` dynamically before installing this package.

---

## Installation

Copy the following files to your Amiga (or Emu68 SD card):

| File | Destination |
|---|---|
| `bcmpcie.library` | `LIBS:` |
| `openpci.library` | `LIBS:` (optional — see note below) |
| `lspci` | `C:` (optional, diagnostic tool) |

> **Note:** `openpci.library` is a compatibility shim for existing software that opens
> it by name, including utilities that probe for an openpci-compatible interface.
> It is optional and should **not** be installed on systems that already have a real
> PCI expansion board (Mediator, Prometheus, G-REX, …).

`gic400.library` must also be present in `LIBS:` at runtime — it is required for MSI
interrupt support.  Install it from the `emu68-gic400-library` package.

---

## lspci — bus diagnostic tool

`lspci` is a standalone AmigaOS CLI tool that opens `bcmpcie.library`, walks the
enumerated device list, and prints a concise summary of every device it discovers.
In its default mode it then follows with configuration-space, capability, bridge,
and BAR details that are useful during hardware bring-up.

```
C:lspci
```

Sample verbose output captured on a Raspberry Pi 4:

```
Bus 0 Dev 0 Func 0: Vendor 14E4 Device 2711 Class 06:04:00
  Class = 06:04:00 (rev 20)
  Command = 0x0006, Status = 0x0010
  Capability Pointer = 0x48
  Interrupt Line = 0x01, Pin = 0x01
  Header Type = 0x01 (PCI-to-PCI Bridge)
    Bridge bus numbers: primary=0 secondary=1 subordinate=1 sec_latency=0
    Bridge I/O range: type=0 base=0x00000000 limit=0x00000FFF
    Bridge memory range: base=0xC0000000 limit=0xC00FFFFF
    Bridge prefetchable memory range (64-bit): base=0x00000000FFF00000 limit=0x00000000000FFFFF
    Secondary status = 0x0000, Bridge control = 0x0000

    Standard PCI capabilities:
      off=0x48 id=0x01 (Power Management) next=0xAC
        dword0=0x4813AC01 dword1=0x00002008
      off=0xAC id=0x10 (PCI Express) next=0x00
        dword0=0x00420010 dword1=0x00008002
        PCIe Flags=0x0042

    PCIe extended capabilities:
      off=0x100 id=0x0001 (Advanced Error Reporting) ver=1 next=0x180
        dword0=0x18010001 dword1=0x00000000
      off=0x180 id=0x000B (Vendor-Specific) ver=1 next=0x240
        dword0=0x2401000B dword1=0x02800000
      off=0x240 id=0x001E (L1 PM Substates) ver=1 next=0x000
        dword0=0x0001001E dword1=0x0028081F

Bus 1 Dev 0 Func 0: Vendor 1106 Device 3483 Class 0C:03:30
  BAR0: addr=0xFA000000 size=0xFFFFF000
  Class = 0C:03:30 (rev 01)
  Command = 0x0146, Status = 0x0010
  Capability Pointer = 0x80
  Interrupt Line = 0x01, Pin = 0x01
  Header Type = 0x00 (Endpoint)

    Standard PCI capabilities:
      off=0x80 id=0x01 (Power Management) next=0x90
        dword0=0x89C39001 dword1=0x00000000
      off=0x90 id=0x05 (MSI) next=0xC4
        dword0=0x0084C405 dword1=0x00000000
        MSI: ctl=0x0084 addr_lo=0x00000000
      off=0xC4 id=0x10 (PCI Express) next=0x00
        dword0=0x00020010 dword1=0x00008001
        PCIe Flags=0x0002

    PCIe extended capabilities:
      off=0x100 id=0x0001 (Advanced Error Reporting) ver=1 next=0x000
        dword0=0x00010001 dword1=0x00000000
```

For scripts or log scraping, `QUIET/S` switches to a one-line-per-device format:

```
C:lspci QUIET
```

Sample quiet output:

```
0:00.0 vendor=14E4 device=2711 class=06:04:00 owner=-
1:00.0 vendor=1106 device=3483 class=0C:03:30 owner=- bar0=0xFA000000,0xFFFFF000
```

Use `lspci` to verify that:
- The PCIe link is up (if no devices appear, the link did not train).
- BAR assignments are non-zero in the detailed output (a zero base address means
  auto-config did not run).
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

All listed dependencies must be installed before building this project.

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

- [ ] Validate with additional PCIe devices beyond the VL805
- [ ] Exercise the `openpci.library` shim with more existing openpci-aware software
- [ ] Test multi-driver bus sharing beyond the current xhci.device use case
