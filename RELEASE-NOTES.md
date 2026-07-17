# Release notes — bcmpcie.library 2.1

Changes since v2.0.

---

## Breaking changes

None.

---

## Bug fixes

### No longer crashes on non-Emu68 systems

Opening the library on an Amiga without PiStorm/Emu68 used to crash the machine.
It now fails cleanly instead: `OpenLibrary` returns `NULL`, so software that needs
PCIe can handle its absence gracefully.


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
| -342 | `LONG AllocIntVectors(dev, min, max, flags)` | Reserve `[min,max]` vectors of the best allowed type (MSI-X → MSI → INTx); returns the count (≥ `min`), or a negative `PCIE_ERR_*` |
| -348 | `void FreeIntVectors(dev)` | Release the allocation |
| -354 | `LONG AddIntVectorServer(dev, vec, isr)` | Install a server on vector `vec` (and unmask it); `PCIE_OK` or a negative `PCIE_ERR_*` |
| -360 | `void RemIntVectorServer(dev, vec, isr)` | Remove a vector's server |
| -366 | `BOOL MaskIntVector(dev, vec)` | ISR-safe per-vector mask; returns whether the mask took effect |
| -372 | `BOOL UnmaskIntVector(dev, vec)` | ISR-safe per-vector unmask; returns whether the unmask took effect |
| -378 | `ULONG GetIntVectorType(dev)` | Active type: `PCI_IRQ_MSIX/_MSI/_INTX` (0 if none) |

`flags` is a mask of `PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX`
(`PCI_IRQ_ALL_TYPES`) from `<libraries/pci_constants.h>`.  A type is attempted
only when its bit is set, so omitting a bit disables it — drop `PCI_IRQ_MSIX` to
forbid MSI-X, or pass `PCI_IRQ_INTX` alone to force the legacy line.

`MaskIntVector` / `UnmaskIntVector` return `BOOL`: `TRUE` when the change took
effect.  For level-triggered — and possibly shared — INTx, `FALSE` means the
change was deferred because the pending state did not match the request (on
mask, nothing was pending, so it is not our line; on unmask, an interrupt is
still pending — drain and retry).  This is the same INTx pending-guard the
obsolete `CheckSetINTxMask` provided.  MSI-X per-vector masking is mandatory and
always returns `TRUE`; for MSI, `FALSE` means the device has no Per-Vector
Masking Capability and so cannot be masked at the device.

### Typed error codes

The `LONG`-returning calls — `AllocIntVectors`, `AddIntVectorServer`,
`EnableMSI` and `FLR` — now report a typed code from the new public header
`<libraries/bcmpcie_errors.h>`: `PCIE_OK` (0) on success, or a negative
`enum pcie_error` (`PCIE_ERR_INVAL`, `_BUSY`, `_NODEV`, `_NOTSUPP`, `_NOMEM`,
`_IO`).  `AllocIntVectors` returns the positive vector count instead of
`PCIE_OK`.  Every failure is negative, so existing `< 0` / `< 1` / `!= 0` caller
checks keep working unchanged.  A header-only `pcie_strerror()` is provided for
debug logging — no extra library entry point.

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
`MaskMSI` / `UnmaskMSI` / `CheckSetINTxMask` are obsolete (still functional,
single-vector MSI/INTx, never MSI-X).  Use `AllocIntVectors` +
`AddIntVectorServer` / `RemIntVectorServer` and `MaskIntVector` /
`UnmaskIntVector` instead — the latter pair carries the same INTx pending-guard
status that `CheckSetINTxMask` returned.  `EnableMSI` and `FLR` now also report
the typed `PCIE_ERR_*` codes described above (their negative-on-failure contract
is unchanged).  See *Interrupts* in the developer guide for equivalences.

`emu68-nvme-driver` and `emu68-xhci-driver` have been migrated to the new API
and now use MSI-X when the device and controller support it.

---

## Implementation notes

The interrupt code was refactored around a shared `pci_irq` core that owns the
controller demux-slot pool and the per-device active allocation; the obsolete
calls are thin wrappers over it (locked to MSI/INTx).  No public `struct pci_dev`
change.

`gic400.library` ownership moved out of the library shim and into the controller
layer: it is now opened by `brcm_pcie_probe` (`brcm_pcie_open_gic400`) and the
`pcie.library` base no longer holds a `gic400Base`.  It is still required either
way — the BCM2711 MSI/MSI-X demux is delivered on a GIC aggregation interrupt
(a GIC SPI), and per-device INTx lines are GIC SPIs too — so this is a structural
change, not a dependency change.

---

## Build & tooling

* The embedded `$VER:` strings of `bcmpcie.library` and `openpci.library` are now
  stamped `MAJOR.MINOR` (the patch component is dropped), and `lspci` now carries
  a `$VER:` version stamp of its own.
* Stack-wide debug-backend selection: `-DEMU68_DEBUG_BACKEND=pistorm|serial|off`
  (via `emu68-common`).  `serial` routes debug to the AmigaOS serial console and
  is not ROM-able; `off` compiles debug out.
* Build adjustments for NDK 3.9 and `-O3`.
* A CI versioning / release-check workflow was added.


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
