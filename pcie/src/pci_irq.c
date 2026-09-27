// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic, controller-agnostic interrupt-vector management.  See pci_irq.h.
 *
 * Holds the demux-slot pool (a bitmap of opaque tokens), the choice of
 * interrupt type, and the type-agnostic dispatch over dev->active.mode.
 * Per-type reservation and capability programming live in pcie_msi.c /
 * pcie_msix.c / pci_int.c; the message encoding and the per-slot server table
 * live in the controller back-end (pcie_brcmstb_msi.c).
 */

#define __NOLIBBASE__
#define EXEC_BASE_NAME SysBase /* a local in every function, from its context's sysBase */
#include <bits.h>
#include <errors.h>

#include <pci.h>
#include <pci_irq.h>
#include <pci_util.h>
#include <pcie_brcmstb.h>

/* ---- demux-slot pool (pcie->msi.used bitmap; slots are opaque here) ---- */

u32 pci_irq_slots_alloc_any(struct pci_controller *pcie, u32 n, s32 *out)
{
	u32 k = 0;
	for (s32 i = 0; i < MSI_MAX_VECTORS && k < n; i++)
	{
		if (!(pcie->msi.used & (1u << i)))
		{
			pcie->msi.used |= (1u << i);
			out[k++] = i;
		}
	}
	return k;
}

s32 pci_irq_slots_alloc_aligned(struct pci_controller *pcie, u32 n)
{
	for (u32 base = 0; base + n <= MSI_MAX_VECTORS; base += n)
	{
		u32 mask = (n >= 32) ? 0xffffffffu : (((1u << n) - 1u) << base);
		if ((pcie->msi.used & mask) == 0)
		{
			pcie->msi.used |= mask;
			return (s32)base;
		}
	}
	return -1;
}

void pci_irq_slots_free(struct pci_controller *pcie, const s32 *slots, u32 n)
{
	for (u32 i = 0; i < n; i++)
		pcie->msi.used &= ~(1u << (u32)slots[i]);
}

/* ---- allocation ---- */

s32 pci_irq_vectors_alloc(struct pci_device *dev, u32 min, u32 max, u32 flags)
{
	if (dev->active.mode)
		return -EBUSY;
	if (min < 1)
		min = 1;
	if (max > MSI_MAX_VECTORS)
		max = MSI_MAX_VECTORS;
	if (max < min)
		return -ERANGE;

	/* Best type first; the next one is tried when the one before it is not
	 * allowed or fails, and the last failure is the one reported. */
	u32 mode;
	s32 n = -ENODEV;
	if ((flags & PCI_IRQ_MSIX) && (n = pci_msix_alloc(dev, min, max)) > 0)
		mode = PCI_IRQ_MSIX;
	else if ((flags & PCI_IRQ_MSI) && (n = pci_msi_alloc(dev, min, max)) > 0)
		mode = PCI_IRQ_MSI;
	else if ((flags & PCI_IRQ_INTX) && (n = pci_intx_alloc(dev, min, max)) > 0)
		mode = PCI_IRQ_INTX;
	else
		return n;

	dev->active.mode = mode;
	dev->active.nvec = (u16)n;
	return n;
}

/* ---- servers ---- */

/* The server attached to @vec, or NULL. */
static struct Interrupt *pci_irq_vec_server(const struct pci_device *dev, u32 vec)
{
	if (dev->active.mode == PCI_IRQ_INTX)
		return dev->active.intx_server;
	return brcm_msi_slot_server(pci_get_controller(dev->bus), dev->active.slots[vec]);
}

/*
 * Device-level vector gate: the MSI-X table entry or the MSI per-vector mask
 * bit (a PCIe write).  Opened when a server is added, closed when it is
 * removed; the runtime mask (pci_irq_vec_mask/unmask) works at the root complex.
 * MSI per-vector masking is optional: without mask bits the MSI gate does nothing.
 */
