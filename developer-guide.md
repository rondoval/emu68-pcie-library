# bcmpcie.library — Driver Developer Guide

This guide covers using `bcmpcie.library` to write an AmigaOS driver that accesses a PCIe
device on Raspberry Pi 4 or Compute Module 4 (CM4).  It is aimed at driver authors rather
than end users.  The Raspberry Pi 4 has a VIA VL805 USB 3.0 controller hardwired to the
PCIe bus.  The CM4 exposes the PCIe interface externally; common use cases include NVMe
storage (as on PiStorm32-lite) and other PCIe endpoints.

For installation and build instructions see [README.md](README.md).
The bundled `lspci` command described there is also the quickest way to confirm
that enumeration, BAR assignment, and capability discovery are behaving as
expected before you start debugging a driver.

---

## API tier labels

Every function in this guide is tagged with one of three labels to indicate which library
must be open to call it and whether it is forwarded by the `openpci.library` shim:

| Label | Description | LVO range | `openpci.library` forwards it? |
|---|---|---|---|
| **[openpci]** | Classic openpci-compatible API | -30 to -192 | Yes |
| **[openpci v3]** | Tag-based openpci v3 additions | -204 to -252 | Yes |
| **[bcmpcie extension]** | BCM2711-specific, not in the shim | -258 and beyond | **No** |

New drivers should open `bcmpcie.library` directly to access the full API.
`openpci.library` is a compatibility shim for existing binaries that already open it by
name.  It is mainly intended for openpci-aware tooling and compatibility probes;
driver compatibility with historical third-party binaries should still be
validated case by case.

---

## 1. BCM2711 / RPi4 Hardware Context

### Architecture

The Raspberry Pi 4 and CM4 each expose a single PCIe root port driven by the Broadcom
STB PCIe controller embedded in the BCM2711 SoC.  On the Raspberry Pi 4 the bus topology
is fixed by the board:

```
Root Port (BCM2711 STB PCIe, bus 0)
  └─ Bus 0, Device 0 — PCIe-to-PCIe bridge (14e4:2711)
       └─ Bus 1, Device 0 — endpoint device
                            (VL805 USB 3.0, hardwired on RPi4)
```

On the CM4 the PCIe interface is exposed on the high-density connector; any endpoint or
downstream bridge may be attached there.

The library handles controller bringup, link negotiation, bus enumeration, BAR allocation
and VL805 firmware reload entirely inside `LibOpen()`.  By the time `OpenLibrary()` returns
the entire bus is configured and every `pci_dev` is ready to use.

### Advantages over classic Amiga PCI boards

- **MSI instead of shared IRQ lines.**  BCM2711 MSI delivers per-device vectored interrupts
  — no sharing, no polling, lower latency.
- **PCIe bandwidth.**  The BCM2711 controller negotiates up to PCIe Gen 2 ×1 (~500 MB/s),
  versus the 133 MB/s ceiling of a 32-bit 33 MHz PCI bus.
- ** DMA in both directions.**  The PCIe engine can read from and write to Fast
  RAM directly — no bounce buffers required.  A PCIe card writing to Fast RAM works as
  expected; the library handles address translation transparently.

### Limitations

- **Single root port.**  There is one PCIe port.  On the Raspberry Pi 4 the downstream
  topology is fixed by the board (bridge + VL805).  On the CM4 any endpoint may be
  attached.  Neither variant supports hotplug.

- **No IO space — hardware limitation.**  The BCM2711 PCIe controller has no IO aperture;
  IO BARs are not assigned.

- **Two layers of address translation.**  PCIe MMIO windows are placed at high ARM physical
  addresses by the PCIe bus mapping.  The ARM MMU then maps them into the 32-bit address
  space.

---

## 2. Library Lifecycle

### Opening

Open `bcmpcie.library` by name:

```c
#define __NOLIBBASE__
#include <proto/bcmpcie.h>
#include <libraries/openpci.h>

struct Library *pcielibBase = OpenLibrary("bcmpcie.library", 1);
if (pcielibBase == NULL)
    return ERROR_NO_LIBRARY;
```

