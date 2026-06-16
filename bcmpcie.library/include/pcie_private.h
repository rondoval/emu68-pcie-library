// SPDX-License-Identifier: GPL-2.0-only

#ifndef _PCIE_PRIVATE_H
#define _PCIE_PRIVATE_H

#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/interrupts.h>
#include <exec/memory.h>

#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#include <proto/exec.h>
#endif

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/tasks.h>
#include <types.h>
#include <debug.h>
#include <dma_mem.h>
#include <libraries/openpci.h>
#include <libraries/pcitags.h>
#include <pci_types.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/* These are overridden by cmake */
#ifndef LIBRARY_NAME
#define LIBRARY_NAME "bcmpcie.library"
#endif

#ifndef LIBRARY_IDSTRING
#define LIBRARY_IDSTRING "bcmpcie.library 1.0 (13.04.2026)"
#endif

#ifndef LIBRARY_VERSION
#define LIBRARY_VERSION 1
#endif

#ifndef LIBRARY_REVISION
#define LIBRARY_REVISION 0
#endif

#ifndef LIBRARY_PRIORITY
#define LIBRARY_PRIORITY 0
#endif

/*
 * The pcie.library base structure.
 *
 * gic400Base and the PCIe controller are initialised on first LibOpen
 * and torn down when lib_OpenCnt drops back to zero.
 */
struct PCIELibBase {
    struct Library          libNode;
    ULONG                   segList;

    struct SignalSemaphore  semaphore;      /* protects all mutable state */

    struct Library         *gic400Base;    /* opened on first LibOpen, closed when OpenCnt reaches 0 */
    struct pci_controller  *ctrl;          /* the one BCM2711 controller */
    struct pci_bus         *rootBus;       /* root bus after probe */

    /*
     * Linked list of struct pci_dev nodes (public openpci type).
     * Built on first open; head->next chains all enumerated devices.
     * pci_dev.reserved is re-used as a back-pointer to struct pci_device.
     */
    struct pci_dev         *devListHead;   /* first pci_dev, or NULL */

    BOOL                    ctrlReady;     /* TRUE after successful first-open init */
    struct dma_mem_ctx      dma_ctx;       /* Emu68 (DMA-reachable) RAM regions; backs dmaPool */
    struct dma_pool        *dmaPool;       /* region-restricted DMA pool (Emu68 RAM) for DMA buffers */
};

/* Extract the internal pci_device pointer stored in pci_dev.reserved. */
static inline struct pci_device *pcie_dev_from_openpci(const struct pci_dev *dev)
{
    return (struct pci_device *)dev->reserved;
}

/* Forward declaration for Hook pointer parameters (utility/hooks.h not required). */
struct Hook;

/* -----------------------------------------------------------------------
 * pcie_irq.c — interrupt registration, MSI setup/teardown, ISR-safe ctrl
 * ----------------------------------------------------------------------- */
BOOL LibAddIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"));
void LibRemIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"));
LONG LibEnableMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void LibDisableMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void LibMaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void LibUnmaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
BOOL LibCheckSetINTxMask(struct pci_dev *dev asm("a0"), BOOL mask asm("d0"), struct PCIELibBase *base asm("a6"));

/* -----------------------------------------------------------------------
 * pcie_io.c — I/O port, config-space read/write, memory copy
 * ----------------------------------------------------------------------- */
