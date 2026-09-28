# Release notes — bcmpcie.library 2.5

Changes since v2.4. Needs emu68-common 2.0.0.

---

## Breaking changes

None.

---

## Improvements

### `MaskIntVector` masks MSI and MSI-X at the root complex

For MSI and MSI-X, `MaskIntVector` / `UnmaskIntVector` now gate the vector in
the Pi's PCIe controller (its MSI demux) instead of at the device. That is a
register write inside the Pi rather than a transaction over the PCIe link, so
it is cheap enough to call on every interrupt: mask in the interrupt server,
unmask once the task has drained, and a message that arrives in between fires
on unmask. Both calls now always return `TRUE` for MSI and MSI-X, including
MSI devices without per-vector mask bits, which used to return `FALSE`. The
device-level mask is now only opened by `AddIntVectorServer` and closed by
`RemIntVectorServer`.

### INTx lines are not promised to be exclusive

An INTx line can carry more than one function — the pin swizzle maps the whole
device tree onto four lines. `gic400.library` takes one interrupt server per
interrupt, so the second device on a line gets `PCIE_ERR_BUSY` today; that is
gic400's limitation, not this library's, and it is expected to go. When gic400
gains server chaining, the call starts succeeding and every server on the line
is called for every interrupt on it — with no change to this library.

Write INTx interrupt servers for that now. Test a register of your own device
first and return "not handled" — with the Z flag set, per
`exec.library/AddIntServer` — unless it raised the interrupt. Config space is
not reachable from an interrupt server, so the test has to be a memory-mapped
register. `MaskIntVector` needs no change of habit: on INTx it gates your device
alone, which is what makes mask-in-the-server / unmask-after-the-drain safe on a
shared line. MSI and MSI-X vectors are device-private and are never shared.
The developer guide (§9) and `interrupt-chaining.md` in the gic400 component
have the details.

### Interrupt vectors

- `FreeIntVectors` first detaches any interrupt server that is still attached.
  Before, freeing an INTx allocation with its server attached left the server
  registered with gic400, and that line could not be used again.
- A vector takes one interrupt server: `AddIntVectorServer` on a vector that
  already has one returns `PCIE_ERR_BUSY` (it used to replace the server
  silently), and `RemIntVectorServer` ignores a server that is not the
  vector's.
- A device's MSI-X table is located and checked when the device is probed. A
  device whose table cannot be reached is treated as having no MSI-X and gets
  MSI or INTx.
- The obsolete `pci_add_intserver` works on a device that has MSI but no INTx
  pin (after `EnableMSI`). It used to refuse.

### MSI dispatch

- The root complex delivers MSI and MSI-X only for vectors that have a server
  attached. A message for any other vector is held and discarded when that
  vector next gets a server; before, it raised an interrupt that nobody
  handled.

- The MSI dispatcher hands each interrupt server the Exec base it received
  instead of reading address 4 for every vector, and no longer waits for its
  controller register accesses to complete.
- The MSI demux interrupt server reports "not handled" when the controller's
  MSI status register was empty, instead of always claiming the interrupt. It
  makes no difference while its interrupt is its own, and is what lets it share
  one with something else later.
- Interrupt servers may now use `A5` freely, as Exec allows. Before, a server
  that changed `A5` could corrupt the dispatcher.

---

## Fixes

### INTx

- Masking an INTx interrupt never took effect, so removing an INTx interrupt
  server left the device free to keep asserting its line. `MaskIntVector` /
  `UnmaskIntVector` (and the obsolete `CheckSetINTxMask`) now write the PCI
  command register's INTx-disable bit unconditionally and return `TRUE`. The
  old "deferred, interrupt pending" `FALSE` result is gone: the write gates the
  one device, so there is no shared-line pending state to check first. For INTx
  these calls touch config space, so call them from a task, not from an
  interrupt server.
- A device whose INTx pin has no entry in the controller's interrupt map was
  registered on the wrong GIC interrupt. It now gets no INTx at all
  (`AllocIntVectors` with only `PCI_IRQ_INTX` returns `PCIE_ERR_NODEV`).