Optionally verify the BCM2711 implementation with `pci_bus()` **[openpci]**:

```c
if (!(pci_bus() & BCM2711PCIeBus))   /* LVO -30; BCM2711PCIeBus = 0x80 */
{
    CloseLibrary(pcielibBase);
    return ERROR_WRONG_HARDWARE;
}
```

Multiple drivers can call `OpenLibrary()` concurrently — the library reference-counts
opens and shares a single initialised bus state.

### Closing

```c
CloseLibrary(pcielibBase);
pcielibBase = NULL;
```

Release any device ownership (see §6) and memory before closing.

---

## 3. Device Discovery

### [openpci] — Iterate by class or ID

```c
/* Find first xHCI controller (class 0x0C0330) */
struct pci_dev *pd = pci_find_class(0x0C0330, NULL);   /* LVO -96  */

/* Find next one (for multi-unit drivers) */
struct pci_dev *pd2 = pci_find_class(0x0C0330, pd);

/* Find by vendor/device ID */
struct pci_dev *vl805 = pci_find_device(0x1106, 0x3483, NULL);  /* LVO -90 */

/* Find by BDF */
struct pci_dev *byBDF = pci_find_slot(1, (0 << 3) | 0);  /* bus 1, dev 0, fn 0, LVO -102 */
```

All three functions return `NULL` when no (further) match is found.  Pass the previous
result as `prev` to continue iterating.  The device list is not an Exec list — do not use
`NEXTNODE()` or similar.

### [openpci v3] — Tag-based search with `FindBoardA()`

```c
#include <libraries/pcitags.h>
#include <utility/tagitem.h>

struct TagItem searchTags[] = {
    { PRM_Class,  0x0C0330 },
    { TAG_DONE,   0        }
};
struct pci_dev *pd = FindBoardA(NULL, searchTags);   /* LVO -204 */
```

`FindBoardA()` accepts these tags as search criteria: `PRM_Vendor`, `PRM_Device`,
`PRM_Class`, `PRM_SubClass`, `PRM_Interface`, `PRM_Revision`, `PRM_SubsysVendor`,
`PRM_SubsysID`, `PRM_BusNumber`, `PRM_SlotNumber`, and `PRM_FunctionNumber`.
Pass the previous result as `prev` to iterate.  The varargs wrapper is `FindBoard()`.

---

## 4. Device Attributes

### [openpci v3] — `GetBoardAttrsA()` / `SetBoardAttrsA()`

```c
ULONG vendorID, deviceID, bar0addr, bar0size, bar0flags;
struct TagItem getTags[] = {
    { PRM_Vendor,       (ULONG)&vendorID   },
    { PRM_Device,       (ULONG)&deviceID   },
    { PRM_MemoryAddr0,  (ULONG)&bar0addr   },
    { PRM_MemorySize0,  (ULONG)&bar0size   },
    { PRM_MemoryFlags0, (ULONG)&bar0flags  },
    { TAG_DONE, 0 }
};
GetBoardAttrsA(pd, getTags);   /* LVO -210 */
/* varargs: GetBoardAttrs(pd, PRM_Vendor, &vendorID, ..., TAG_DONE) */
```

The full set of tags:

