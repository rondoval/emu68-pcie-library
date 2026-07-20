# emu68-pcie-library

## Architecture Notes

**Two public libraries:**
- `bcmpcie.library` — full BCM2711-specific API (openpci v2 + BCM extensions v3 tag API)
- `openpci.library` — thin shim over bcmpcie for classic openpci compatibility; new code should use bcmpcie directly

**Source layout:** generic PCIe core lives in `pcie/src/` (glob-compiled into the library); the AmigaOS library glue (entry point, LVO handlers, jump table) lives in `bcmpcie.library/src/`.

**Key module roles:**

| Module | Role |
|---|---|
| `pcie/src/pcie_brcmstb.c` | BCM STB controller bring-up, port training (ported from Linux) |
| `pcie/src/pcie_brcmstb_msi.c` | BCM2711 PCIe MSI block setup / dispatch (GIC IRQ 180) |
| `pcie/src/pci_auto.c` | BAR sizing and resource auto-configuration |
| `pcie/src/pcie_msi.c` | MSI vector allocation, routing to GIC-400 SPIs |
| `pcie/src/pci_probe.c` | Bus enumeration |
| `pcie/src/vl805_reset.c` | VL805 firmware reload via `mailbox.resource` after PCIe reset |
| `bcmpcie.library/src/pcie_main.c` | Library entry point, device list, jump table |
| `bcmpcie.library/src/pcie_irq.c` | Interrupt registration (`LibAddIntServer`/`LibRemIntServer`/`LibEnableMSI`); MSI add/rem serialise on `base->semaphore` — open-time API, not an ISR-context helper |

**`pci_dev.reserved`** is repurposed as a back-pointer to the internal `pci_device` struct. Do not overwrite it.

**VL805:** The VIA VL805 USB chip on Pi 4 needs firmware loaded via VideoCore mailbox (`mailbox.resource`) after PCIe link training completes (see `pcie/src/vl805_reset.c`).

## Role

- `bcmpcie.library` is a real AmigaOS dynamic library opened by name (`OpenLibrary("bcmpcie.library", 1)`), shared by multiple consumers. It is built ROM-able (no writable `.data`/`.bss`, enforced by `emu68_rom_check`).
- Consumers (e.g. `emu68-xhci-driver`, `lspci`) open it by name and link only against the SFD-generated headers (`Emu68PCIe::pcie_headers`), not a static archive.
- `openpci.library` is a thin shim that opens `bcmpcie.library` on first use and forwards the classic openpci API (no BCM2711 extensions).
- `lspci` is the standalone bring-up and debugging tool for enumeration and BAR assignment.

## Build Commands

Required installed dependencies: `emu68-common`, `emu68-gic400-library`, and `mailbox` (the library links `mailbox.resource` for VL805 firmware reload). The top-level project builds three targets: `pcie_library` (output `bcmpcie.library`), `openpci_library` (output `openpci.library`), and `lspci`.

Build through the superbuild's container wrapper — never host `cmake` (build trees
are configured at `/work` inside the toolchain container), from the
`emu68-driver-stack` superbuild root:

```sh
./scripts/docker-build.sh --target emu68-pcie-library
```

Debug backend: `EMU68_CONFIGURE_ARGS="-DEMU68_DEBUG_BACKEND=serial" ./scripts/docker-build.sh` (default `pistorm` | `serial` | `off`). Selected stack-wide via `emu68-common`; `serial` links `debug.lib` and is not ROM-able.

SFD headers are generated at build time by `cmake/GenerateSfdHeaders.cmake` (target `pcie_library_sfd_headers`) from `sfd/bcmpcie.sfd` into `build/bcmpcie.library/generated/include`.

## SFD and Jump Table Discipline

- The SFD file (`sfd/bcmpcie.sfd`) and the C jump table in `bcmpcie.library/src/pcie_main.c` must stay in sync at all times.
- LVO offset comments are documentation only but must reflect the actual position of each entry. Each slot is 6 bytes; LVOs run in steps of 6 (e.g. -30, -36, -42, …).
- When adding entries, append at the end of the table and assign the next available LVO. Update the section comment if starting a new group.
- **Default removal policy: do NOT remove an entry and renumber.** Instead, replace it with a null stub (`(APTR)LibStub_<Name>` or `(APTR)NULL`) and mark it reserved/deprecated in the SFD. Only renumber when explicitly asked to compact the table.
- If renumbering is explicitly requested: removing N entries shifts every following LVO by +6N. Update every subsequent LVO comment in both files, plus any section header comments.
- Always verify SFD section header LVOs match the first function in that group after any change.

## Internal Device Model

- `struct pci_device` (internal, not public) carries a BAR cache: `header_type`, `bars_num`, and `bars[6]` (`struct pci_bar_info`). Populated during `pciauto_setup_device`; must not be reprobed at open time.
- `pci_bar_info.bar_response` is `pci_size_t` (64-bit when `CONFIG_SYS_PCI_64BIT` is enabled). Stores the raw PCI BAR sizing response including all flag bits. The old `size_mask` field has been removed.
- `pdev->base_address[i]` is already the 68K virtual MMIO pointer for BAR `i`, set from `idev->bars[i].virt_addr` by `pcie_make_pdev`. Do not call `pci_map_bar` or `pci_bus_to_virt` again at open time.
- `pdev->base_size[i]` stores `(ULONG)idev->bars[i].bar_response` — the raw sizing response with flags in the low bits. `PRM_MemorySize` masks type bits before computing size.
- `bars_num` and `header_type` are set once in `pci_create_device` and must not be overwritten by `pci_bind_bus_devices` on the existing-device path.
- BAR slot index must be computed as `(bar_reg - PCI_BASE_ADDRESS_0) / 4`; do not use a `bar_nr` counter that increments only once per 64-bit BAR pair.
- `PRM_BoardOwner` in `LibSetBoardAttrsA`: non-NULL value only succeeds if `dev->owner == NULL`; NULL always clears unconditionally.

## Address Translation

- `LibLogicToPhysic` and `LibPhysicToLogic` handle two address spaces:
  - **BAR windows**: `virt_addr` ↔ `bus_addr` are both in the BAR cache; offset arithmetic applied directly.
  - **DMA RAM (Fast RAM)**: logical == ARM physical (1:1), so `pci_phys_to_bus` / `pci_bus_to_phys` are used as fallback.
- Both functions return NULL when no `pci_dev` is supplied.
- `LibObtainPCIRegion` only searches MEM BARs; IO BARs (`bar->type == PCI_REGION_IO`) are skipped.

## Code Handling

- Be conservative with public PCIe API changes; `emu68-xhci-driver` opens `bcmpcie.library` by name and depends on its LVO/SFD ABI. Adding/removing/reordering jump-table entries breaks resident consumers.
- The library is opened concurrently by multiple consumers — keep shared/global state safe under `base->semaphore`, and avoid baking in single-owner assumptions.
- Preserve the current BCM2711 + VL805 assumptions unless the task is explicitly widening hardware support.
- Licensing was audited against U-Boot/Linux provenance. Preserve existing file-level SPDX headers; do not overwrite the special non-GPL provenance on `pcie/include/bcm2711.h`.

## Validation

- Small changes can be validated in this repo alone.
- Interface or behavior changes affecting xHCI should also be validated by rebuilding `emu68-xhci-driver`, preferably through `emu68-driver-stack`.