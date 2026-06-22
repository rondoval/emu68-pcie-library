# Release notes — bcmpcie.library 2.0

Changes since v1.1.

---

## Highlights

A new **typed, multi-vector interrupt-allocation API**, and full **MSI-X**
support.  Drivers now choose the interrupt type explicitly and can request more
than one vector (multi-vector MSI-X and multi-message MSI).

---

## Breaking changes

None to the ABI.  All v1.x functions keep their LVOs and behaviour; the new
functions are appended (LVOs -342 … -378), so drivers built against 1.x —
including `xhci.device` — continue to work without recompilation.

The previously *transparent* MSI-X behaviour (where `EnableMSI` could silently
pick MSI-X) has been removed: the obsolete calls are now strictly single-vector
**MSI or INTx, never MSI-X**.  MSI-X is reached only through the new API.

---

## New features

### Typed, multi-vector interrupt API

| LVO | Function | Purpose |
|-----|----------|---------|
| -342 | `LONG AllocIntVectors(dev, min, max, flags)` | Reserve `[min,max]` vectors of the best allowed type (MSI-X → MSI → INTx); returns the count |
| -348 | `void FreeIntVectors(dev)` | Release the allocation |
| -354 | `LONG AddIntVectorServer(dev, vec, isr)` | Install a server on vector `vec` (and unmask it) |
| -360 | `void RemIntVectorServer(dev, vec, isr)` | Remove a vector's server |
| -366 | `void MaskIntVector(dev, vec)` | ISR-safe per-vector mask |
| -372 | `void UnmaskIntVector(dev, vec)` | ISR-safe per-vector unmask |
| -378 | `ULONG GetIntVectorType(dev)` | Active type: `PCI_IRQ_MSIX/_MSI/_INTX` (0 if none) |

`flags` is a mask of `PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX`
(`PCI_IRQ_ALL_TYPES`) from `<libraries/pci_constants.h>`.  A type is attempted
only when its bit is set, so omitting a bit disables it — drop `PCI_IRQ_MSIX` to
forbid MSI-X, or pass `PCI_IRQ_INTX` alone to force the legacy line.

### MSI-X

MSI-X is fully supported on the BCM2711 root complex (which delivers it
identically to MSI — a write to the MSI doorbell, demuxed by the shared
aggregation interrupt).  This fixes drives whose single-message MSI is broken,
e.g. the Micron 2300, which now works via MSI-X.

### Multi-vector

`AllocIntVectors` can reserve several vectors: MSI-X uses any free demux slots
up to the device's table size; multi-message MSI uses a power-of-two,
2^k-aligned contiguous slot block with the Multiple-Message-Enable field set to
log2(n).

---

## Obsoletions

`EnableMSI` / `DisableMSI` / `pci_add_intserver` / `pci_rem_intserver` /
`MaskMSI` / `UnmaskMSI` are obsolete (still functional, single-vector MSI/INTx).
Use `AllocIntVectors` + `AddIntVectorServer` / `RemIntVectorServer` and
`MaskIntVector` / `UnmaskIntVector` instead.  See *Interrupts* in the developer
guide for equivalences.

`emu68-nvme-driver` and `emu68-xhci-driver` have been migrated to the new API
and now use MSI-X when the device and controller support it.

---

## Implementation notes

The interrupt code was refactored around a shared `pci_irq` core that owns the
controller demux-slot pool and the per-device active allocation; the obsolete
calls are thin wrappers over it (locked to MSI/INTx).  No public `struct pci_dev`
change.


# Release notes — bcmpcie.library 1.1

Changes since v1.0.

---

## Breaking changes

None.  The library ABI, the public/openpci API, and the client-facing DMA
allocation contract are unchanged.  Drivers built against 1.0 — including
`xhci.device` — continue to work without recompilation.

---

## New features

### ROM-able library

`bcmpcie.library` is now built ROM-able and the build enforces it: an
`emu68_rom_check` step fails the build if any writable `.data`/`.bss` sneaks
into the image.  The library can therefore be placed in `LIBS:` or embedded in
a ROM image without modification.

---

## Bug fixes / Improvements

### Region-restricted DMA pools

The shared DMA pool that backs `AllocDMAMem` / `AllocateDMAMemoryForBoard` is
now built from a region-restricted allocator (`dma_mem`) that draws only from
Emu68 (Pi-DRAM) Fast RAM the PCIe DMA engine can actually reach.  Previously the
pool was an ordinary `CreatePool(MEMF_PUBLIC | MEMF_FAST, …)`, which could hand
back Fast RAM the inbound PCIe window does not decode (for example
Zorro/accelerator Fast RAM).  Every buffer served from the pool is now
guaranteed to be DMA-reachable regardless of which device uses it.

If no DMA-reachable region exists (for instance when the controller comes up
with no usable device tree), the library now refuses to initialise rather than
returning unreachable memory.

The system-memory region registered with the PCIe controller is likewise
restricted to the RAM covered by the inbound window (`dma-ranges`); Fast RAM
outside that window is skipped so that PCI-bus/physical address translation can
never resolve to memory the engine cannot reach.  When `dma-ranges` is
unavailable the previous behaviour (register all Fast RAM) is kept as a
fallback.

### Correct `dma-ranges` parsing and full memory-list walk

The device-tree `dma-ranges` parser now advances the cell cursor by a full
record per iteration and stops on a partial trailing record (`len >=
cells_per_record`), and only decodes the record at the requested index instead
of overwriting the output on every pass.  The system-memory walk iterates the
entire exec `MemList` (terminating on the list tail) under `Forbid()`/`Permit()`
instead of capping at a fixed bank count, and the Fast RAM region size is now
computed from the exclusive upper bound (`end - start`) rather than
`end - start + 1`.

### Millisecond-based delay helpers

All controller bring-up and reset timing now uses `delay_ms()` instead of
`delay_us(n * 1000)` — link-up polling and PERST# settling in
`brcm_pcie_probe`, the SSC settle in `brcm_pcie_set_ssc`, the 100 ms FLR wait in
`pci_flr`, and the post-firmware-reload settle in `pcie_hw_init`.  Timing
behaviour is unchanged; the call sites are simply clearer.

### VL805 firmware-reload command array refactor

The mailbox command buffer in `bcm2711_reload_vl805_firmware` is now populated
field-by-field into a plain local array rather than via an aggregate
initialiser.  This keeps the firmware-reload helper free of writable static
state, supporting the ROM-ability guarantee above.  No functional change to the
reload sequence.


# Release notes — bcmpcie.library 1.0

## What's Changed
* initial release as a standalone library by @rondoval in https://github.com/rondoval/emu68-pcie-library/pull/2
* See README.md for details

**Full Changelog**: https://github.com/rondoval/emu68-pcie-library/commits/v1.0