| Tag | Access | Description |
|---|---|---|
| `PRM_Vendor` | R | PCI vendor ID |
| `PRM_Device` | R | PCI device ID |
| `PRM_Revision` | R | Revision byte |
| `PRM_Class` | R | Class code (base<<16, sub<<8, prog-if) |
| `PRM_SubClass` | R | Subclass byte |
| `PRM_Interface` | R | Programming interface byte |
| `PRM_HeaderType` | R | PCI header type |
| `PRM_SubsysVendor` | R | Subsystem vendor ID |
| `PRM_SubsysID` | R | Subsystem ID |
| `PRM_BusNumber` | R | PCI bus number |
| `PRM_SlotNumber` | R | Device (slot) number |
| `PRM_FunctionNumber` | R | Function number |
| `PRM_InterruptLine` | R | IRQ line from config space |
| `PRM_InterruptPin` | R | IRQ pin from config space |
| `PRM_MemoryAddr0..5` | R | 68K virtual MMIO base address for BAR 0–5 |
| `PRM_MemorySize0..5` | R | Decoded BAR size in bytes |
| `PRM_MemoryFlags0..5` | R | Low 4 bits of the raw BAR sizing response (type/prefetchable) |
| `PRM_ROM_Address` | R | 68K address of mapped ROM BAR, or 0 |
| `PRM_ROM_Size` | R | ROM BAR sizing response |
| `PRM_ConfigArea` | R | Base of config space window |
| `PRM_PCIMemWindowLow` | R | PCI bus address of low end of memory window |
| `PRM_PCIMemWindowHigh` | R | PCI bus address of high end of memory window |
| `PRM_LegacyIOSpace` | R | Always NULL (0) on BCM2711 — IO space is not mapped |
| `PRM_PCIToHostOffset` | R | Always 0 on BCM2711 — 68K and PCI bus addresses for Fast RAM are identical (1:1) |
| `PRM_BoardOwner` | **R/W** | Task pointer for exclusive ownership (see §6) |

`PRM_MemorySize*` differs from `struct pci_dev.base_size[]`: the tag API returns the
already-decoded BAR size, while `base_size[]` preserves the raw sizing response and
flag bits from enumeration.
---

## 5. `struct pci_dev` Fields

The `struct pci_dev` (from `<libraries/openpci.h>`) exposes the most commonly needed
fields directly.  All addresses are 68K virtual pointers:

```c
struct pci_dev {
    void            *bus;           /* unused */
    struct pci_dev  *next, *pred;   /* linked list — NOT an Exec list */
    UBYTE            devfn;         /* (device << 3) | function */
    UBYTE            kludgefill;
    UWORD            vendor, device;
    ULONG            devclass;      /* (base<<16, sub<<8, prog-if) */
    ULONG            hdr_type;
    UWORD            master;
    ULONG            irq;
    ULONG            base_address[6]; /* 68K virtual MMIO pointers, pre-mapped */
    ULONG            base_size[6];    /* raw BAR sizing response; flags in low 4 bits */
    ULONG            rom_address, rom_size;
    void            *reserved;
    UBYTE           *legacy_io;       /* NULL on BCM2711 (IO space unavailable) */
    struct Node     *owner;           /* set by PRM_BoardOwner */
};
```

`base_address[i]` is already a ready-to-use 68K virtual pointer.  Do not call any
mapping function on it again.

To decode `base_size[i]`:

```c
ULONG raw   = pd->base_size[0];
ULONG flags = raw & 0xF;            /* PCI BAR type bits */
ULONG size  = raw ? (~raw + 1) : 0; /* only valid for 32-bit non-prefetchable MEM */
/* For full decoding: mask off the type bits appropriate to the BAR type first */
```

---

## 6. Exclusive Device Ownership

`PRM_BoardOwner` **[openpci v3]** is an advisory per-device ownership marker — a Task
pointer stored in the `pci_dev`.  The library enforces exclusivity only on
`SetBoardAttrsA(PRM_BoardOwner, …)` itself: a non-NULL `owner` cannot be overwritten by
a second caller.  No other library call checks the owner field; callers must cooperate.

### Claiming

```c
if (!SetBoardAttrs(pd, PRM_BoardOwner, (ULONG)FindTask(NULL), TAG_DONE))
{
    /* Device already owned by another task */
    return IOERR_UNITBUSY;
}
```

`SetBoardAttrsA()` returns FALSE and leaves `owner` unchanged if it is already non-NULL.

### Releasing

```c
SetBoardAttrs(pd, PRM_BoardOwner, 0UL, TAG_DONE);
```

