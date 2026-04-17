// SPDX-License-Identifier: GPL-2.0-only
/*
 * pcie_io.c — I/O port access, config-space read/write, memory copy shims.
 *
 * Openpci-compatible shims over the U-Boot-derived pci_* helpers.  None of
 * these functions acquire the library semaphore; they operate on already-
 * enumerated devices via the pci_device back-pointer in pci_dev.reserved.
 */

#include <pcie_private.h>
#include <pci.h>
#include <iomem.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/* -----------------------------------------------------------------------
 * I/O port / MMIO accessors — forward to mmio_read/write so the platform
 * can apply the correct byte-order wrappers (LE on BCM2711).
 * NULL address returns a harmless sentinel / silently ignores writes.
 * ----------------------------------------------------------------------- */

UBYTE LibPCIInb(APTR addr asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    return addr ? (UBYTE)mmio_read8(addr) : 0xFF;
}

void LibPCIOutb(UBYTE val asm("d0"), APTR addr asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (addr) mmio_write8(val, addr);
}

UWORD LibPCIInw(APTR addr asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    return addr ? (UWORD)mmio_read16(addr) : 0xFFFF;
}

void LibPCIOutw(UWORD val asm("d0"), APTR addr asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (addr) mmio_write16(val, addr);
}

ULONG LibPCIInl(APTR addr asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    return addr ? (ULONG)mmio_read32(addr) : 0xFFFFFFFFUL;
}

void LibPCIOutl(ULONG val asm("d0"), APTR addr asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (addr) mmio_write32(val, addr);
}

/* -----------------------------------------------------------------------
 * PCI memory copy — DMA is coherent on Pi4/emu68; plain CopyMem suffices.
 * ----------------------------------------------------------------------- */

void LibPCIToHostCpy(APTR src asm("a0"), APTR dst asm("a1"), ULONG sz asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    KASSERT(((ULONG)src & 7) == 0 && ((ULONG)dst & 7) == 0,
            "pci_to_hostcpy: unaligned address");
    CopyMem(src, dst, sz);
}

void LibHostToPCICpy(APTR src asm("a0"), APTR dst asm("a1"), ULONG sz asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    KASSERT(((ULONG)src & 7) == 0 && ((ULONG)dst & 7) == 0,
            "host_to_pcicpy: unaligned address");
    CopyMem(src, dst, sz);
}

void LibPCIToPCICpy(APTR src asm("a0"), APTR dst asm("a1"), ULONG sz asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    KASSERT(((ULONG)src & 7) == 0 && ((ULONG)dst & 7) == 0,
            "pci_to_pcicpy: unaligned address");
    CopyMem(src, dst, sz);
}

/* -----------------------------------------------------------------------
 * Standard (UBYTE reg) config-space access — covers offsets 0x00–0xFF.
 * ----------------------------------------------------------------------- */

UBYTE LibReadConfigByte(UBYTE reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return 0xFF;
    ObtainSemaphore(&base->semaphore);
    u8 val = 0xFF;
    pci_read_config8(pcie_dev_from_openpci(dev), (u32)reg, &val);
    ReleaseSemaphore(&base->semaphore);
    return (UBYTE)val;
}

UWORD LibReadConfigWord(UBYTE reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return 0xFFFF;
    ObtainSemaphore(&base->semaphore);
    u16 val = 0xFFFF;
    pci_read_config16(pcie_dev_from_openpci(dev), (u32)reg, &val);
    ReleaseSemaphore(&base->semaphore);
    return (UWORD)val;
}

ULONG LibReadConfigLong(UBYTE reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return 0xFFFFFFFFUL;
    ObtainSemaphore(&base->semaphore);
    u32 val = 0xFFFFFFFFUL;
    pci_read_config32(pcie_dev_from_openpci(dev), (u32)reg, &val);
    ReleaseSemaphore(&base->semaphore);
    return (ULONG)val;
}

void LibWriteConfigByte(UBYTE reg asm("d0"), UBYTE val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return;
    ObtainSemaphore(&base->semaphore);
    pci_write_config8(pcie_dev_from_openpci(dev), (u32)reg, (u32)val);
    ReleaseSemaphore(&base->semaphore);
}

void LibWriteConfigWord(UBYTE reg asm("d0"), UWORD val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return;
    ObtainSemaphore(&base->semaphore);
    pci_write_config16(pcie_dev_from_openpci(dev), (u32)reg, (u32)val);
    ReleaseSemaphore(&base->semaphore);
}

void LibWriteConfigLong(UBYTE reg asm("d0"), ULONG val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return;
    ObtainSemaphore(&base->semaphore);
    pci_write_config32(pcie_dev_from_openpci(dev), (u32)reg, (u32)val);
    ReleaseSemaphore(&base->semaphore);
}

/* -----------------------------------------------------------------------
 * Extended config-space access — ULONG register, supports offsets >= 0x100.
 * ----------------------------------------------------------------------- */

UBYTE LibReadExtConfigByte(ULONG reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return 0xFF;
    ObtainSemaphore(&base->semaphore);
    u8 val = 0xFF;
    pci_read_config8(pcie_dev_from_openpci(dev), reg, &val);
    ReleaseSemaphore(&base->semaphore);
    return (UBYTE)val;
}

UWORD LibReadExtConfigWord(ULONG reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return 0xFFFF;
    ObtainSemaphore(&base->semaphore);
    u16 val = 0xFFFF;
    pci_read_config16(pcie_dev_from_openpci(dev), reg, &val);
    ReleaseSemaphore(&base->semaphore);
    return (UWORD)val;
}

ULONG LibReadExtConfigLong(ULONG reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return 0xFFFFFFFFUL;
    ObtainSemaphore(&base->semaphore);
    u32 val = 0xFFFFFFFFUL;
    pci_read_config32(pcie_dev_from_openpci(dev), reg, &val);
    ReleaseSemaphore(&base->semaphore);
    return (ULONG)val;
}

void LibWriteExtConfigByte(ULONG reg asm("d0"), UBYTE val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return;
    ObtainSemaphore(&base->semaphore);
    pci_write_config8(pcie_dev_from_openpci(dev), reg, (u32)val);
    ReleaseSemaphore(&base->semaphore);
}

void LibWriteExtConfigWord(ULONG reg asm("d0"), UWORD val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return;
    ObtainSemaphore(&base->semaphore);
    pci_write_config16(pcie_dev_from_openpci(dev), reg, (u32)val);
    ReleaseSemaphore(&base->semaphore);
}

void LibWriteExtConfigLong(ULONG reg asm("d0"), ULONG val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev) return;
    ObtainSemaphore(&base->semaphore);
    pci_write_config32(pcie_dev_from_openpci(dev), reg, (u32)val);
    ReleaseSemaphore(&base->semaphore);
}
