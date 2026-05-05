// SPDX-License-Identifier: GPL-2.0-only

/*
 * pcie_find.c — device enumeration: openpci-compatible find functions and
 *               the v3 tag-API FindBoardA.
 *
 * LibFindDevice / LibFindClass / LibFindSlot:
 *   Walk the pci_dev linked list built at first LibOpen.  Exact counterparts
 *   to the OpenPCI functions of the same name.
 *
 * LibFindBoardA:
 *   Tag-based board search (openpci v3 API).  Iterates from after 'prev' and
 *   returns the first device that satisfies all supplied tag constraints.
 */

#include <pcie_private.h>
#include <utility/tagitem.h>
#include <libraries/pci_constants.h>
#include <pci_io.h>
#include <pci_capability.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/* -----------------------------------------------------------------------
 * Simple find functions (openpci-compatible API)
 * ----------------------------------------------------------------------- */

/*
 * Walk the pci_dev linked list starting after 'prev' (NULL = from the head).
 * Returns the first node whose vendor and device fields both match, or NULL.
 */
struct pci_dev *LibFindDevice(UWORD vendor asm("d0"), UWORD device asm("d1"), struct pci_dev *prev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    struct pci_dev *cur = prev ? prev->next : base->devListHead;
    while (cur)
    {
        if ((vendor == (UWORD)0xFFFF || cur->vendor == vendor) &&
            (device == (UWORD)0xFFFF || cur->device == device))
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/*
 * Walk the list starting after 'prev' (NULL = from head).
 * devclass is the 24-bit openpci class code (base<<16 | sub<<8 | prog-if).
 */
struct pci_dev *LibFindClass(ULONG devclass asm("d0"), struct pci_dev *prev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    struct pci_dev *cur = prev ? prev->next : base->devListHead;
    while (cur)
    {
        if (cur->devclass == devclass)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/*
 * Find a device by bus number and Linux-style devfn (device<<3 | function).
 * Searches from the head of the list; there is at most one match.
 */
struct pci_dev *LibFindSlot(UBYTE bus asm("d0"), ULONG devfn asm("d1"), struct PCIELibBase *base asm("a6"))
{
    for (struct pci_dev *cur = base->devListHead; cur; cur = cur->next)
    {
        const struct pci_device *idev = pcie_dev_from_openpci(cur);
        if (idev->bus->bus_number == (u32)bus && cur->devfn == (UBYTE)devfn)
            return cur;
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Tag-based board search (openpci v3 API)
 * ----------------------------------------------------------------------- */

struct pci_dev *LibFindBoardA(struct pci_dev *prev asm("a0"), struct TagItem *tags asm("a1"), struct PCIELibBase *base asm("a6"))
{
    if (!base->ctrlReady) return NULL;

    ObtainSemaphore(&base->semaphore);
    struct pci_dev *cur = (prev != NULL) ? prev->next : base->devListHead;
    struct pci_dev *result = NULL;

    while (cur != NULL)
    {
        struct pci_device *idev = pcie_dev_from_openpci(cur);
        BOOL match = TRUE;

        for (const struct TagItem *tag = tags; tag->ti_Tag != TAG_DONE; ++tag)
        {
            switch ((ULONG)tag->ti_Tag)
            {
            case TAG_IGNORE:
                break;

            case PRM_Vendor:
                if (cur->vendor != (UWORD)tag->ti_Data) match = FALSE;
                break;
            case PRM_Device:
                if (cur->device != (UWORD)tag->ti_Data) match = FALSE;
                break;
            case PRM_Class:
                if (((cur->devclass >> 16) & 0xFFu) != (tag->ti_Data & 0xFFu)) match = FALSE;
                break;
            case PRM_SubClass:
                if (((cur->devclass >> 8) & 0xFFu) != (tag->ti_Data & 0xFFu)) match = FALSE;
                break;
            case PRM_Interface:
                if ((cur->devclass & 0xFFu) != (tag->ti_Data & 0xFFu)) match = FALSE;
                break;
            case PRM_Revision:
                if (idev->revision != (UBYTE)tag->ti_Data) match = FALSE;
                break;
            case PRM_BusNumber:
                if (idev->bus->bus_number != tag->ti_Data) match = FALSE;
                break;
            case PRM_SlotNumber:
                if ((ULONG)(cur->devfn >> 3) != tag->ti_Data) match = FALSE;
                break;
            case PRM_FunctionNumber:
                if ((ULONG)(cur->devfn & 7u) != tag->ti_Data) match = FALSE;
                break;
            case PRM_SubsysVendor:
                if (idev->subsys_vendor != (UWORD)tag->ti_Data) match = FALSE;
                break;
            case PRM_SubsysID:
                if (idev->subsys_id != (UWORD)tag->ti_Data) match = FALSE;
                break;
            /*
             * PRM_MemRangeLower / PRM_MemRangeUpper: find devices with a BAR
             * that overlaps the given address range.
             */
            case PRM_MemRangeLower:
            case PRM_MemRangeUpper:
                /* Memory-range search not supported; never match. */
                match = FALSE;
                break;

            default:
                /* Unknown/non-filter tags are ignored. */
                break;
            }

            if (!match) break;
        }

        if (match) { result = cur; break; }
        cur = cur->next;
    }
    ReleaseSemaphore(&base->semaphore);
    return result;
}

/* -----------------------------------------------------------------------
 * PCIe capability lookup
 * ----------------------------------------------------------------------- */

/*
 * LibFindCapability — returns the config-space offset of the requested
 * standard PCI capability, or 0 if not present.
 */
ULONG LibFindCapability(struct pci_dev *dev asm("a0"), UBYTE cap asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev) return 0;
    return (ULONG)pci_find_capability(pcie_dev_from_openpci(dev), (u8)cap);
}

/*
 * LibFindExtCapability — returns the config-space offset of the requested
 * PCIe extended capability, or 0 if not present.
 */
ULONG LibFindExtCapability(struct pci_dev *dev asm("a0"), UWORD cap asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev) return 0;
    return (ULONG)pci_find_ext_capability(pcie_dev_from_openpci(dev), (u16)cap);
}