Setting `0UL` always clears the owner unconditionally.  Release in every error path and
in the unit close path before calling `CloseLibrary()`.

---

## 7. BAR Access

### Direct access via `base_address[]`

The library maps all BARs during initialisation.  `pd->base_address[i]` is a valid 68K
virtual pointer to the start of BAR `i` on entry to your open function:

```c
volatile struct device_regs *regs =
    (volatile struct device_regs *)pd->base_address[0];
```

Never call `pci_bus_to_virt()` or any equivalent — the mapping is already done.

### [openpci v3] — `ObtainPCIRegion()` / `ReleasePCIRegion()`

In the original openpci API these functions map an arbitrary PCI bus address range into
the CPU address space.  On BCM2711 there is no runtime remapping — MMIO windows are
established once during enumeration.  `ObtainPCIRegion()` resolves a PCI bus address by
searching the device's MEM BAR windows for one that contains it and returns the
corresponding 68K virtual pointer.  If the bus address falls outside all mapped MEM BARs,
it returns NULL.

The primary use case is when you have a PCI bus address from a device descriptor and need
the CPU-side pointer:

```c
/* pci_bus_address is a PCI bus address pointing into one of the device's BARs */
APTR ptr = ObtainPCIRegion(pd, pci_bus_address, size);  /* LVO -246 */
if (!ptr)
    return ERROR_BAD_ADDRESS;
/* … use ptr … */
ReleasePCIRegion(pd, ptr);   /* LVO -252; no-op, call for future compat */
```

`ObtainPCIRegion()` only works for **memory-mapped BARs**.  IO BARs are skipped.
`ReleasePCIRegion()` is a no-op: BAR MMIO windows are permanent for the life of the library.

---

## 8. Config Space

### [openpci] — Standard (offsets 0x00–0xFF)

```c
UBYTE rev      = pci_read_config_byte(PCI_REVISION_ID, pd);     /* LVO -108 */
UWORD cmd      = pci_read_config_word(PCI_COMMAND, pd);         /* LVO -114 */
ULONG classcode = pci_read_config_long(PCI_CLASS_REVISION, pd); /* LVO -120 */

pci_write_config_word(PCI_COMMAND, cmd | PCI_COMMAND_MASTER, pd); /* LVO -132 */
```

The `reg` argument is a `UBYTE`, so the maximum addressable offset is 0xFF.

### [bcmpcie extension] — Extended config space (offsets 0x100+)

PCIe extended capabilities live above offset 0x100.  Use the extended functions which
take a `ULONG` register argument:

```c
ULONG extcap = ReadExtConfigLong(0x100, pd);   /* LVO -300 */
WriteExtConfigByte(0x10C, 0x01, pd);           /* LVO -306 */
```

All six variants follow the same register ordering as the standard functions.

---

## 9. Interrupts

### Bus mastering

`pci_set_master()` **[openpci]** enables the `PCI_COMMAND_MASTER` bit.  It is required
when using MSI/MSI-X (message delivery depends on the device being a bus master) and for
DMA.  Call it before allocating interrupt vectors:

```c
pci_set_master(pd);   /* LVO -144 */
```

### [bcmpcie extension] — Interrupt vectors (preferred API)

The driver chooses the interrupt type and how many vectors it wants.  `AllocIntVectors()`
reserves vectors of the **best allowed type in priority order MSI-X → MSI → INTx**, where
`flags` is any combination of `PCI_IRQ_MSIX`, `PCI_IRQ_MSI`, `PCI_IRQ_INTX` (or
`PCI_IRQ_ALL_TYPES`) from `<libraries/pci_constants.h>`.  A type is attempted only when its
bit is set, so omitting a bit disables it — e.g. drop `PCI_IRQ_MSIX` to forbid MSI-X, or
pass `PCI_IRQ_INTX` alone to force the legacy line.  It returns the number of vectors
actually reserved (≥ `min`), or a negative value on failure.

