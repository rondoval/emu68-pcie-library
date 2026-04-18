// SPDX-License-Identifier: GPL-2.0-only

#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/tasks.h>
#include <utility/tagitem.h>
#include <pcie_private.h>
#include <pcie_brcmstb.h>
#include <pci.h>
#include <pci_util.h>
#include <pci_bar.h>
#include <pci_probe.h>
#include <pci_io.h>
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
 * Allocate and populate one openpci pci_dev from an internal pci_device.
 *
 * base_address[i] is set to the 68k-accessible virtual address of each BAR.
 * The BCM2711 PCIe controller maps the PCIe bus addresses through an outbound
 * window to ARM physical addresses above 4 GB; the ARM MMU then maps those into
 * a 32-bit MMIO window so that emu68 can reach them.  pci_bus_to_virt() performs
 * both steps (bus → phys via regions[], phys → virt via mmio_window_virtual).
 *
 * base_size[i] stores the raw openpci sizing mask (~(size - 1) & addr_mask),
 * following the convention in libraries/openpci.h.
 *
 * Returns the allocated pdev on success, NULL on allocation failure.
 */
static struct pci_dev *pcie_make_pdev(struct pci_device *idev)
{
    struct pci_dev *pdev = AllocMem(sizeof(*pdev), MEMF_CLEAR | MEMF_PUBLIC);
    if (!pdev)
        return NULL;

    /* Convert U-Boot devfn (device<<11 | func<<8) to Linux/openpci (device<<3 | func) */
    pdev->devfn    = (UBYTE)((PCI_DEV(idev->devfn) << 3) | PCI_FUNC(idev->devfn));
    pdev->vendor   = idev->vendor;
    pdev->device   = idev->device;
    pdev->devclass = idev->class;
    pdev->irq      = (ULONG)idev->irq_line;

    for (u32 i = 0; i < idev->bars_num; ++i)
    {
        if (!idev->bars[i].present)
            continue;

        /* bar_response is the raw BAR sizing result with flags included (pci_size_t);
         * clamp to ULONG for the 32-bit openpci base_size[] field. */
        pdev->base_size[i]    = (ULONG)idev->bars[i].bar_response;
        pdev->base_address[i] = (ULONG)idev->bars[i].virt_addr;

        if (idev->bars[i].is64)
            ++i;
    }

    pdev->reserved = idev;
    return pdev;
}

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

            struct pci_dev *pdev = pcie_make_pdev(idev);
            if (!pdev)
            {
                Kprintf("[pcie] %s: out of memory building device list\n", __func__);
                return -1;
            }

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
        goto err_ctrl_alloc;
    }
    _NewMinList(&base->ctrl->buses);
    base->ctrl->gic400Base = base->gic400Base;

    s32 res = brcm_pcie_probe(base->ctrl, 0);
    if (res < 0)
    {
        Kprintf("[pcie] %s: brcm_pcie_probe failed (%ld)\n", __func__, (LONG)res);
        goto err_probe;
    }
    Kprintf("[pcie] %s: controller probed successfully\n", __func__);

    base->rootBus = AllocMem(sizeof(*base->rootBus), MEMF_CLEAR | MEMF_PUBLIC);
    if (!base->rootBus)
    {
        Kprintf("[pcie] %s: out of memory for root bus\n", __func__);
        goto err_root_bus;
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
        goto err_dev_list;
    }
    Kprintf("[pcie] %s: bus devices bound successfully\n", __func__);

    res = pci_auto_config_devices(base->rootBus);
    if (res < 0)
    {
        Kprintf("[pcie] %s: pci_auto_config_devices failed (%ld)\n", __func__, (LONG)res);
        goto err_dev_list;
    }
    Kprintf("[pcie] %s: devices auto-configured successfully\n", __func__);

    (void)bcm2711_reload_vl805_firmware();
    delay_us(1000);
    Kprintf("[pcie] %s: VL805 firmware reloaded\n", __func__);

    res = pcie_build_dev_list(base);
    if (res != 0)
        goto err_dev_list;

    base->ctrlReady = TRUE;
    Kprintf("[pcie] %s: controller ready\n", __func__);

    base->dmaPool = CreatePool(MEMF_PUBLIC | MEMF_FAST, 4096, 4096);
    if (!base->dmaPool)
    {
        Kprintf("[pcie] %s: out of memory for DMA pool\n", __func__);
        goto err_dma_pool;
    }

    return 0;

err_dma_pool:
    base->ctrlReady = FALSE;
err_dev_list:
    pcie_free_dev_list(base);
    FreeMem(base->rootBus, sizeof(*base->rootBus));
    base->rootBus = NULL;
err_root_bus:
    brcm_pcie_remove(base->ctrl);
err_probe:
    FreeMem(base->ctrl, sizeof(*base->ctrl));
    base->ctrl = NULL;
err_ctrl_alloc:
    CloseLibrary(base->gic400Base);
    base->gic400Base = NULL;
    return -1;
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
    if (base->dmaPool) { DeletePool(base->dmaPool); base->dmaPool = NULL; }
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

static UWORD LibPCIBus(struct PCIELibBase *base asm("a6"))
{
    return base->ctrlReady ? BCM2711PCIeBus : 0;
}

static ULONG LibPrivate(struct PCIELibBase *base asm("a6"))
{
    (void)base;
    return 0;
}

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
    /* Extended config-space access (ULONG reg, supports offsets >= 0x100) */
    (APTR)LibReadExtConfigByte,   /* -288 */
    (APTR)LibReadExtConfigWord,   /* -294 */
    (APTR)LibReadExtConfigLong,   /* -300 */
    (APTR)LibWriteExtConfigByte,  /* -306 */
    (APTR)LibWriteExtConfigWord,  /* -312 */
    (APTR)LibWriteExtConfigLong,  /* -318 */
    /* ISR-safe interrupt control */
    (APTR)LibMaskMSI,             /* -324 */
    (APTR)LibUnmaskMSI,           /* -330 */
    (APTR)LibCheckSetINTxMask,    /* -336 */
    (APTR)-1,
};

static const APTR initTable[4] = {
    (APTR)sizeof(struct PCIELibBase),
    (APTR)funcTable,
    NULL,
    (APTR)LibInit,
};
