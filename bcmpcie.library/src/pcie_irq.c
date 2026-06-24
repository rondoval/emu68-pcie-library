// SPDX-License-Identifier: GPL-2.0-only

/*
 * pcie_irq.c — interrupt API for bcmpcie.library
 *
 * The policy / public-ABI layer, and nothing more: it interprets the public
 * PCI_IRQ_* flags, applies the MSI-X -> MSI -> INTx preference order, takes the
 * library semaphore, and maps the core's errno results to PCIE_ERR_* codes.
 * All interrupt *mechanism* — for every type, INTx included — lives in the
 * controller-agnostic core (pci_irq.c + pcie_msi.c / pcie_msix.c / pci_int.c)
 * and the controller back-end (pcie_brcmstb_msi.c).  This file is the only place
 * that interprets the public flags or maps the internal pci_irq_type.
 *
 *   New typed/multi-vector API (preferred):
 *     AllocIntVectors / FreeIntVectors / AddIntVectorServer / RemIntVectorServer
 *     MaskIntVector / UnmaskIntVector / GetIntVectorType
 *
 *   Obsolete compat API (single-vector MSI or INTx only — never MSI-X):
 *     EnableMSI / DisableMSI / pci_add_intserver / pci_rem_intserver
 *     MaskMSI / UnmaskMSI / CheckSetINTxMask
 *
 * Alloc/free/add/rem take the library semaphore; Mask/Unmask are register-only
 * and ISR-safe.  Kept separate from pcie_main.c (library lifecycle) purely as
 * the interrupt-API translation unit.
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <exec/interrupts.h>
#include <exec/types.h>
#include <pcie_private.h>
#include <debug.h>
#include <libraries/pci_constants.h>
#include <pci_util.h>
#include <pci_int.h>
#include <pci_irq.h>
#include <pci_msi.h>
#include <pci_msix.h>
#include <errors.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

/* ---------------------------------------------------------------------------
 * Policy helpers.  irq_alloc assumes the caller holds base->semaphore.
 * ------------------------------------------------------------------------- */

/* Map a core -errno result onto the public PCIE_ERR_* code. */
static LONG pcie_err(s32 e)
{
	switch (e)
	{
	case -EINVAL:
	case -ERANGE:
		return PCIE_ERR_INVAL;
	case -EBUSY:
		return PCIE_ERR_BUSY;
	case -ENOTSUPP:
		return PCIE_ERR_NOTSUPP;
	case -ENOMEM:
	case -ENOSPC:
		return PCIE_ERR_NOMEM;
	case -EIO:
		return PCIE_ERR_IO;
	case -ENODEV:
	default:
		return PCIE_ERR_NODEV;
	}
}

/* Reserve [min,max] vectors of the best allowed type into idev->active by
 * trying the per-type core allocators in MSI-X -> MSI -> INTx preference order,
 * each gated by its public flag bit.  Returns the count, or a negative
 * PCIE_ERR_* code (the reason the last attempted type failed). */
static s32 irq_alloc(struct pci_device *idev, u32 min, u32 max, u32 flags)
{
	s32 err = PCIE_ERR_NODEV; /* reason no requested type could be satisfied */

	if (min < 1)
		min = 1;

	if (flags & PCI_IRQ_MSIX)
	{
		s32 n = pci_msix_alloc(idev, min, max);
		if (n >= (s32)min)
			return n;
		err = pcie_err(n);
	}
	if (flags & PCI_IRQ_MSI)
	{
		s32 n = pci_msi_alloc(idev, min, max);
		if (n >= (s32)min)
			return n;
		err = pcie_err(n);
	}
	if (flags & PCI_IRQ_INTX)
	{
		s32 n = pci_intx_alloc(idev, min, max);
		if (n >= (s32)min)
			return n;
		err = pcie_err(n);
	}

	return err;
}

/* ===========================================================================
 * New typed / multi-vector API
 * ========================================================================= */

LONG LibAllocIntVectors(struct pci_dev *dev asm("a0"), ULONG min asm("d0"),
						ULONG max asm("d1"), ULONG flags asm("d2"),
						struct PCIELibBase *base asm("a6"))
{
	if (!dev)
		return PCIE_ERR_INVAL;
	struct pci_device *idev = pcie_dev_from_openpci(dev);
	if (idev->active.mode != PCI_IRQT_NONE)
	{
		Kprintf("[pcie] %s: device %04lx:%04lx already has vectors\n", __func__,
				(ULONG)idev->vendor, (ULONG)idev->device);
		return PCIE_ERR_BUSY;
	}

	ObtainSemaphore(&base->semaphore);
	s32 n = irq_alloc(idev, (u32)min, (u32)max, (u32)flags);
	ReleaseSemaphore(&base->semaphore);
	return (LONG)n;
}

void LibFreeIntVectors(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	if (!dev)
		return;
	struct pci_device *idev = pcie_dev_from_openpci(dev);

	ObtainSemaphore(&base->semaphore);
	pci_irq_free(idev);
	ReleaseSemaphore(&base->semaphore);
}