```c
struct Interrupt isr0;
isr0.is_Node.ln_Type = NT_INTERRUPT;
isr0.is_Node.ln_Name = "mydriver_isr";
isr0.is_Data         = unit;
isr0.is_Code         = (APTR)my_isr;

LONG n = AllocIntVectors(pd, 1, 1, PCI_IRQ_ALL_TYPES);   /* LVO -342 */
if (n < 1)
    return ERROR_INTERRUPT;

ULONG itype = GetIntVectorType(pd);   /* LVO -378: PCI_IRQ_MSIX/_MSI/_INTX */

/* Install a server per vector; this also unmasks that vector. */
AddIntVectorServer(pd, 0, &isr0);     /* LVO -354 (vector index, isr) */
```

For multiple MSI-X vectors (e.g. one per queue), request a range and install a server for
each.  `AllocIntVectors` returns how many it actually got:

```c
LONG nvec = AllocIntVectors(pd, 1, NUM_QUEUES, PCI_IRQ_MSIX);
for (ULONG v = 0; v < (ULONG)nvec; v++)
    AddIntVectorServer(pd, v, &unit->qisr[v]);
```

Tear down in reverse: remove each server, then free the allocation:

```c
RemIntVectorServer(pd, 0, &isr0);     /* LVO -360 */
FreeIntVectors(pd);                   /* LVO -348 */
```

### ISR-safe masking — prefer the device's own registers

For a well-behaved driver the recommended pattern, in the ISR, is to (a) probe one of
*your device's* status registers to answer "is this interrupt mine?" — returning 0 so the
next server runs when it isn't, which is what makes a shared INTx line work — and (b) quiet
the interrupt at *your device's* mask/ack register.  That device-level mask deasserts the
INTx pin (and gates MSI/MSI-X) on its own, so no PCIe-config masking is required.  This is
how `nvme.device`/`xhci.device` work.

```c
/* Inside ISR — is it ours?  then mask at the device and signal the task */
if (!device_irq_is_ours(unit)) return 0;   /* let the next shared-line server run */
device_irq_mask(unit);                      /* device register: deasserts the line */
Signal(unit->task, 1UL << unit->irq_signal);
return 1;
/* Inside task — rearm at the device after processing */
device_irq_unmask(unit);
```

### [bcmpcie extension] — ISR-safe PCIe-level masking (fallback)

`MaskIntVector()` / `UnmaskIntVector()` do **not** acquire the library semaphore and are
safe to call from interrupt context.  They dispatch on the active type automatically
(per-vector mask for MSI/MSI-X, `PCI_COMMAND.INTX_DISABLE` pin mask for INTx).  Use them
only when you cannot quiet the device at its own registers (e.g. a generic/pass-through
handler).  They return whether the (un)mask took effect.  MSI-X per-vector masking is
mandatory, so it always returns TRUE.  MSI per-vector masking is **optional**: both calls
return FALSE when the device lacks the Per-Vector Masking Capability — there is no mask bit,
so the vector cannot be quieted at the device and you must mask it at the device's own
registers instead.  For INTx, `UnmaskIntVector()` returns FALSE when the line never
de-asserted (an interrupt is still pending) — the server will not fire again, so you must
re-signal the task yourself to drain it:

```c
MaskIntVector(pd, 0);                 /* LVO -366 */
/* ... */
if (!UnmaskIntVector(pd, 0))          /* LVO -372 — INTx pending-guard */
    Signal(unit->task, 1UL << unit->irq_signal);
```

### [obsolete] — `EnableMSI` / `pci_add_intserver`

The older calls remain for backward compatibility but are **single-vector and select MSI
or INTx only — never MSI-X**.  New code should use `AllocIntVectors()`.  Equivalences:
`EnableMSI()`+`pci_add_intserver()` → `AllocIntVectors(pd,1,1,PCI_IRQ_MSI|PCI_IRQ_INTX)`+
`AddIntVectorServer(pd,0,isr)`; `MaskMSI`/`UnmaskMSI` → `MaskIntVector`/`UnmaskIntVector`;
`pci_rem_intserver`+`DisableMSI` → `RemIntVectorServer`+`FreeIntVectors`.

