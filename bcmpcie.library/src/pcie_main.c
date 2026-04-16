// SPDX-License-Identifier: GPL-2.0-only

#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/tasks.h>
#include <utility/tagitem.h>
#include <pcie_private.h>
#include <pcie_brcmstb.h>
#include <pci.h>
#include <minlist.h>
#include <timing.h>
#include <debug.h>

LONG __attribute__((used, no_reorder)) doNotExecute(void);
LONG __attribute__((used, no_reorder)) doNotExecute(void)
{
    return -1;
}

extern const UBYTE endOfCode;
static const char libraryName[] = LIBRARY_NAME;
static const char libraryIdString[] = LIBRARY_IDSTRING;
static const APTR initTable[4];

const struct Resident pcieResident __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&pcieResident,
    (APTR)&endOfCode,
    RTF_AUTOINIT,
    LIBRARY_VERSION,
    NT_LIBRARY,
    LIBRARY_PRIORITY,
    (APTR)&libraryName,
    (APTR)&libraryIdString,
    (APTR)initTable,
};


/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/*
 * Walk all buses in the controller and build the pci_dev linked list.
 * Returns 0 on success, negative on allocation failure.
 */
static s32 pcie_build_dev_list(struct PCIELibBase *base)
{
    Kprintf("[pcie] Building device list...\n");
    struct pci_dev *prev = NULL;
    s32 bus_max = pci_get_bus_max(base->ctrl);

    for (u32 bus_index = 0; bus_max >= 0 && bus_index <= (u32)bus_max; ++bus_index)
    {
        Kprintf("[pcie] Scanning bus %ld...\n", (ULONG)bus_index);
        struct pci_bus *bus;
        if (pci_get_bus(base->ctrl, bus_index, &bus) != 0)
            continue;

        for (struct MinNode *node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
        {
            struct pci_device *idev = (struct pci_device *)node;
            Kprintf("[pcie] Found device %04x:%04x (bus %ld dev %ld func %ld)\n",
                    (ULONG)idev->vendor, (ULONG)idev->device,
                    (ULONG)bus_index, (ULONG)PCI_DEV(idev->devfn), (ULONG)PCI_FUNC(idev->devfn));

            struct pci_dev *pdev = AllocMem(sizeof(*pdev), MEMF_CLEAR | MEMF_PUBLIC);
            if (!pdev)
            {
                Kprintf("[pcie] %s: out of memory building device list\n", __func__);
                return -1;
            }

            /* Convert U-Boot devfn (device<<11 | func<<8) to Linux/openpci (device<<3 | func) */
            pdev->devfn   = (UBYTE)((PCI_DEV(idev->devfn) << 3) | PCI_FUNC(idev->devfn));
            pdev->vendor  = idev->vendor;
            pdev->device  = idev->device;
            pdev->devclass = idev->class;
            pdev->irq     = (ULONG)idev->irq;

            /* Populate base_address[] and base_size[] from config space.
             *
             * Size probing: write all-ones to the BAR register, read back a
             * sizing response, restore the original assigned address.
             *
             * base_size[n] follows the openpci convention documented in
             * libraries/openpci.h: it stores the raw sizing mask
             * (response & address_mask), which equals ~(actual_size - 1).
             * A consumer must apply ~base_size[n] + 1 to recover the actual
             * byte size.  Zero means the BAR is not implemented.
             *
             * pcie.library's own GetBARInfo extension decodes this
             * internally and returns the actual size directly.
             *
             * This probing is safe here because it runs only once, at first
             * LibOpen, before any consumer starts using the devices.
             */
            for (u32 i = 0; i < 6; ++i)
            {
                u32 bar_reg = PCI_BASE_ADDRESS_0 + i * 4;
                u32 saved;
                pci_read_config32(idev, bar_reg, &saved);

                /* Read assigned address (flag bits masked out) */
                pdev->base_address[i] = pci_read_bar32(idev, i);

                /* Probe size */
                pci_write_config32(idev, bar_reg, 0xFFFFFFFFu);
                u32 response;
                pci_read_config32(idev, bar_reg, &response);
                pci_write_config32(idev, bar_reg, saved); /* restore */

                if (response == 0 || response == 0xFFFFFFFFu)
                {
                    pdev->base_size[i] = 0; /* BAR not present */
                }
                else if (response & PCI_BASE_ADDRESS_SPACE_IO)
                {
                    /* Store raw I/O sizing mask per openpci convention */
                    pdev->base_size[i] = response & (u32)PCI_BASE_ADDRESS_IO_MASK;
                }
                else
                {
                    u32 type = (response >> 1) & 3u;
                    if (type == 2)
                    {
                        /* 64-bit BAR: next slot holds the high 32 bits (unused) */
                        u32 hi;
                        pci_read_config32(idev, bar_reg + 4, &hi);
                        (void)hi; /* we only support 32-bit addresses for now */
                        /* Store raw MEM sizing mask per openpci convention */
                        pdev->base_size[i] = response & (u32)PCI_BASE_ADDRESS_MEM_MASK;
                        /* Mark the high-word slot as padding */
                        if (i + 1 < 6)
                            pdev->base_size[i + 1] = 0;
                        ++i;
                    }
                    else
                    {
                        /* Store raw MEM sizing mask per openpci convention */
                        pdev->base_size[i] = response & (u32)PCI_BASE_ADDRESS_MEM_MASK;
                    }
                }
            }

            /* Store back-pointer to the internal pci_device in the reserved field */
            pdev->reserved = idev;

            if (prev == NULL)
            {
                base->devListHead = pdev;
                pdev->pred = NULL;
            }
            else
            {
                prev->next = pdev;
                pdev->pred = prev;
            }
            pdev->next = NULL;
            prev = pdev;
        }
    }
    return 0;
}

/*
 * Free all pci_dev nodes in the linked list.
 */
static void pcie_free_dev_list(struct PCIELibBase *base)
{
    struct pci_dev *cur = base->devListHead;
    while (cur != NULL)
    {
        struct pci_dev *next = cur->next;
        FreeMem(cur, sizeof(*cur));
        cur = next;
    }
    base->devListHead = NULL;
}

/*
 * Perform controller probe and bus enumeration.
 * Called from LibOpen under the semaphore when lib_OpenCnt transitions 0→1.
 * Returns 0 on success.
 */
static s32 pcie_hw_init(struct PCIELibBase *base)
{
    base->gic400Base = OpenLibrary((CONST_STRPTR)"gic400.library", 1);
    if (!base->gic400Base)
    {
        Kprintf("[pcie] %s: failed to open gic400.library\n", __func__);
        return -1;
    }
    Kprintf("[pcie] %s: gic400.library opened successfully\n", __func__);

    base->ctrl = AllocMem(sizeof(*base->ctrl), MEMF_CLEAR | MEMF_PUBLIC);
    if (!base->ctrl)
    {
        Kprintf("[pcie] %s: out of memory for pci_controller\n", __func__);
        CloseLibrary(base->gic400Base);
        base->gic400Base = NULL;
        return -1;
    }
    _NewMinList(&base->ctrl->buses);
    base->ctrl->gic400Base = base->gic400Base;

    s32 res = brcm_pcie_probe(base->ctrl, 0);
    if (res < 0)
    {
        Kprintf("[pcie] %s: brcm_pcie_probe failed (%ld)\n", __func__, (LONG)res);
        FreeMem(base->ctrl, sizeof(*base->ctrl));
        base->ctrl = NULL;
        CloseLibrary(base->gic400Base);
        base->gic400Base = NULL;
        return -1;
    }
    Kprintf("[pcie] %s: controller probed successfully\n", __func__);

    base->rootBus = AllocMem(sizeof(*base->rootBus), MEMF_CLEAR | MEMF_PUBLIC);
    if (!base->rootBus)
    {
        Kprintf("[pcie] %s: out of memory for root bus\n", __func__);
        brcm_pcie_remove(base->ctrl);
        FreeMem(base->ctrl, sizeof(*base->ctrl));
        base->ctrl = NULL;
        CloseLibrary(base->gic400Base);
        base->gic400Base = NULL;
        return -1;
    }
    _NewMinList(&base->rootBus->devices);
    base->rootBus->controller = base->ctrl;
    base->rootBus->parent     = NULL;
    base->rootBus->pci_bridge = NULL;
    CopyMem((APTR)"pcie0", base->rootBus->name, 6);
    base->rootBus->bus_number = 0;
    AddTailMinList(&base->ctrl->buses, (struct MinNode *)base->rootBus);
    Kprintf("[pcie] %s: root bus initialized\n", __func__);

    res = pci_bind_bus_devices(base->rootBus);
    if (res != 0)
    {
        Kprintf("[pcie] %s: pci_bind_bus_devices failed (%ld)\n", __func__, (LONG)res);
        FreeMem(base->rootBus, sizeof(*base->rootBus));
        base->rootBus = NULL;
        brcm_pcie_remove(base->ctrl);
        FreeMem(base->ctrl, sizeof(*base->ctrl));
        base->ctrl = NULL;
        CloseLibrary(base->gic400Base);
        base->gic400Base = NULL;
        return -1;
    }
    Kprintf("[pcie] %s: bus devices bound successfully\n", __func__);

    res = pci_auto_config_devices(base->rootBus);
    if (res < 0)
    {
        Kprintf("[pcie] %s: pci_auto_config_devices failed (%ld)\n", __func__, (LONG)res);
        FreeMem(base->rootBus, sizeof(*base->rootBus));
        base->rootBus = NULL;
        brcm_pcie_remove(base->ctrl);
        FreeMem(base->ctrl, sizeof(*base->ctrl));
        base->ctrl = NULL;
        CloseLibrary(base->gic400Base);
        base->gic400Base = NULL;
        return -1;
    }
    Kprintf("[pcie] %s: devices auto-configured successfully\n", __func__);

    (void)bcm2711_reload_vl805_firmware();
    delay_us(1000);
    Kprintf("[pcie] %s: VL805 firmware reloaded\n", __func__);

    res = pcie_build_dev_list(base);
    if (res != 0)
    {
        pcie_free_dev_list(base);
        FreeMem(base->rootBus, sizeof(*base->rootBus));
        base->rootBus = NULL;
        brcm_pcie_remove(base->ctrl);
        FreeMem(base->ctrl, sizeof(*base->ctrl));
        base->ctrl = NULL;
        CloseLibrary(base->gic400Base);
        base->gic400Base = NULL;
        return -1;
    }

    base->ctrlReady = TRUE;
    Kprintf("[pcie] %s: controller ready\n", __func__);
    return 0;
}

/*
 * Tear down controller and all associated state.
 * Called from LibClose when lib_OpenCnt drops to 0.
 * Must be called with the semaphore held.
 */
static void pcie_hw_shutdown(struct PCIELibBase *base)
{
    pcie_free_dev_list(base);
    brcm_pcie_remove(base->ctrl);
    FreeMem(base->rootBus, sizeof(*base->rootBus));
    base->rootBus = NULL;
    FreeMem(base->ctrl, sizeof(*base->ctrl));
    base->ctrl = NULL;
    CloseLibrary(base->gic400Base);
    base->gic400Base = NULL;
    base->ctrlReady = FALSE;
    Kprintf("[pcie] %s: controller shut down\n", __func__);
}

/* -----------------------------------------------------------------------
 * Standard library lifecycle
 * ----------------------------------------------------------------------- */

static ULONG LibExpunge(struct PCIELibBase *base asm("a6"))
{
    ULONG segList = base->segList;

    if (base->libNode.lib_OpenCnt > 0)
    {
        base->libNode.lib_Flags |= LIBF_DELEXP;
        return 0;
    }

    Forbid();
    Remove((struct Node *)base);
    Permit();

    ULONG size = (ULONG)base->libNode.lib_NegSize + base->libNode.lib_PosSize;
    FreeMem((APTR)((ULONG)base - base->libNode.lib_NegSize), size);

    return segList;
}

static struct Library *LibInit(struct Library *libBase asm("d0"), ULONG seglist asm("a0"), struct ExecBase *execBase asm("a6"))
{
    struct PCIELibBase *base = (struct PCIELibBase *)libBase;
    (void)execBase;

    base->segList = seglist;
    base->libNode.lib_Revision = (UWORD)LIBRARY_REVISION;

    InitSemaphore(&base->semaphore);
    _NewMinList(&base->reservations);
    base->devListHead = NULL;
    base->ctrl        = NULL;
    base->rootBus     = NULL;
    base->gic400Base  = NULL;
    base->ctrlReady   = FALSE;

    return libBase;
}

static struct PCIELibBase *LibOpen(ULONG version asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)version;

    ObtainSemaphore(&base->semaphore);

    if (!base->ctrlReady)
    {
        if (pcie_hw_init(base) != 0)
        {
            ReleaseSemaphore(&base->semaphore);
            return NULL;
        }
    }

    base->libNode.lib_OpenCnt++;
    base->libNode.lib_Flags &= (UBYTE)~LIBF_DELEXP;

    ReleaseSemaphore(&base->semaphore);
    return base;
}