- `AddIntVectorServer` on an INTx line that already has a server returns
  `PCIE_ERR_BUSY` instead of `PCIE_ERR_IO`.

### Bus enumeration

- **A PCI-to-PCI bridge behind which the bus could not be allocated was probed
  through an uninitialised pointer.** `pci_create_bus()` leaves its output
  pointer untouched when it fails, and its return value was ignored, so an
  allocation failure while enumerating a bridge sent `pci_probe_bus()` into a
  stale stack slot. Out-of-memory during enumeration is rare, which is why this
  was never seen — the failure would have been a crash at boot with no
  diagnostic. The return value is now checked and enumeration stops with the
  error.

---

# Release notes — bcmpcie.library 2.4

Changes since v2.3.

---

## Breaking changes

None.

---

## New features

### Can be built into a custom Kickstart ROM

`bcmpcie.library` now initialises early enough in the Kickstart boot sequence to
serve `xhci.device` and `nvme.device` before DOS exists. That is what makes a
ROM image that boots from a USB or NVMe drive possible.

Nothing changes for the normal `LIBS:bcmpcie.library` installation.

---

## Build & tooling

- Hardcoded `-m68040` removed — it overrode the toolchain's `M68K_CPU`, so
  non-68040 builds produced 68040 code.

---

# Release notes — bcmpcie.library 2.3

Changes since v2.2.

---

## Breaking changes

None.

---

## Improvements / Maintenance

### New shared PCIe interrupt-vector helper

A new public header, `include/libraries/pci_irq.h`, wraps the typed
multi-vector interrupt API introduced in 2.0 into two inline calls:
`pci_irq_attach()` (`AllocIntVectors` → `GetIntVectorType` →
`AddIntVectorServer`, unwinding cleanly if the server add fails) and
`pci_irq_detach()` (`RemIntVectorServer` + `FreeIntVectors`). It's unrelated
to the existing internal `pcie/src/pci_irq.c` (the MSI/MSI-X allocation core
this API is built on) — this is a small caller-side convenience header, not
a change to that engine. No change to the underlying API or its
MSI-X → MSI → INTx behavior — existing callers using the raw LVOs directly
are unaffected.

### `brcm_pcie_wait_mdio_value` uses the shared `mmio_poll_timeout` helper

The BCM2711 MDIO register wait loop now calls `emu68-common`'s shared
`mmio_poll_timeout()` instead of a hand-rolled poll loop. Same timeout
semantics. No functional change.

### GCC 16.1 build portability

`bcmpcie.library` and `openpci.library` now build cleanly under GCC 16.1. No
behavior change:

- `-ffreestanding` moved from link options to compile options, where it
  actually affects code generation — as a link-only flag it was silently
  inert.

### Dependencies

Building `bcmpcie.library` now requires **`emu68-common` 1.9.0** or later
(`mmio_poll_timeout()`, `include/iomem.h`).

---

# Release notes — bcmpcie.library 2.2

Changes since v2.1.

---

## Breaking changes

None.

---

## Improvements / Maintenance

### Debug output follows emu68-common's tier ladder

Verbose logging now gates on `TRACE` instead of the old `DEBUG_HIGH`, and logs
through `KprintfT` instead of `KprintfH`, matching emu68-common's cumulative
`PROFILE`/`DEBUG`/`TRACE` tier system. The build calls `emu68_debug_definitions()`
(renamed from `emu68_debug_backend_definitions()`) in `bcmpcie.library` and
`lspci`. No behavior change for consumers — the `EMU68_DEBUG_BACKEND` selection
(`pistorm` | `serial` | `off`) still works the same way.

### Fixed a garbled address in a trace log

`pci_bus_to_virt()`'s trace print shifted the 32-bit `pci_addr_t bus_addr` right
by 32 before widening it, always printing zero for the high half. It now widens
to `u64` before the shift. `TRACE`-only, no functional effect.

---

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