---

## 10. DMA Memory

Fast RAM in the Emu68 environment has a 1:1 mapping between 68K addresses, ARM physical
addresses, and PCIe bus addresses — no bounce buffers or address translation required.
Any MEMF_FAST memory that is physically PI4/CM4 RAM is directly addressable by the PCIe
DMA engine at the same numeric address the CPU uses.

### [openpci] — Legacy allocation

```c
APTR dmaBuf = pci_allocdma_mem(bufSize, MEM_NONCACHEABLE);  /* LVO -162 */
if (!dmaBuf)
    return ERROR_NO_MEMORY;
/* … */
pci_freedma_mem(dmaBuf, bufSize);   /* LVO -168 */
```

Both allocation functions return cache-line-aligned Fast RAM (`DMA_ALIGN_MIN`).  DMA
transfers require the buffer to be aligned to this boundary; this is the only difference
from `AllocMem(MEMF_FAST)`.  The flags argument is currently ignored — all allocations
come from the shared DMA pool.

> **Note:** Only Fast RAM provided by Emu68 is DMA-capable.  Chip RAM and any Fast RAM
> not mapped by Emu68 is not accessible by the PCIe DMA engine and must not be used as
> a DMA buffer.

> **Note:** PCIe on PI4/CM4 is not cache coherent. Use CachePreDMA()/CachePostDMA()

### [openpci v3] — Per-board allocation

```c
APTR dmaBuf = AllocateDMAMemoryForBoard(pd, bufSize, MEM_NONCACHEABLE);  /* LVO -222 */
ReleaseDMAMemoryForBoard(pd, dmaBuf, bufSize);                           /* LVO -228 */
```

### Flags

| Flag | Value | Meaning |
|---|---|---|
| `MEM_PCI` | 0x1 | On the PCI bus (always implied) |
| `MEM_NONCACHEABLE` | 0x2 | Memory must not be cached by the 68K side |
| `MEM_24BITDMA` | 0x4 | Hint: 24-bit DMA address required; no-op on BCM2711 |

Use `MEM_NONCACHEABLE` for any buffer the device writes into and the CPU reads out of,
unless your driver performs explicit cache invalidation.

---

## 11. Address Translation

The BCM2711 has two distinct address spaces that require translation:

- **BAR windows** — PCIe MMIO windows are assigned high PCI bus addresses.  The ARM MMU
  maps them down into the 32-bit 68K address space.  The library stores both the 68K
  virtual address (`virt_addr`) and the PCI bus address (`bus_addr`) for each BAR;
  offset arithmetic translates between them.
- **DMA / Fast RAM** — 68K address == ARM physical address == PCIe bus address (1:1).
  The DMA region table records the base and size of each Fast RAM region.

### [openpci] — `pci_logic_to_physic_addr()` / `pci_physic_to_logic_addr()`

```c
/* 68K logical (emu68 virtual) → PCI bus address */
APTR pciBusAddr = pci_logic_to_physic_addr(logicalAddr, pd);  /* LVO -174 */

/* PCI bus address → 68K logical */
APTR logicalAddr = pci_physic_to_logic_addr(pciBusAddr, pd);  /* LVO -180 */
```

Both functions:
1. Check whether the address falls within any BAR MMIO window and apply the window's
   offset to translate between 68K virtual and PCI bus address.
2. Check whether the address falls within a mapped Fast RAM region (1:1 address space)
   and apply the DMA region offset.
3. Return **NULL** if `dev` is NULL or the address does not fall within any known range.

---

## 12. PCIe Extensions [bcmpcie extension]

These functions are not available through the `openpci.library` shim.

### Capability discovery