LONG LibAddIntVectorServer(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
						   struct Interrupt *isr asm("a1"), struct PCIELibBase *base asm("a6"))
{
	if (!dev || !isr)
		return PCIE_ERR_INVAL;
	struct pci_device *idev = pcie_dev_from_openpci(dev);

	ObtainSemaphore(&base->semaphore);
	s32 r = pci_irq_install(idev, (u32)vec, isr);
	ReleaseSemaphore(&base->semaphore);
	return r < 0 ? pcie_err(r) : PCIE_OK;
}

void LibRemIntVectorServer(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
						   struct Interrupt *isr asm("a1"), struct PCIELibBase *base asm("a6"))
{
	if (!dev || !isr)
		return;
	struct pci_device *idev = pcie_dev_from_openpci(dev);

	ObtainSemaphore(&base->semaphore);
	pci_irq_uninstall(idev, (u32)vec, isr);
	ReleaseSemaphore(&base->semaphore);
}

BOOL LibMaskIntVector(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
					  struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return FALSE;
	return pci_irq_mask(pcie_dev_from_openpci(dev), (u32)vec);
}

BOOL LibUnmaskIntVector(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
						struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return FALSE;
	return pci_irq_unmask(pcie_dev_from_openpci(dev), (u32)vec);
}

ULONG LibGetIntVectorType(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return 0;
	/* Map the internal tag back to the public PCI_IRQ_* value callers expect. */
	switch (pcie_dev_from_openpci(dev)->active.mode)
	{
	case PCI_IRQT_INTX:
		return PCI_IRQ_INTX;
	case PCI_IRQT_MSI:
		return PCI_IRQ_MSI;
	case PCI_IRQT_MSIX:
		return PCI_IRQ_MSIX;
	default:
		return 0;
	}
}

/* ===========================================================================
 * Obsolete compat API — single-vector MSI or INTx only, never MSI-X.
 * Reimplemented on the shared core; behaviour preserved.
 * ========================================================================= */

/*
 * OBSOLETE: use AllocIntVectors + AddIntVectorServer.
 * Install an interrupt server: MSI if EnableMSI() was called and succeeded,
 * otherwise INTx.  MSI-X is never selected by this path.
 */
BOOL LibAddIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
	if (!isr || !dev || dev->irq == 0)
		return FALSE;

	struct pci_device *idev = pcie_dev_from_openpci(dev);
	u32 flags = idev->intx.prefer_msi ? (PCI_IRQ_MSI | PCI_IRQ_INTX) : PCI_IRQ_INTX;
	BOOL ok = FALSE;

	ObtainSemaphore(&base->semaphore);
	if (irq_alloc(idev, 1, 1, flags) >= 1)
	{
		if (pci_irq_install(idev, 0, isr) == 0)
			ok = TRUE;
		else
			pci_irq_free(idev); /* roll back the allocation */
	}
	ReleaseSemaphore(&base->semaphore);

	if (!ok)
		Kprintf("[pcie] %s: failed for device %04lx:%04lx\n", __func__,
				(ULONG)idev->vendor, (ULONG)idev->device);
	return ok;
}

/*
 * OBSOLETE: use RemIntVectorServer + FreeIntVectors.
 */
void LibRemIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
	if (!isr || !dev)
		return;

	struct pci_device *idev = pcie_dev_from_openpci(dev);
	if (idev->active.mode == PCI_IRQT_NONE)
		return;

	ObtainSemaphore(&base->semaphore);
	pci_irq_uninstall(idev, 0, isr);
	pci_irq_free(idev);
	ReleaseSemaphore(&base->semaphore);
}

/*
 * OBSOLETE: pass PCI_IRQ_MSI to AllocIntVectors instead.
 * Hint that the obsolete pci_add_intserver() path should prefer MSI (never
 * MSI-X) over INTx.  Returns PCIE_OK on success.
 */
LONG LibEnableMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return PCIE_ERR_INVAL;
	struct pci_device *idev = pcie_dev_from_openpci(dev);
	if (idev->msi.cap_offset == 0)
		return PCIE_ERR_NODEV; /* no MSI capability */
	if (pci_get_controller(idev->bus)->msi.enabled == FALSE)
		return PCIE_ERR_NOTSUPP; /* controller MSI demux not enabled */

	idev->intx.prefer_msi = TRUE;
	return PCIE_OK;
}

/* OBSOLETE: pass PCI_IRQ_INTX-only to AllocIntVectors instead. */
void LibDisableMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return;
	pcie_dev_from_openpci(dev)->intx.prefer_msi = FALSE;
}

/* OBSOLETE: use MaskIntVector(dev, 0). */
void LibMaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return;
	pci_irq_mask(pcie_dev_from_openpci(dev), 0);
}

/* OBSOLETE: use UnmaskIntVector(dev, 0). */
void LibUnmaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return;
	pci_irq_unmask(pcie_dev_from_openpci(dev), 0);
}

BOOL LibCheckSetINTxMask(struct pci_dev *dev asm("a0"), BOOL mask asm("d0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return FALSE;
	return pci_check_and_set_intx_mask(pcie_dev_from_openpci(dev), mask);
}
