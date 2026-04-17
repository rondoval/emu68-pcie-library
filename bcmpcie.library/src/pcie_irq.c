// SPDX-License-Identifier: GPL-2.0-only

/*
 * pcie_irq.c — interrupt registration for pcie.library
 *
 * pci_add_intserver / pci_rem_intserver handle both MSI and traditional INTx:
 *
 *   MSI path (pci_device.msi.enabled == TRUE):
 *     Delegates to add_int_server() from the static pcie library, which assigns
 *     a free slot in pci_controller.msi.vectors[], programs the device MSI
 *     capability (msi_capability_init + pci_write_msg_msi), and inserts the ISR
 *     into the BCM2711 PCIe MSI dispatch table (registered at LibOpen via
 *     brcm_pcie_enable_msi at GIC IRQ 180).
 *
 *   INTx path (msi.enabled == FALSE):
 *     Traditional PCI interrupt line; delegates to AddIntServerEx via gic400.library
 *     using dev->irq + 32 (BCM2711 GIC SPI base offset).
 *
 * This file is separate from pcie_main.c to isolate the gic400 proto include:
 * gic400's generated headers define LibOpen/LibClose/LibExpunge/LibNull stubs
 * which would collide with the identically-named static functions in pcie_main.c.
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/gic400_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>

#define GIC400_BASE_NAME (base->gic400Base)
#include <proto/gic400.h>
#endif

#include <exec/interrupts.h>
#include <exec/types.h>
#include <pcie_private.h>
#include <debug.h>
#include <pci_util.h>
#include <pci_int.h>
#include <pci_msi.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/*
 * Install an interrupt server for a PCI device.
 *
 * MSI: if the device has MSI enabled (PCIe_EnableMSI was called), delegates to
 * add_int_server() which registers the ISR in pci_controller.msi.vectors[]
 * and programs the device MSI capability so the BCM2711 PCIe MSI dispatcher
 * routes the interrupt correctly.
 *
 * INTx: if MSI is not enabled, registers the ISR via AddIntServerEx using the
 * device's GIC interrupt number (dev->irq + 32).
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL LibAddIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
    if (!isr || !dev || dev->irq == 0)
        return FALSE;

    struct pci_device *idev = pcie_dev_from_openpci(dev);
    if (!idev)
        return FALSE;

    if (idev->prefer_msi)
    {
        if (idev->msi.cap_offset == 0) // device doesn't have MSI capability
        {
            Kprintf("[pcie] %s: device does not have MSI capability\n", __func__);
            return FALSE;
        }
        if (pci_get_controller(idev->bus)->msi.enabled == FALSE) // controller doesn't have MSI enabled
        {
            Kprintf("[pcie] %s: controller does not have MSI enabled\n", __func__);
            return FALSE;
        }

        /* MSI path: register in the PCIe MSI dispatch table.
         * Serialise with other Add/Rem calls: add_int_server does a
         * scan-then-write on pcie->msi.vectors[], which is not atomic. */
        ObtainSemaphore(&base->semaphore);
        s32 ret = add_int_server(idev, isr);
        ReleaseSemaphore(&base->semaphore);
        if (ret < 0)
            Kprintf("[pcie] %s: add_int_server failed (%ld)\n", __func__, ret);
        return (BOOL)(ret >= 0);
    }
    else
    {
        /* INTx path: traditional PCI interrupt line via GIC */
        if (!base->gic400Base)
            return FALSE;
        LONG ret = AddIntServerEx((ULONG)idev->irq_line + 32, 0, FALSE, isr);
        if (ret != 0)
        {
            Kprintf("[pcie] %s: AddIntServerEx(irq=%ld) failed (%ld)\n",
                    __func__, (LONG)idev->irq_line + 32, ret);
        }
        else
        {
            pci_intx(idev, TRUE);                     /* unmask the device's INTx line so interrupts can flow */
            pci_check_and_set_intx_mask(idev, FALSE); /* clear any stale INTx mask so interrupts can flow */
        }
        return (BOOL)(ret == 0);
    }
}

/*
 * Remove an interrupt server previously installed with pci_add_intserver.
 *
 * Dispatches to rem_int_server() for MSI, or RemIntServerEx() for INTx,
 * matching the path taken by LibAddIntServer.
 */
void LibRemIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
    if (!isr || !dev)
        return;

    struct pci_device *idev = pcie_dev_from_openpci(dev);
    if (!idev)
        return;

    if (idev->msi.enabled)
    {
        /* Serialise with LibAddIntServer: protects vectors[] and num_vectors */
        ObtainSemaphore(&base->semaphore);
        rem_int_server(idev);
        ReleaseSemaphore(&base->semaphore);
    }
    else
    {
        if (!base->gic400Base)
            return;
        if (!pci_check_and_set_intx_mask(idev, TRUE)) // mask the device's INTx line to prevent interrupts while we're removing the handler
        {
            Kprintf("[pcie] %s: failed to mask INTx line for device %04x:%04x\n", __func__, idev->vendor, idev->device);
        }
        RemIntServerEx((ULONG)idev->irq_line + 32, isr);
    }
}

/* -----------------------------------------------------------------------
 * PCIe MSI setup / teardown and FLR (no gic400 dependency)
 * ----------------------------------------------------------------------- */

/*
 * LibEnableMSI — initialise MSI for up to nvec vectors.
 * Returns 0 on success, negative on failure.
 */
LONG LibEnableMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev)
        return -1;
    struct pci_device *idev = pcie_dev_from_openpci(dev);
    if (idev->msi.cap_offset == 0)
        return -2; // device doesn't have MSI capability
    if (pci_get_controller(idev->bus)->msi.enabled == FALSE)
        return -3; // controller doesn't have MSI enabled

    idev->prefer_msi = TRUE; /* hint to prefer MSI if available */
    return 0;
}

/*
 * LibDisableMSI — disable MSI for the device, reverting to INTx if available.
 * Safe to call even if MSI was never enabled or the device doesn't support MSI.
 */
void LibDisableMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev)
        return;
    pcie_dev_from_openpci(dev)->prefer_msi = FALSE; /* hint to prefer INTx if available */
}

/* -----------------------------------------------------------------------
 * ISR-safe interrupt control — no semaphore, safe from interrupt context.
 * ----------------------------------------------------------------------- */

void LibMaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev)
        return;
    struct pci_device *idev = pcie_dev_from_openpci(dev);
    pci_msi_mask_irq(idev, idev->msi.vector);
}

void LibUnmaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev)
        return;
    struct pci_device *idev = pcie_dev_from_openpci(dev);
    pci_msi_unmask_irq(idev, idev->msi.vector);
}

BOOL LibCheckSetINTxMask(struct pci_dev *dev asm("a0"), BOOL mask asm("d0"), struct PCIELibBase *base asm("a6"))
{
    (void)base;
    if (!dev)
        return FALSE;
    return pci_check_and_set_intx_mask(pcie_dev_from_openpci(dev), mask);
}