```c
ULONG capOffset = FindCapability(pd, PCI_CAP_ID_MSI);   /* LVO -258 */
if (!capOffset)
    return ERROR_NO_MSI;

/* Extended capabilities (offset >= 0x100) */
ULONG extOffset = FindExtCapability(pd, PCI_EXT_CAP_ID_AER);  /* LVO -264 */
```

Returns the byte offset within config space, or 0 if not found.

### Function Level Reset

```c
LONG result = FLR(pd);   /* LVO -282; 0 = success */
```

`FLR()` only succeeds if the device advertises FLR support in its capabilities.  It
waits for the device to recover before returning.

---

## 13. Worked Example: USB xHCI Driver

This section shows the complete PCIe lifecycle as implemented in `emu68-xhci-driver`.
Source files: `xhci.device/src/device.c` (library open), `xhci.device/src/unit.c`
(device discovery and BAR access), `xhci.device/src/irq.c` (interrupt handling).

### Step 1 — Open the library (device.c)

```c
/* Try bcmpcie.library first, then openpci.library for compatibility */
static const char * const libNames[] = { "bcmpcie.library", "openpci.library" };
static const ULONG libVersions[] = { 1, 0 };

for (int i = 0; i < 2; i++)
{
    struct Library *pcielibBase = OpenLibrary(libNames[i], libVersions[i]);
    if (!pcielibBase)
        continue;

    UWORD flags = pci_bus();
    if (flags & BCM2711PCIeBus)
    {
        base->pcieBase = pcielibBase;
        break;
    }

    CloseLibrary(pcielibBase);
}
if (!base->pcieBase)
    return -1;
```

### Step 2 — Discover the device (unit.c)

```c
/* Find the Nth xHCI controller; unit 0 is the onboard one (devicetree path),
 * units 1+ are PCIe VL805 controllers. */
struct pci_dev *pd = NULL;
for (LONG i = 0; i < unitNumber; i++)
{
    pd = pci_find_class(0x0C0330, pd);  /* [openpci] LVO -96 */
    if (!pd) break;
}
if (!pd)
    return ERR_BAD_PARAMETERS;
```

### Step 3 — Validate with config space reads (unit.c)

```c
UBYTE revision  = pci_read_config_byte(PCI_REVISION_ID, pd);  /* [openpci] LVO -108 */
UBYTE prog_if   = pci_read_config_byte(PCI_CLASS_PROG, pd);
UBYTE subclass  = pci_read_config_byte(PCI_CLASS_DEVICE, pd);
UBYTE baseclass = pci_read_config_byte(PCI_CLASS_DEVICE + 1, pd);
ULONG mcuFw     = pci_read_config_long(0x50, pd);
```

### Step 4 — Claim exclusive ownership (unit.c)

```c
if (!SetBoardAttrs(pd, PRM_BoardOwner, (ULONG)FindTask(NULL), TAG_DONE))
{                                      /* [openpci v3] LVO -216 */
    Kprintf("[xhci] device already owned\n");
    return ERR_BAD_PARAMETERS;
}
```

On failure, roll back immediately:

```c
/* If a later step fails: */
SetBoardAttrs(pd, PRM_BoardOwner, 0UL, TAG_DONE);
```

### Step 5 — Access BAR0 registers (unit.c)

```c
/* base_address[0] is already a 68K virtual pointer — no mapping needed */
struct xhci_hccr *hccr = (struct xhci_hccr *)pd->base_address[0];
if (!hccr)
    return -EIO;

struct xhci_hcor *hcor = (struct xhci_hcor *)
    ((uintptr_t)hccr + HC_LENGTH(mmio_read32(&hccr->cr_capbase)));
```

### Step 6 — Enable bus mastering (unit.c)

```c
pci_set_master(pd);   /* [openpci] LVO -144 */
```

### Step 7 — Enable interrupts with MSI fallback to INTx (irq.c)

```c
BOOL msiEnabled = FALSE;
if (DEVICE_USE_MSI && EnableMSI(pd) == 0)   /* [bcmpcie extension] LVO -270 */
    msiEnabled = TRUE;

if (!pci_add_intserver(&unit->irq_isr, pd)) /* [openpci] LVO -150 */
    return -1;
```