static ULONG LibClose(struct PCIELibBase *base asm("a6"))
{
    ObtainSemaphore(&base->semaphore);

    pcie_release_task_reservations(base, FindTask(NULL));

    base->libNode.lib_OpenCnt--;

    if (base->libNode.lib_OpenCnt == 0)
        pcie_hw_shutdown(base);

    ReleaseSemaphore(&base->semaphore);

    if (base->libNode.lib_OpenCnt == 0)
    {
        if (base->libNode.lib_Flags & LIBF_DELEXP)
            return LibExpunge(base);
    }

    return 0;
}

static ULONG LibNull(void)
{
    return 0;
}

/* -----------------------------------------------------------------------
 * pci_bus() — the only real implementation in Phase 1
 * ----------------------------------------------------------------------- */

static UWORD LibPCIBus(struct PCIELibBase *base asm("a6"))
{
    return base->ctrlReady ? BCM2711PCIeBus : 0;
}

/* -----------------------------------------------------------------------
 * Phase 3 — device reservation
 * ----------------------------------------------------------------------- */

static ULONG LibPrivate(struct PCIELibBase *base asm("a6"))
{
    (void)base;
    return 0;
}

/*
 * Install an interrupt server for the given device's IRQ.
 * The IRQ number stored in pci_dev.irq maps directly to the GIC-400 SPI line
 * (i.e. it is the GIC interrupt number as assigned by the static lib during
 * device enumeration, shifted by +32 on the Pi4 to reach the SPI range).
 * Returns TRUE on success.
 * Implemented in pcie_irq.c to keep gic400.library headers isolated from
 * this file's library-vector function definitions.
 */

