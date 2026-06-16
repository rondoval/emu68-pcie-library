// SPDX-License-Identifier: GPL-2.0-only
/*
 * pcie_mem.c — DMA allocation, address translation, BAR mapping, and
 *              PCI memory-region reservation shims.
 *
 * Fast RAM is 1:1 virtual-to-physical on Pi4/emu68, so address translation for
 * DMA buffers is identity.  But only *Emu68* Fast RAM (Pi DRAM) is reachable by
 * the PCIe DMA engine; Chip RAM and any Zorro/accelerator Fast RAM are not.  DMA
 * buffers therefore come from a region-restricted pool (see dma_mem.h) rather
 * than ordinary AllocMem(MEMF_FAST).
 */

#include <pcie_private.h>
#include <memory.h>
#include <pci_bar.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/* -----------------------------------------------------------------------
 * DMA memory allocation — region-restricted, cache-line-aligned pool.
 *
 * base->dmaPool is backed by Emu68 (Pi-DRAM) RAM only, so every allocation is
 * reachable by the PCIe DMA engine regardless of which device will use it.
 * The caller is responsible for freeing buffers via ReleaseDMAMemoryForBoard.
 * ----------------------------------------------------------------------- */

APTR LibAllocDMAMem(ULONG size asm("d0"), ULONG flags asm("d1"), struct PCIELibBase *base asm("a6"))
{
    (void)flags;
    return dma_zalloc(base->dmaPool, DMA_ALIGN_MIN, size);
}

void LibFreeDMAMem(APTR buf asm("a0"), ULONG size asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)size;
    dma_free(base->dmaPool, buf);
}

APTR LibAllocDMAForBoard(struct pci_dev *dev asm("a0"), ULONG size asm("d0"), ULONG flags asm("d1"), struct PCIELibBase *base asm("a6"))
{
    (void)dev;
    (void)flags;
    return dma_zalloc(base->dmaPool, DMA_ALIGN_MIN, size);
}

void LibReleaseDMAForBoard(struct pci_dev *dev asm("a0"), APTR mem asm("a1"), ULONG size asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)dev;
    (void)size;
    dma_free(base->dmaPool, mem);
}

/* -----------------------------------------------------------------------
 * Address translation.
 *
 * Two address spaces need bridging:
 *
 *  1. DMA RAM (Fast RAM allocated by pci_allocdma_mem / AllocDMAMemoryForBoard):
 *     emu68 virtual == ARM physical (1:1 map for Fast RAM), so the logical
 *     address is passed directly to pci_phys_to_bus / pci_bus_to_phys which
 *     translate through the inbound DMA window table.
 *
 *  2. BAR windows (device MMIO mapped by the ARM MMU above 4 GB):
 *     emu68 virtual addresses in a BAR window are NOT 1:1 to physical.
 *     The BAR cache records both virt_addr (emu68 logical) and bus_addr
 *     (PCI bus address), so for any address that falls inside a BAR window
 *     we compute the PCI address directly as:
 *         bus_addr + (logical - virt_addr)
 *     and the reverse for PhysicToLogic.
 *
 * When no device handle is supplied the translation cannot be performed and
 * NULL is returned.
 * ----------------------------------------------------------------------- */

APTR LibLogicToPhysic(APTR addr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!addr || !dev)
        return NULL;
    struct pci_device *idev = pcie_dev_from_openpci(dev);
    /* Check BAR windows first: virt_addr is the emu68 logical address */
    for (u32 i = 0; i < idev->bars_num; i++)
    {
        const struct pci_bar_info *bar = &idev->bars[i];
        if (!bar->present || !bar->virt_addr)
            continue;
        ULONG virt = (ULONG)bar->virt_addr;
        ULONG size = (ULONG)bar->size;
        if ((ULONG)addr >= virt && (ULONG)addr < virt + size)
            return (APTR)(ULONG)(bar->bus_addr + ((ULONG)addr - virt));
    }
    /* Fall back to DMA RAM path (Fast RAM: logical == ARM physical) */
    return (APTR)(ULONG)pci_phys_to_bus(idev, (phys_addr_t)(ULONG)addr,
                                        1, 0, 0);
}