### Step 8 — ISR masking pattern (irq.c)

Mask at *your device's* own registers, not at the PCIe level.  A device-level mask
deasserts the INTx pin (and gates MSI/MSI-X) for both interrupt types, so a single write
quiets the source until the UnitTask rearms — no `MaskMSI`/`CheckSetINTxMask` needed.  See
§9 ("ISR-safe masking") for why this also handles shared INTx lines correctly.

```c
/* Inside ISR: confirm it's ours (also the shared-INTx check), mask at the
 * device, then signal UnitTask */
if (!device_irq_is_ours(unit))
    return 0;                           /* let the next shared-line server run */
device_irq_mask(unit);                  /* your device's mask register */
Signal(unit->task, 1UL << unit->irq_signal);
return 1;

/* Inside UnitTask after processing: rearm at the device */
device_irq_unmask(unit);                /* re-raises if events arrived while masked */
```

### Step 9 — Teardown (unit.c)

```c
/* In UnitClose(), last-opener path: */
pci_rem_intserver(&unit->irq_isr, pd);  /* [openpci] LVO -156 */
if (msiEnabled)
    DisableMSI(pd);                     /* [bcmpcie extension] LVO -276 */

SetBoardAttrs(pd, PRM_BoardOwner, 0UL, TAG_DONE);  /* [openpci v3] LVO -216 */
/* CloseLibrary() happens at device expunge */
```

---

## 14. openpci.library Compatibility Shim

`openpci.library` is a thin wrapper that opens `bcmpcie.library` on first use and
forwards every call via naked Motorola 68K trampolines.  It exposes:

- All **[openpci]** functions (LVO -30 to -192).
- All **[openpci v3]** functions (LVO -204 to -252).

It does **not** expose any **[bcmpcie extension]** function (LVO -258 and above).
In particular, the following are **unavailable** through the shim:

- `FindCapability()`, `FindExtCapability()`, `FLR()`
- `EnableMSI()`, `DisableMSI()`
- `MaskMSI()`, `UnmaskMSI()`, `CheckSetINTxMask()`
- Extended config space (`ReadExtConfigByte/Word/Long()`, `WriteExtConfigByte/Word/Long()`)

Drivers that need any of these must open `bcmpcie.library` directly.

The shim exists for backward compatibility with applications that call
`OpenLibrary("openpci.library", …)` and expect the classic openpci interface.
That makes it useful for utilities such as `identify.library` and similar
openpci-oriented tools, but it should still be treated as a targeted compatibility
layer rather than a blanket guarantee for every legacy PCI driver.

---

## 15. Limitations and Roadmap

### Current limitations

- **Fixed BCM2711 / Raspberry Pi 4 target.**  The controller bringup code is specific to
  the Broadcom STB PCIe IP.  Port to other SoCs would require a new controller backend.
- **Fixed bus topology on Raspberry Pi 4.**  One root port, one PCIe-to-PCIe bridge,
  one endpoint slot.  There is no multi-root or hotplug support.  On the CM4 a PCIe
  switch or downstream bridge board can be attached to the exposed connector, providing
  additional slots; each still enumerates under the same single root port.
- **No IO space — hardware limitation.**  The BCM2711 PCIe controller has no IO aperture;
  IO BARs are left unassigned.

---

## CMake Integration

Add the following to your driver's `CMakeLists.txt`:

```cmake
find_package(Emu68PCIe REQUIRED)
target_link_libraries(my_driver PRIVATE Emu68PCIe::pcie_headers)
```

The `pcie_headers` target provides the public header search paths
(`<libraries/openpci.h>`, `<libraries/pcitags.h>`, `<libraries/pci_constants.h>`,
`<libraries/pci_ids.h>`, generated proto/clib/inline headers).

The library itself is opened at runtime with `OpenLibrary()` — there is no import
library to link against.