UBYTE LibPCIInb(APTR addr asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibPCIOutb(UBYTE val asm("d0"), APTR addr asm("a0"), struct PCIELibBase *base asm("a6"));
UWORD LibPCIInw(APTR addr asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibPCIOutw(UWORD val asm("d0"), APTR addr asm("a0"), struct PCIELibBase *base asm("a6"));
ULONG LibPCIInl(APTR addr asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibPCIOutl(ULONG val asm("d0"), APTR addr asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibPCIToHostCpy(APTR src asm("a0"), APTR dst asm("a1"), ULONG sz asm("d0"), struct PCIELibBase *base asm("a6"));
void  LibHostToPCICpy(APTR src asm("a0"), APTR dst asm("a1"), ULONG sz asm("d0"), struct PCIELibBase *base asm("a6"));
void  LibPCIToPCICpy(APTR src asm("a0"), APTR dst asm("a1"), ULONG sz asm("d0"), struct PCIELibBase *base asm("a6"));
UBYTE LibReadConfigByte(UBYTE reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
UWORD LibReadConfigWord(UBYTE reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
ULONG LibReadConfigLong(UBYTE reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibWriteConfigByte(UBYTE reg asm("d0"), UBYTE val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibWriteConfigWord(UBYTE reg asm("d0"), UWORD val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibWriteConfigLong(UBYTE reg asm("d0"), ULONG val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
UBYTE LibReadExtConfigByte(ULONG reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
UWORD LibReadExtConfigWord(ULONG reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
ULONG LibReadExtConfigLong(ULONG reg asm("d0"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibWriteExtConfigByte(ULONG reg asm("d0"), UBYTE val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibWriteExtConfigWord(ULONG reg asm("d0"), UWORD val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibWriteExtConfigLong(ULONG reg asm("d0"), ULONG val asm("d1"), struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));

/* -----------------------------------------------------------------------
 * pcie_mem.c — DMA allocation, address translation, BAR mapping, regions
 * ----------------------------------------------------------------------- */
APTR LibAllocDMAMem(ULONG size asm("d0"), ULONG flags asm("d1"), struct PCIELibBase *base asm("a6"));
void LibFreeDMAMem(APTR buf asm("a0"), ULONG size asm("d0"), struct PCIELibBase *base asm("a6"));
APTR LibAllocDMAForBoard(struct pci_dev *dev asm("a0"), ULONG size asm("d0"), ULONG flags asm("d1"), struct PCIELibBase *base asm("a6"));
void LibReleaseDMAForBoard(struct pci_dev *dev asm("a0"), APTR mem asm("a1"), ULONG size asm("d0"), struct PCIELibBase *base asm("a6"));
APTR LibLogicToPhysic(APTR addr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"));
APTR LibPhysicToLogic(APTR addr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"));
BOOL LibAddMemHandler(struct pci_dev *dev asm("a0"), struct Hook *hk asm("a1"), BYTE pri asm("d0"), struct PCIELibBase *base asm("a6"));
BOOL LibRemMemHandler(struct pci_dev *dev asm("a0"), struct Hook *hk asm("a1"), struct PCIELibBase *base asm("a6"));
APTR LibObtainPCIRegion(struct pci_dev *dev asm("a0"), ULONG region asm("a1"), ULONG size asm("d0"), struct PCIELibBase *base asm("a6"));
void LibReleasePCIRegion(struct pci_dev *dev asm("a0"), APTR addr asm("a1"), struct PCIELibBase *base asm("a6"));

/* -----------------------------------------------------------------------
 * pcie_find.c — linked-list traversal and tag-based device search
 * ----------------------------------------------------------------------- */
struct pci_dev *LibFindDevice(UWORD vendor asm("d0"), UWORD device asm("d1"), struct pci_dev *prev asm("a0"), struct PCIELibBase *base asm("a6"));
struct pci_dev *LibFindClass(ULONG devclass asm("d0"), struct pci_dev *prev asm("a0"), struct PCIELibBase *base asm("a6"));
struct pci_dev *LibFindSlot(UBYTE bus asm("d0"), ULONG devfn asm("d1"), struct PCIELibBase *base asm("a6"));
struct pci_dev *LibFindBoardA(struct pci_dev *prev asm("a0"), struct TagItem *tags asm("a1"), struct PCIELibBase *base asm("a6"));
ULONG LibFindCapability(struct pci_dev *dev asm("a0"), UBYTE cap asm("d0"), struct PCIELibBase *base asm("a6"));
ULONG LibFindExtCapability(struct pci_dev *dev asm("a0"), UWORD cap asm("d0"), struct PCIELibBase *base asm("a6"));

/* -----------------------------------------------------------------------
 * pcie_board.c — device reservation and board attribute access/mutation
 * ----------------------------------------------------------------------- */
BOOL  LibObtainCard(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
void  LibReleaseCard(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
LONG  LibFLR(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
BOOL  LibSetMaster(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"));
ULONG LibGetBoardAttrsA(struct pci_dev *dev asm("a0"), struct TagItem *tags asm("a1"), struct PCIELibBase *base asm("a6"));
BOOL  LibSetBoardAttrsA(struct pci_dev *dev asm("a0"), struct TagItem *tags asm("a1"), struct PCIELibBase *base asm("a6"));

#endif /* _PCIE_PRIVATE_H */