APTR LibPhysicToLogic(APTR addr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!addr || !dev)
        return NULL;
    struct pci_device *idev = pcie_dev_from_openpci(dev);
    /* Check BAR windows first: bus_addr is the PCI bus address */
    pci_addr_t pciaddr = (pci_addr_t)(ULONG)addr;
    for (u32 i = 0; i < idev->bars_num; i++)
    {
        const struct pci_bar_info *bar = &idev->bars[i];
        if (!bar->present || !bar->virt_addr)
            continue;
        if (pciaddr >= bar->bus_addr && pciaddr < bar->bus_addr + bar->size)
            return (APTR)((ULONG)bar->virt_addr + (ULONG)(pciaddr - bar->bus_addr));
    }
    /* Fall back to DMA RAM path (logical == ARM physical for Fast RAM) */
    return (APTR)(ULONG)pci_bus_to_phys(idev, pciaddr, 1, 0, 0);
}

/* -----------------------------------------------------------------------
 * Memory-change notification hooks — no-op stubs.
 * The bounce-notification protocol is not implemented: clients that need
 * DMA-safe memory should obtain it from AllocDMAMemoryForBoard/AllocDMAMem,
 * which return Emu68 (PCIe-reachable) RAM.  Return TRUE (success) so callers
 * (e.g. P96) do not treat the registration as failed.
 * ----------------------------------------------------------------------- */

BOOL LibAddMemHandler(struct pci_dev *dev asm("a0"), struct Hook *hk asm("a1"), BYTE pri asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)dev;
    (void)hk;
    (void)pri;
    (void)base;
    return TRUE; /* no-op: clients should use AllocDMAMem for reachable memory */
}

BOOL LibRemMemHandler(struct pci_dev *dev asm("a0"), struct Hook *hk asm("a1"), struct PCIELibBase *base asm("a6"))
{
    (void)dev;
    (void)hk;
    (void)base;
    return TRUE; /* no-op */
}

/* -----------------------------------------------------------------------
 * PCI region reservation.
 *
 * ObtainPCIRegion() gives the CPU access to an arbitrary PCI bus address.
 * On BCM2711, the CPU can only reach PCIe bus addresses through the
 * outbound windows established for each BAR during enumeration (both
 * memory and I/O BARs); there is no mechanism to remap the window at runtime.  We therefore
 * walk the requesting device's MEM BAR cache to find which window covers
 * [region, region+size) and return the corresponding virtual address.  I/O
 * BARs are excluded per the spec: "This function can only gain access to
 * memory mapped regions, not to IO mapped regions."
 *
 * ReleasePCIRegion() is a no-op: BAR MMIO windows are permanent for
 * the life of the library.
 * ----------------------------------------------------------------------- */

APTR LibObtainPCIRegion(struct pci_dev *dev asm("a0"), ULONG region asm("a1"), ULONG size asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev || !region)
        return NULL;

    struct pci_device *idev = pcie_dev_from_openpci(dev);
    for (u32 i = 0; i < idev->bars_num; i++)
    {
        const struct pci_bar_info *bar = &idev->bars[i];
        if (!bar->present || !bar->virt_addr || bar->type == PCI_REGION_IO)
            continue;
        /* Check [region, region+size) ⊆ [bus_addr, bus_addr+size) */
        if ((pci_addr_t)region >= bar->bus_addr &&
            (pci_addr_t)region + size <= bar->bus_addr + bar->size)
        {
            return (APTR)((ULONG)bar->virt_addr + (region - (ULONG)bar->bus_addr));
        }
    }
    return NULL; /* no mapped BAR window covers the requested bus address */
}

void LibReleasePCIRegion(struct pci_dev *dev asm("a0"), APTR addr asm("a1"), struct PCIELibBase *base asm("a6"))
{
    (void)dev;
    (void)addr;
    (void)base;
    /* no-op: BAR MMIO windows are permanent on BCM2711 */
}
