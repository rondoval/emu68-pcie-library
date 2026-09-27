// SPDX-License-Identifier: GPL-2.0-only

/*
 * pcie_irq.c — interrupt API for bcmpcie.library
 *
 * The public-ABI layer, and nothing more: it checks the LVO arguments, takes
 * the library semaphore and maps the core's errno results to PCIE_ERR_* codes.
 * All interrupt mechanism and policy — the choice of interrupt type included —
 * lives in the controller-agnostic core (pci_irq.c + pcie_msi.c / pcie_msix.c /
 * pci_int.c) and the controller back-end (pcie_brcmstb_msi.c).
 *
 *   Typed/multi-vector API (preferred):
 *     AllocIntVectors / FreeIntVectors / AddIntVectorServer / RemIntVectorServer
 *     MaskIntVector / UnmaskIntVector / GetIntVectorType
 *
 *   Obsolete compat API (pci_add_intserver picks MSI or INTx, never MSI-X):
 *     EnableMSI / DisableMSI / pci_add_intserver / pci_rem_intserver
 *     MaskMSI / UnmaskMSI / CheckSetINTxMask
 *
 * Alloc/free/add/rem take the library semaphore.  Mask/Unmask do not: they are
 * register-only and ISR-safe for MSI and MSI-X; for INTx they are a
 * config-space access, task context only.
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME SysBase /* a local in every function, from its context's sysBase */
#include <proto/exec.h>
#endif

#include <exec/interrupts.h>
#include <exec/types.h>
#include <pcie_private.h>
#include <debug.h>
#include <libraries/pci_constants.h>
#include <pci_util.h>
#include <pci_irq.h>
#include <errors.h>

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

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
	case -ENOSPC:
		return PCIE_ERR_NOMEM;
	case -EIO:
		return PCIE_ERR_IO;
	case -ENODEV:
	default:
		return PCIE_ERR_NODEV;
	}
}

/* ===========================================================================
 * Typed / multi-vector API
 * ========================================================================= */

LONG LibAllocIntVectors(struct pci_dev *dev asm("a0"), ULONG min asm("d0"),
						ULONG max asm("d1"), ULONG flags asm("d2"),
						struct PCIELibBase *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	if (!dev)
		return PCIE_ERR_INVAL;

	ObtainSemaphore(&base->semaphore);
	s32 n = pci_irq_vectors_alloc(pcie_dev_from_openpci(dev), (u32)min, (u32)max, (u32)flags);
	ReleaseSemaphore(&base->semaphore);
	return n < 0 ? pcie_err(n) : (LONG)n;
}

void LibFreeIntVectors(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	if (!dev)
		return;

	ObtainSemaphore(&base->semaphore);
	pci_irq_vectors_free(pcie_dev_from_openpci(dev));
	ReleaseSemaphore(&base->semaphore);
}

LONG LibAddIntVectorServer(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
						   struct Interrupt *isr asm("a1"), struct PCIELibBase *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	if (!dev || !isr)
		return PCIE_ERR_INVAL;

	ObtainSemaphore(&base->semaphore);
	s32 r = pci_irq_add_server(pcie_dev_from_openpci(dev), (u32)vec, isr);
	ReleaseSemaphore(&base->semaphore);
	return r < 0 ? pcie_err(r) : PCIE_OK;
}

void LibRemIntVectorServer(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
						   struct Interrupt *isr asm("a1"), struct PCIELibBase *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	if (!dev || !isr)
		return;

	ObtainSemaphore(&base->semaphore);
	pci_irq_rem_server(pcie_dev_from_openpci(dev), (u32)vec, isr);
	ReleaseSemaphore(&base->semaphore);
}

BOOL LibMaskIntVector(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
					  struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return FALSE;
	return pci_irq_vec_mask(pcie_dev_from_openpci(dev), (u32)vec);
}

BOOL LibUnmaskIntVector(struct pci_dev *dev asm("a0"), ULONG vec asm("d0"),
						struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return FALSE;
	return pci_irq_vec_unmask(pcie_dev_from_openpci(dev), (u32)vec);
}

ULONG LibGetIntVectorType(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return 0;
	return pcie_dev_from_openpci(dev)->active.mode; /* a PCI_IRQ_* value, 0 if none */
}

/* ===========================================================================
 * Obsolete compat API, on the same core.
 * ========================================================================= */

/*
 * OBSOLETE: use AllocIntVectors + AddIntVectorServer.
 * Install an interrupt server: MSI if EnableMSI() was called and succeeded,
 * otherwise INTx.  MSI-X is never selected by this path.
 */
BOOL LibAddIntServer(struct Interrupt *isr asm("a0"), struct pci_dev *dev asm("a1"), struct PCIELibBase *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	if (!isr || !dev)
		return FALSE;

	struct pci_device *idev = pcie_dev_from_openpci(dev);
	u32 flags = idev->intx.prefer_msi ? (PCI_IRQ_MSI | PCI_IRQ_INTX) : PCI_IRQ_INTX;
	BOOL ok = FALSE;

	ObtainSemaphore(&base->semaphore);
	if (pci_irq_vectors_alloc(idev, 1, 1, flags) == 1)
	{
		if (pci_irq_add_server(idev, 0, isr) == 0)
			ok = TRUE;
		else
			pci_irq_vectors_free(idev); /* roll back the allocation */
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
	struct ExecBase *SysBase = base->sysBase;
	if (!isr || !dev)
		return;

	struct pci_device *idev = pcie_dev_from_openpci(dev);

	ObtainSemaphore(&base->semaphore);
	pci_irq_rem_server(idev, 0, isr);
	pci_irq_vectors_free(idev);
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
	pci_irq_vec_mask(pcie_dev_from_openpci(dev), 0);
}

/* OBSOLETE: use UnmaskIntVector(dev, 0). */
void LibUnmaskMSI(struct pci_dev *dev asm("a0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return;
	pci_irq_vec_unmask(pcie_dev_from_openpci(dev), 0);
}

/* OBSOLETE: use MaskIntVector/UnmaskIntVector(dev, 0).  Unconditional since
 * 2.5 (INTx lines are exclusive); task context only. */
BOOL LibCheckSetINTxMask(struct pci_dev *dev asm("a0"), BOOL mask asm("d0"), struct PCIELibBase *base asm("a6"))
{
	(void)base;
	if (!dev)
		return FALSE;
	pci_intx(pcie_dev_from_openpci(dev), !mask);
	return TRUE;
}