/* -----------------------------------------------------------------------
 * Function table and init table
 * ----------------------------------------------------------------------- */

static const APTR funcTable[] = {
    /* Standard library vectors */
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibNull,
    /* openpci-compatible API — LVO -30 onwards */
    (APTR)LibPCIBus,              /* -30  */
    (APTR)LibPCIInb,              /* -36  */
    (APTR)LibPCIOutb,             /* -42  */
    (APTR)LibPCIInw,              /* -48  */
    (APTR)LibPCIOutw,             /* -54  */
    (APTR)LibPCIInl,              /* -60  */
    (APTR)LibPCIOutl,             /* -66  */
    (APTR)LibPCIToHostCpy,        /* -72  */
    (APTR)LibHostToPCICpy,        /* -78  */
    (APTR)LibPCIToPCICpy,         /* -84  */
    (APTR)LibFindDevice,          /* -90  */
    (APTR)LibFindClass,           /* -96  */
    (APTR)LibFindSlot,            /* -102 */
    (APTR)LibReadConfigByte,      /* -108 */
    (APTR)LibReadConfigWord,      /* -114 */
    (APTR)LibReadConfigLong,      /* -120 */
    (APTR)LibWriteConfigByte,     /* -126 */
    (APTR)LibWriteConfigWord,     /* -132 */
    (APTR)LibWriteConfigLong,     /* -138 */
    (APTR)LibSetMaster,           /* -144 */
    (APTR)LibAddIntServer,        /* -150 */
    (APTR)LibRemIntServer,        /* -156 */
    (APTR)LibAllocDMAMem,         /* -162 */
    (APTR)LibFreeDMAMem,          /* -168 */
    (APTR)LibLogicToPhysic,       /* -174 */
    (APTR)LibPhysicToLogic,       /* -180 */
    (APTR)LibObtainCard,          /* -186 */
    (APTR)LibReleaseCard,         /* -192 */
    (APTR)LibPrivate,             /* -198 */
    (APTR)LibFindBoardA,          /* -204 */
    (APTR)LibGetBoardAttrsA,      /* -210 */
    (APTR)LibSetBoardAttrsA,      /* -216 */
    (APTR)LibAllocDMAForBoard,    /* -222 */
    (APTR)LibReleaseDMAForBoard,  /* -228 */
    (APTR)LibAddMemHandler,       /* -234 */
    (APTR)LibRemMemHandler,       /* -240 */
    (APTR)LibObtainPCIRegion,     /* -246 */
    (APTR)LibReleasePCIRegion,    /* -252 */
    /* PCIe extensions */
    (APTR)LibFindCapability,      /* -258 */
    (APTR)LibFindExtCapability,   /* -264 */
    (APTR)LibEnableMSI,           /* -270 */
    (APTR)LibDisableMSI,          /* -276 */
    (APTR)LibFLR,                 /* -282 */
    (APTR)LibMapBAR,              /* -288 */
    (APTR)LibGetBARInfo,          /* -294 */
    /* Extended config-space access (ULONG reg, supports offsets >= 0x100) */
    (APTR)LibReadExtConfigByte,   /* -300 */
    (APTR)LibReadExtConfigWord,   /* -306 */
    (APTR)LibReadExtConfigLong,   /* -312 */
    (APTR)LibWriteExtConfigByte,  /* -318 */
    (APTR)LibWriteExtConfigWord,  /* -324 */
    (APTR)LibWriteExtConfigLong,  /* -330 */
    /* ISR-safe interrupt control */
    (APTR)LibMaskMSI,             /* -336 */
    (APTR)LibUnmaskMSI,           /* -342 */
    (APTR)LibCheckSetINTxMask,    /* -348 */
    (APTR)-1,
};

static const APTR initTable[4] = {
    (APTR)sizeof(struct PCIELibBase),
    (APTR)funcTable,
    NULL,
    (APTR)LibInit,
};
