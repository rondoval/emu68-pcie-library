# emu68-pcie-library Agent Notes

## Role

- This repo currently ships the PCIe support code as a static library linked into consumers, not as a shared opened-by-name Amiga library.
- The intended direction is to turn `pcie.library` into a real dynamic library shared between multiple users; treat upcoming work in this repo as part of that transition unless a task is explicitly limited to current behavior.
- `lspci` is the standalone bring-up and debugging tool for enumeration and BAR assignment.

## Build

- Required installed dependencies: `emu68-common` and `emu68-gic400-library`.
- Preferred commands:
  - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_PREFIX_PATH=/path/to/emu68-driver-stack -DCMAKE_INSTALL_PREFIX=/path/to/emu68-driver-stack`
  - `cmake --build build`
  - `cmake --install build`

## Code Handling

- Be conservative with public PCIe API changes; `emu68-xhci-driver` links this directly.
- Keep the future shared-library design in mind when introducing new state, ownership assumptions, or APIs, even if the current implementation is still static-linked.
- Preserve the current BCM2711 + VL805 assumptions unless the task is explicitly widening hardware support.
- Changes to enumeration, BAR assignment, or MSI setup should be validated with both library consumers and `lspci` where possible.
- Do not treat `pcie.library` as a real shared library yet when reasoning about current runtime state ownership, but avoid cementing assumptions that would block the planned multi-user dynamic-library model.
- Licensing in this repo was audited against its U-Boot/Linux provenance. Preserve existing file-level SPDX headers; in particular, do not overwrite the special non-GPL provenance on `pcie.library/include/bcm2711.h`.

## SFD and Jump Table Discipline

- The SFD file (`sfd/bcmpcie.sfd`) and the C jump table in `bcmpcie.library/src/pcie_main.c` must stay in sync at all times.
- LVO offset comments in both files are documentation only, but must reflect the actual position of each entry. Each slot is 6 bytes, so LVOs run in steps of 6 (e.g. -30, -36, -42, …).
- When adding entries, append at the end of the table and assign the next available LVO. Update the section comment if starting a new group.
- **Default removal policy: do NOT remove an entry and renumber.** Instead, replace it with a null stub in the jump table (`(APTR)LibStub_<Name>` or `(APTR)NULL`) and mark it reserved/deprecated in the SFD (e.g. `* RESERVED (was FooBar)`). This preserves all LVO positions for existing callers. Only renumber when explicitly asked to compact the table.
- If renumbering is explicitly requested: removing N entries shifts every following LVO by +6N (the negative magnitude decreases by 6N). Update every subsequent LVO comment in both the SFD and the jump table, and update any section header comments (e.g. `* -288: Extended config space access`) to match the new first entry in that group.
- Always verify SFD section header LVOs match the first function in that group after any change.

## Internal Device Model

- `struct pci_device` (internal, not public) now carries a BAR cache: `header_type`, `bars_num`, and `bars[6]` (`struct pci_bar_info`). These are populated during `pciauto_setup_device` and must not be reprobed at open time.
- `pci_bar_info.bar_response` is `pci_size_t` (64-bit when `CONFIG_SYS_PCI_64BIT` is enabled). It stores the raw PCI BAR sizing response including all flag bits. The old `size_mask` field has been removed.
- `pdev->base_address[i]` (openpci `struct pci_dev`) is already the 68K virtual MMIO pointer for BAR `i`, set directly from `idev->bars[i].virt_addr` by `pcie_make_pdev`. There is no need to call `pci_map_bar` or `pci_bus_to_virt` again at open time.
- `pdev->base_size[i]` stores `(ULONG)idev->bars[i].bar_response` — the raw sizing response with flags in the low bits (openpci convention: `actual_size = ~(base_size[i] & mask) + 1`). `PRM_MemoryFlags` reads `base_size[i] & 0xF`; `PRM_MemorySize` masks type bits before computing size.
- `bars_num` and `header_type` are set once in `pci_create_device` and must not be overwritten by `pci_bind_bus_devices` on the existing-device path.
- BAR slot index must be computed as `(bar_reg - PCI_BASE_ADDRESS_0) / 4`; do not use a `bar_nr` counter that increments only once per 64-bit BAR pair, as it diverges from the physical slot index.
- `PRM_BoardOwner` in `LibSetBoardAttrsA`: sets `dev->owner` directly under semaphore. Non-NULL value only succeeds if `dev->owner == NULL` (returns FALSE otherwise). NULL always clears the owner unconditionally.

## Address Translation

- `LibLogicToPhysic` and `LibPhysicToLogic` handle two address spaces:
  - **BAR windows**: `virt_addr` (emu68 logical) ↔ `bus_addr` (PCI bus address) are both in the BAR cache; offset arithmetic is applied directly without going through the MMU or DMA region table.
  - **DMA RAM (Fast RAM)**: logical == ARM physical (1:1), so `pci_phys_to_bus` / `pci_bus_to_phys` are used as fallback.
- Both functions return NULL when no `pci_dev` is supplied.
- `LibObtainPCIRegion` only searches MEM BARs; IO BARs (`bar->type == PCI_REGION_IO`) are skipped per spec: "This function can only gain access to memory mapped regions, not to IO mapped regions."

## Validation

- Small changes can be validated in this repo alone.
- Interface or behavior changes that affect xHCI should also be validated by rebuilding `emu68-xhci-driver`, preferably through `emu68-driver-stack`.

