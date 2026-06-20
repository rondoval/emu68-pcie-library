// SPDX-License-Identifier: GPL-2.0-only

/*
 * pcie_board.c — device reservation and board attribute access/mutation.
 *
 * LibObtainCard / LibReleaseCard:
 *   Reserve a PCI device for exclusive use by the calling task.
 *   The reservation list is protected by base->semaphore, which is an
 *   AmigaOS SignalSemaphore (re-entrant, signal-safe).
  *
 * LibGetBoardAttrsA / LibSetBoardAttrsA:
 *   Read/write device attributes using the openpci v3 tag-based API
 *   (PRM_* tags from <libraries/pcitags.h>).
 */

#include <pcie_private.h>
#include <pci.h>
#include <bcm2711.h>
#include <pci_io.h>
#include <pci_util.h>
#include <iomem.h>
#include <minlist.h>
#include <utility/tagitem.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/* -----------------------------------------------------------------------
 * Device reservation
 * ----------------------------------------------------------------------- */

/*
 * Mark a device as owned by the calling task.
 * Returns TRUE on success, FALSE if already reserved by another task.
 */
BOOL LibObtainCard(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev)
        return FALSE;

    ObtainSemaphore(&base->semaphore);
    if (dev->owner)
    {
        KprintfH("[pcie] %s: device %04x:%04x already reserved by %s\n",
                 __func__,
                 (ULONG)dev->vendor, (ULONG)dev->device,
                 dev->owner->ln_Name ? dev->owner->ln_Name : "<unnamed>");
        ReleaseSemaphore(&base->semaphore);
        return FALSE;
    }
    dev->owner = (struct Node *)FindTask(NULL);
    ReleaseSemaphore(&base->semaphore);
    return TRUE;
}

/*
 * Release the reservation held by the calling task for the given device.
 * A no-op if the device is not owned by the calling task.
 */
void LibReleaseCard(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev)
        return;

    ObtainSemaphore(&base->semaphore);
    if (dev->owner == (struct Node *)FindTask(NULL))
        dev->owner = NULL;
    ReleaseSemaphore(&base->semaphore);
}

/* -----------------------------------------------------------------------
 * Board attribute read (openpci v3 tag API)
 * ----------------------------------------------------------------------- */

ULONG LibGetBoardAttrsA(struct pci_dev *dev asm("a0"), struct TagItem *tags asm("a1"), struct PCIELibBase *base asm("a6"))
{
    if (!dev || !tags) return 0;

    struct pci_device *idev = pcie_dev_from_openpci(dev);
    ULONG count = 0;

    ObtainSemaphore(&base->semaphore);
    for (struct TagItem *tag = tags; (ULONG)tag->ti_Tag != TAG_DONE; ++tag)
    {
        if ((ULONG)tag->ti_Tag == TAG_IGNORE) continue;

        ULONG *dest = (ULONG *)tag->ti_Data;
        if (!dest) continue;

        switch ((ULONG)tag->ti_Tag)
        {
        case PRM_Vendor:          *dest = (ULONG)dev->vendor;                         ++count; break;
        case PRM_Device:          *dest = (ULONG)dev->device;                         ++count; break;
        case PRM_Class:           *dest = (dev->devclass >> 16) & 0xFFu;              ++count; break;
        case PRM_SubsysVendor:    *dest = (ULONG)idev->subsys_vendor;                 ++count; break;
        case PRM_SubsysID:        *dest = (ULONG)idev->subsys_id;                     ++count; break;
        case PRM_SubClass:        *dest = (dev->devclass >>  8) & 0xFFu;              ++count; break;
        case PRM_Revision:        *dest = (ULONG)idev->revision;                      ++count; break;
        case PRM_HeaderType:      *dest = (ULONG)idev->header_type;                   ++count; break;
        case PRM_Interface:       *dest =  dev->devclass & 0xFFu;                     ++count; break;
        case PRM_BusNumber:       *dest = (ULONG)idev->bus->bus_number;               ++count; break;
        case PRM_SlotNumber:      *dest = (ULONG)(dev->devfn >> 3);                   ++count; break;
        case PRM_FunctionNumber:  *dest = (ULONG)(dev->devfn & 7u);                   ++count; break;
        case PRM_InterruptLine:   *dest = (ULONG)idev->intx.pin_routed;               ++count; break;
        case PRM_InterruptPin:    *dest = (ULONG)idev->intx.pin;                      ++count; break;
        case PRM_LegacyIOSpace:   *dest = (ULONG)dev->legacy_io;                      ++count; break;
        case PRM_PCIToHostOffset: *dest = 0;                                          ++count; break;
        case PRM_ROM_Address:     *dest = (ULONG)dev->rom_address;                    ++count; break;
        case PRM_ROM_Size:        *dest = (ULONG)dev->rom_size;                       ++count; break;
        case PRM_ROM_Flags: {
            u32 raw = 0;
            pci_read_config32(idev, PCI_ROM_ADDRESS, &raw);
            /* Bit 0: 1 = ROM decode enabled, 0 = disabled */
            *dest = (ULONG)(raw & 1u);
            ++count; break;
        }

        case PRM_ConfigArea: {
            /*
             * For the root complex (bus 0) the config registers are directly
             * mapped at ctrl->base.  For endpoint devices the BCM2711 uses a
             * shared index/data window: write the ECAM index for this device
             * into PCIE_EXT_CFG_INDEX and the 4 KB window at PCIE_EXT_CFG_DATA
             * becomes this device's config space — until the next config access
             * reprograms it.  Return that window base after selecting the device.
             */
            pci_dev_t bdf = (pci_dev_t)pci_get_bdf(idev);
            if (PCI_BUS(bdf) == 0) {
                *dest = (ULONG)base->ctrl->base;
            } else {
                u32 idx = PCIE_ECAM_OFFSET(PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf), 0);
                mmio_write32(idx, (APTR)((ULONG)base->ctrl->base + PCIE_EXT_CFG_INDEX));
                *dest = (ULONG)base->ctrl->base + PCIE_EXT_CFG_DATA;
            }
            ++count; break;
        }

        case PRM_BoardOwner:
            ObtainSemaphoreShared(&base->semaphore);
            *dest = (ULONG)dev->owner;
            ReleaseSemaphore(&base->semaphore);
            ++count;
            break;

        /* PRM_MemoryAddrN (0x6EDA0010..0x6EDA0015) */
        case PRM_MemoryAddr0: case PRM_MemoryAddr1: case PRM_MemoryAddr2:
        case PRM_MemoryAddr3: case PRM_MemoryAddr4: case PRM_MemoryAddr5:
            *dest = (ULONG)dev->base_address[(ULONG)tag->ti_Tag - PRM_MemoryAddr0];
            ++count; break;

        /* PRM_MemorySizeN (0x6EDA0020..0x6EDA0025) — decode raw BAR sizing mask */
        case PRM_MemorySize0: case PRM_MemorySize1: case PRM_MemorySize2:
        case PRM_MemorySize3: case PRM_MemorySize4: case PRM_MemorySize5: {
            ULONG bar_num = (ULONG)tag->ti_Tag - PRM_MemorySize0;
            ULONG raw  = (ULONG)dev->base_size[bar_num];
            ULONG mask = (dev->base_address[bar_num] & PCI_BASE_ADDRESS_SPACE_IO)
                         ? (ULONG)PCI_BASE_ADDRESS_IO_MASK
                         : (ULONG)PCI_BASE_ADDRESS_MEM_MASK;
            *dest = raw ? (~(raw & mask) + 1u) : 0u;
            ++count; break;
        }

        /* PRM_MemoryFlagsN (0x6EDA0030..0x6EDA0035) — low 4 bits of raw BAR sizing response */
        case PRM_MemoryFlags0: case PRM_MemoryFlags1: case PRM_MemoryFlags2:
        case PRM_MemoryFlags3: case PRM_MemoryFlags4: case PRM_MemoryFlags5:
            *dest = dev->base_size[(ULONG)tag->ti_Tag - PRM_MemoryFlags0] & 0xFu;
            ++count; break;

        case PRM_PCIMemWindowLow:
            *dest = base->ctrl->pci_mem ? (ULONG)base->ctrl->pci_mem->bus_start : 0u;
            ++count; break;

        case PRM_PCIMemWindowHigh:
            *dest = base->ctrl->pci_mem
                    ? (ULONG)(base->ctrl->pci_mem->bus_start + base->ctrl->pci_mem->size)
                    : 0u;
            ++count; break;

        default:
            /* Unrecognised attribute — skip, do not increment count. */
            break;
        }
    }
    ReleaseSemaphore(&base->semaphore);
    return count;
}