static void pci_irq_vec_close(struct pci_device *dev, u32 vec)
{
	if (dev->active.mode == PCI_IRQ_MSIX)
		pci_msix_entry_mask(dev, vec);
	else
		pci_msi_update_mask(dev, 0, BIT(vec));
}

static void pci_irq_vec_open(struct pci_device *dev, u32 vec)
{
	if (dev->active.mode == PCI_IRQ_MSIX)
		pci_msix_entry_unmask(dev, vec);
	else
		pci_msi_update_mask(dev, BIT(vec), 0);
}

s32 pci_irq_add_server(struct pci_device *dev, u32 vec, struct Interrupt *isr)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (vec >= dev->active.nvec)
		return -EINVAL;
	if (pci_irq_vec_server(dev, vec))
		return -EBUSY;

	if (dev->active.mode == PCI_IRQ_INTX)
	{
		s32 r = brcm_intx_add_server(pcie, dev, isr);
		if (r < 0)
			return r;
		dev->active.intx_server = isr;
		pci_intx(dev, TRUE); /* let the device assert the line */
		return 0;
	}

	brcm_msi_slot_bind(pcie, dev->active.slots[vec], isr);
	pci_irq_vec_open(dev, vec); /* the device gate last: its slot is ready */
	return 0;
}

void pci_irq_rem_server(struct pci_device *dev, u32 vec, struct Interrupt *isr)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (vec >= dev->active.nvec || pci_irq_vec_server(dev, vec) != isr)
		return;

	if (dev->active.mode == PCI_IRQ_INTX)
	{
		pci_intx(dev, FALSE); /* quiet the device before its server goes */
		brcm_intx_rem_server(pcie, dev, isr);
		dev->active.intx_server = NULL;
	}
	else
	{
		pci_irq_vec_close(dev, vec); /* the device gate first */
		brcm_msi_slot_unbind(pcie, dev->active.slots[vec]);
	}
}

/* ---- teardown ---- */

void pci_irq_vectors_free(struct pci_device *dev)
{
	/* Detach what is still attached, so there is one teardown path */
	for (u32 v = 0; v < dev->active.nvec; v++)
	{
		struct Interrupt *server = pci_irq_vec_server(dev, v);
		if (server)
			pci_irq_rem_server(dev, v, server);
	}

	if (dev->active.mode == PCI_IRQ_MSIX)
		pci_msix_shutdown(dev);
	else if (dev->active.mode == PCI_IRQ_MSI)
		pci_msi_shutdown(dev);

	if (dev->active.mode != PCI_IRQ_INTX)
		pci_irq_slots_free(pci_get_controller(dev->bus), dev->active.slots, dev->active.nvec);

	dev->active.mode = 0;
	dev->active.nvec = 0;
}

/* ---- runtime mask ---- */

/*
 * MaskIntVector/UnmaskIntVector.  MSI and MSI-X: the root-complex demux slot,
 * a local register write rather than a PCIe transaction, ISR-safe; a message
 * that arrives while masked latches and fires on unmask.  Also covers MSI
 * functions without per-vector mask bits.
 * INTx: the command-register INTX_DISABLE bit, unconditionally.  It gates this
 * device only, so on a line shared with another function it quiets our own
 * contribution and leaves the rest of the line alone - there is no pending
 * state to guard.  A config-space access, hence task context only.
 */
BOOL pci_irq_vec_mask(struct pci_device *dev, u32 vec)
{
	if (vec >= dev->active.nvec)
		return FALSE;

	if (dev->active.mode == PCI_IRQ_INTX)
		pci_intx(dev, FALSE);
	else
		brcm_msi_slot_mask(pci_get_controller(dev->bus), dev->active.slots[vec]);
	return TRUE;
}

BOOL pci_irq_vec_unmask(struct pci_device *dev, u32 vec)
{
	if (vec >= dev->active.nvec)
		return FALSE;

	if (dev->active.mode == PCI_IRQ_INTX)
		pci_intx(dev, TRUE);
	else
		brcm_msi_slot_unmask(pci_get_controller(dev->bus), dev->active.slots[vec]);
	return TRUE;
}
