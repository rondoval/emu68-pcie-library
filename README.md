# emu68-pcie-library

PCIe bus support for AmigaOS running under [Emu68](https://github.com/michalsc/Emu68) on
Raspberry Pi 4 (BCM2711).

> **This is intended to become a proper AmigaOS dynamic library (`pcie.library`).**
> It currently ships as a CMake static library that is linked directly into consumers
> such as `xhci.device`.  As a consequence, each driver that links it gets its own
> private copy of the bus state — meaning **only a single driver can own and manage
> the PCIe bus at any given time**.  The long-term goal is to expose the same API as
> a shared, opened-by-name Amiga library so that multiple drivers and applications can
> co-exist on the same bus without conflicts.

---

## Status

Working.  The library initialises the Broadcom STB PCIe controller, brings the link up,
enumerates the bus hierarchy, allocates BARs and supports both traditional PCI INTx
interrupt lines and MSI.  It is actively used by `emu68-xhci-driver` to reach the VL805
USB 3.0 host controller soldered onto every Raspberry Pi 4.

> **Tested hardware:** VIA VL805 USB 3.0 host controller only.  Other PCIe devices may
> work but have not been validated.

---

## Repository layout

```
pcie.library/       Core library source and headers
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
  include/
    pci.h                   Main public include (bus, device, controller structs)
    pci_types.h             Portable type definitions
    pcie_brcmstb.h          Controller probe/remove/config API
    pci_auto.h              Auto-config API
    …

lspci/              Standalone AmigaOS CLI tool – initialises and enumerates the PCIe
                    bus and prints every discovered device.  Useful for bring-up and
                    debugging without needing a full driver stack.
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

## How xhci.device uses this library

`emu68-xhci-driver` links against `Emu68PCIe::pcie` and calls into the library during
unit initialisation (`unit.c : pcie_init()`):

1. **`brcm_pcie_probe()`** — configures the BCM2711 PCIe controller registers, negotiates
   the link and maps the MMIO window.
2. **`pci_bind_bus_devices()`** — enumerates the root bus; on RPi4 this discovers the
   PCIe-to-PCIe bridge and, behind it, the VIA VL805 USB 3.0 host controller.
3. **`pci_auto_config_devices()`** — assigns MMIO BARs to every discovered device so
   that the VL805 registers are mapped into the CPU address space.
4. **`bcm2711_reload_vl805_firmware()`** — asks VideoCore, via the mailbox property
   interface, to reload the VL805 firmware after PCI reset.
5. **`brcm_pcie_enable_msi()`** — arms the Broadcom MSI interrupt controller so that the
   VL805 can signal completions via MSI rather than a wired IRQ line.

Once `pcie_init()` returns, `xhci.device` locates the VL805 in the device list and
passes its mapped MMIO base to the xHCI stack.

---

## Roadmap

- [ ] Wrap the API behind a proper AmigaOS library base
- [ ] Clean up and stabilise the public API
- [ ] Test with additional PCIe devices beyond the VL805