/* -----------------------------------------------------------------------
 * Board attribute write (openpci v3 tag API)
 * ----------------------------------------------------------------------- */

BOOL LibSetBoardAttrsA(struct pci_dev *dev asm("a0"), struct TagItem *tags asm("a1"), struct PCIELibBase *base asm("a6"))
{
    if (!dev || !tags)
        return FALSE;

    BOOL all_ok = TRUE;

    for (const struct TagItem *tag = tags; (ULONG)tag->ti_Tag != TAG_DONE; ++tag)
    {
        if ((ULONG)tag->ti_Tag == TAG_IGNORE)
            continue;

        switch ((ULONG)tag->ti_Tag)
        {
        case PRM_BoardOwner:
            ObtainSemaphore(&base->semaphore);
            if (tag->ti_Data != 0)
            {
                if (dev->owner == NULL)
                    dev->owner = (struct Node *)tag->ti_Data;
                else
                    all_ok = FALSE;
            }
            else
            {
                dev->owner = NULL;
            }
            ReleaseSemaphore(&base->semaphore);
            break;

        default:
            /* All other tags are read-only from this function's perspective. */
            all_ok = FALSE;
            break;
        }
    }
    return all_ok;
}

/* -----------------------------------------------------------------------
 * Function Level Reset and bus-master enable
 * ----------------------------------------------------------------------- */

/*
 * LibFLR — trigger a Function Level Reset if the device supports it.
 * Returns PCIE_OK on success, PCIE_ERR_INVAL for a NULL/invalid device, or
 * PCIE_ERR_NOTSUPP if the device does not support FLR.
 */
LONG LibFLR(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev)
        return PCIE_ERR_INVAL;
    /* pci_flr() returns 0 on success or -ENOENT when FLR is unsupported. */
    return (pci_flr(pcie_dev_from_openpci(dev)) == 0) ? PCIE_OK : PCIE_ERR_NOTSUPP;
}

/*
 * LibSetMaster — enable bus-mastering for the given device by setting
 * PCI_COMMAND_MASTER in the PCI command register.
 */
BOOL LibSetMaster(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    if (!dev)
        return FALSE;
    struct pci_device *idev = pcie_dev_from_openpci(dev);
    u16 cmd = 0;
    ObtainSemaphore(&base->semaphore);
    pci_read_config16(idev, PCI_COMMAND, &cmd);
    pci_write_config16(idev, PCI_COMMAND, (u32)(cmd | (u16)PCI_COMMAND_MASTER));
    pci_read_config16(idev, PCI_COMMAND, &cmd);
    ReleaseSemaphore(&base->semaphore);
    dev->master = (UWORD)cmd;
    return TRUE;
}
