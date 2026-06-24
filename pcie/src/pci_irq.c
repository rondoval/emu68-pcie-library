// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic, controller-agnostic interrupt-vector management.  See pci_irq.h.
 *
 * Holds the demux-slot pool (a bitmap of opaque tokens) and the type-agnostic
 * dispatch over dev->active.mode.  Per-type reservation and capability
 * programming live in pcie_msi.c / pcie_msix.c; the message encoding and the
 * per-slot ISR table live in the controller back-end (pcie_brcmstb_msi.c).
 */

#include <errors.h>

#include <pci.h>
#include <pci_irq.h>
#include <pci_int.h>
#include <pci_msi.h>
#include <pci_msix.h>
#include <pci_util.h>
#include <pcie_brcmstb.h>

/* ---- demux-slot pool (pcie->msi.used bitmap; slots are opaque here) ---- */

u32 pci_irq_slots_free_count(const struct pci_controller *pcie)
{
	u32 n = 0;
	for (u32 i = 0; i < MSI_MAX_VECTORS; i++)
		if (!(pcie->msi.used & (1u << i)))
			n++;
	return n;
}

s32 pci_irq_slots_alloc_any(struct pci_controller *pcie, u32 n, s32 *out)
{
	if (pci_irq_slots_free_count(pcie) < n)
		return -1;

	u32 k = 0;
	for (s32 i = 0; i < MSI_MAX_VECTORS && k < n; i++)
	{
		if (!(pcie->msi.used & (1u << i)))
		{
			pcie->msi.used |= (1u << i);
			out[k++] = (s32)i;
		}
	}
	return 0;
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
	{
		s32 s = slots[i];
		if (s >= 0 && s < MSI_MAX_VECTORS)
			pcie->msi.used &= ~(1u << (u32)s);
	}
}

/* ---- type-agnostic dispatch over dev->active.mode ---- */

void pci_irq_free(struct pci_device *dev)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (dev->active.mode == PCI_IRQT_INTX)
	{
		/* INTx holds no demux slot or device capability to release. */
		dev->active.mode = PCI_IRQT_NONE;
		dev->active.nvec = 0;
		return;
	}
	if (dev->active.mode == PCI_IRQT_MSIX)
		pci_msix_shutdown(dev);
	else if (dev->active.mode == PCI_IRQT_MSI)
		pci_msi_shutdown(dev);
	else
		return;

	if (pcie)
	{
		for (u32 v = 0; v < dev->active.nvec; v++)
			brcm_msi_unbind(pcie, dev->active.slots[v]);
		pci_irq_slots_free(pcie, dev->active.slots, dev->active.nvec);
	}
	dev->active.mode = PCI_IRQT_NONE;
	dev->active.nvec = 0;
}

BOOL pci_irq_mask(struct pci_device *dev, u32 vec)
{
	if (vec >= dev->active.nvec)
		return FALSE;
	if (dev->active.mode == PCI_IRQT_INTX)
		return pci_check_and_set_intx_mask(dev, TRUE);
	if (dev->active.mode == PCI_IRQT_MSIX)
	{
		pci_msix_mask_irq(dev, (int)vec); /* MSI-X per-vector mask is mandatory */
		return TRUE;
	}
	if (dev->active.mode == PCI_IRQT_MSI)
	{
		/* MSI per-vector masking is optional; without it there is no mask bit. */
		if (!dev->msi.maskable)
			return FALSE;
		pci_msi_mask_irq(dev, (int)vec);
		return TRUE;
	}
	return FALSE;
}

BOOL pci_irq_unmask(struct pci_device *dev, u32 vec)
{
	if (vec >= dev->active.nvec)
		return FALSE;
	if (dev->active.mode == PCI_IRQT_INTX)
		return pci_check_and_set_intx_mask(dev, FALSE);
	if (dev->active.mode == PCI_IRQT_MSIX)
	{
		pci_msix_unmask_irq(dev, (int)vec); /* MSI-X per-vector mask is mandatory */
		return TRUE;
	}
	if (dev->active.mode == PCI_IRQT_MSI)
	{
		/* MSI per-vector masking is optional; without it there is no mask bit. */
		if (!dev->msi.maskable)
			return FALSE;
		pci_msi_unmask_irq(dev, (int)vec);
		return TRUE;
	}
	return FALSE;
}

s32 pci_irq_install(struct pci_device *dev, u32 vec, struct Interrupt *isr)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (!pcie || vec >= dev->active.nvec)
		return -EINVAL;

	if (dev->active.mode == PCI_IRQT_INTX)
	{
		s32 r = brcm_intx_bind(pcie, dev, isr); /* register ISR with gic400 */
		if (r < 0)
			return r;
		pci_intx(dev, TRUE);	  /* enable INTx assertion at the device */
		pci_irq_unmask(dev, vec); /* clear any stale INTx mask */
		return 0;
	}

	s32 slot = dev->active.slots[vec];
	if (slot < 0 || slot >= MSI_MAX_VECTORS)
		return -EINVAL;
	brcm_msi_bind(pcie, slot, isr); /* controller owns the demux ISR table */
	pci_irq_unmask(dev, vec);
	return 0;
}

void pci_irq_uninstall(struct pci_device *dev, u32 vec, struct Interrupt *isr)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (!pcie || vec >= dev->active.nvec)
		return;
	pci_irq_mask(dev, vec);
	if (dev->active.mode == PCI_IRQT_INTX)
		brcm_intx_unbind(pcie, dev, isr);
	else
		brcm_msi_unbind(pcie, dev->active.slots[vec]);
}
