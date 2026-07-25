// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI MSI-X support.
 *
 * Mirrors pcie_msi.c, but the per-vector message lives in a BAR-mapped table
 * rather than config space.  Both MSI and MSI-X deliver the same memory write
 * to the controller doorbell; the message (address/data) is supplied by the
 * controller back-end via brcm_pcie_compose_msi_msg() and the demux ISR lives
 * in pcie_brcmstb_msi.c.  Only the device-side table programming differs and
 * lives here — this file holds no controller specifics.
 */

#include <debug.h>
#include <bits.h>
#include <errors.h>

#include <pci.h>
#include <iomem.h>
#include <pci_irq.h>
#include <pci_msix.h>
#include <pci_capability.h>
#include <pci_io.h>
#include <pci_util.h>
#include <pci_int.h>
#include <pcie_brcmstb.h>

/* Byte pointer to MSI-X table entry @idx. */
static inline volatile u8 *msix_entry(struct pci_device *dev, u32 idx)
{
	return (volatile u8 *)dev->msix.table_virt + (idx * PCI_MSIX_ENTRY_SIZE);
}

static void pci_msix_set_ctrl(struct pci_device *dev, u16 clear, u16 set)
{
	u16 control;

	pci_read_config16(dev, dev->msix.cap_offset + PCI_MSIX_FLAGS, &control);
	control = (u16)((control & ~clear) | set);
	pci_write_config16(dev, dev->msix.cap_offset + PCI_MSIX_FLAGS, control);
}

void pci_msix_mask_irq(struct pci_device *dev, int idx)
{
	if (!dev || !dev->msix.table_virt)
		return;
	KprintfT("[pcie] %s: device %04lx:%04lx mask MSI-X entry %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, (LONG)idx);
	mmio_write32(PCI_MSIX_ENTRY_CTRL_MASKBIT,
				 msix_entry(dev, (u32)idx) + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

void pci_msix_unmask_irq(struct pci_device *dev, int idx)
{
	if (!dev || !dev->msix.table_virt)
		return;
	KprintfT("[pcie] %s: device %04lx:%04lx unmask MSI-X entry %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, (LONG)idx);
	mmio_write32(0u, msix_entry(dev, (u32)idx) + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

/*
 * pci_msix_capability_init - configure a device's MSI-X capability and table
 * (internal to pci_msix_alloc).  Programs @nvec table entries (0..nvec-1) with
 * the controller-composed message for dev->active.slots[i], leaving every entry
 * masked.  The caller has already reserved the slots; AddIntVectorServer unmasks
 * each vector when its server is installed.  Returns 0 on success, negative on error.
 */
static s32 pci_msix_capability_init(struct pci_device *dev, u32 nvec)
{
	struct pci_controller *ctrl = pci_get_controller(dev->bus);
	u8 bir = dev->msix.table_bir;

	if (!ctrl)
		return -EINVAL;
	if (nvec == 0 || nvec > dev->msix.table_size)
		return -EINVAL;

	/* The MSI-X table lives in a memory BAR that auto-config has mapped to a
	 * CPU-virtual address.  Bail to the MSI/INTx fallback if it is not. */
	if (bir >= dev->bars_num || !dev->bars[bir].present ||
		dev->bars[bir].type != PCI_REGION_MEM || dev->bars[bir].virt_addr == NULL)
	{
		Kprintf("[pcie] %s: device %04lx:%04lx MSI-X table BIR %ld not mapped\n",
				__func__, (ULONG)dev->vendor, (ULONG)dev->device, (LONG)bir);
		return -EIO;
	}
	if (dev->msix.table_offset + (u32)dev->msix.table_size * PCI_MSIX_ENTRY_SIZE >
		(u32)dev->bars[bir].size)
	{
		Kprintf("[pcie] %s: device %04lx:%04lx MSI-X table overruns BAR %ld\n",
				__func__, (ULONG)dev->vendor, (ULONG)dev->device, (LONG)bir);
		return -EIO;
	}

	dev->msix.table_virt = (u8 *)dev->bars[bir].virt_addr + dev->msix.table_offset;

	/*
	 * Enter MSI-X mode with the whole function masked while we program the
	 * table; per spec MSI-X and INTx are mutually exclusive, so enabling
	 * MSI-X stops INTx generation regardless.
	 */
	pci_msix_set_ctrl(dev, 0, PCI_MSIX_FLAGS_ENABLE | PCI_MSIX_FLAGS_MASKALL);

	for (u32 i = 0; i < nvec; i++)
	{
		u32 address_lo, address_hi;
		u16 data;
		volatile u8 *e = msix_entry(dev, i);

		brcm_pcie_compose_msi_msg(ctrl, dev->active.slots[i], &address_lo, &address_hi, &data);

		mmio_write32(address_lo, e + PCI_MSIX_ENTRY_LOWER_ADDR);
		mmio_write32(address_hi, e + PCI_MSIX_ENTRY_UPPER_ADDR);
		mmio_write32(data, e + PCI_MSIX_ENTRY_DATA); /* 16-bit data, zero-extended */
		mmio_write32(PCI_MSIX_ENTRY_CTRL_MASKBIT,
					 e + PCI_MSIX_ENTRY_VECTOR_CTRL); /* start masked */

		KprintfT("[pcie] %s: device %04lx:%04lx MSI-X entry %ld data 0x%04lx (slot %ld)\n",
				 __func__, (ULONG)dev->vendor, (ULONG)dev->device,
				 (LONG)i, (ULONG)data, (LONG)dev->active.slots[i]);
	}

	/* Drop legacy INTx, then clear the function mask (per-vector masks remain). */
	pci_intx(dev, 0);
	pci_msix_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);

	Kprintf("[pcie] %s: device %04lx:%04lx MSI-X enabled, %ld vector(s)\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device, (LONG)nvec);
	return 0;
}

/**
 * pci_msix_alloc - reserve and program an MSI-X allocation
 *
 * See pci_msix.h.  Carved from the former pci_irq_alloc_msi() MSI-X path:
 * reserve any free slots up to the table size, then program the table; roll
 * back on failure.
 */
s32 pci_msix_alloc(struct pci_device *dev, u32 min, u32 max)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (!pcie || !pcie->msi.enabled || !dev->msix.cap_offset)
		return -ENODEV;
	if (min < 1)
		min = 1;
	if (max > MSI_MAX_VECTORS)
		max = MSI_MAX_VECTORS;
	if (max < min)
		return -ERANGE;

	u32 n = max;
	if (n > dev->msix.table_size)
		n = dev->msix.table_size;
	u32 freec = pci_irq_slots_free_count(pcie);
	if (n > freec)
		n = freec;
	if (n < min)
		return -ENOSPC;

	if (pci_irq_slots_alloc_any(pcie, n, dev->active.slots) != 0)
		return -ENOSPC;
	dev->active.mode = PCI_IRQT_MSIX;
	dev->active.nvec = (u16)n;
	if (pci_msix_capability_init(dev, n) == 0)
		return (s32)n;

	pci_irq_slots_free(pcie, dev->active.slots, n);
	dev->active.mode = PCI_IRQT_NONE;
	dev->active.nvec = 0;
	return -EIO;
}

void pci_msix_shutdown(struct pci_device *dev)
{
	if (!dev || !dev->msix.cap_offset)
		return;

	Kprintf("[pcie] %s: device %04lx:%04lx shutting down MSI-X\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device);

	/* Mask every allocated vector, then mask the function and disable MSI-X. */
	if (dev->msix.table_virt)
		for (u32 i = 0; i < dev->active.nvec; i++)
			mmio_write32(PCI_MSIX_ENTRY_CTRL_MASKBIT,
						 msix_entry(dev, i) + PCI_MSIX_ENTRY_VECTOR_CTRL);
	pci_msix_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, PCI_MSIX_FLAGS_MASKALL);
}

void pci_msix_init(struct pci_device *dev)
{
	dev->msix.cap_offset = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!dev->msix.cap_offset)
		return;

	u16 control;
	pci_read_config16(dev, dev->msix.cap_offset + PCI_MSIX_FLAGS, &control);
	dev->msix.table_size = (u16)((control & PCI_MSIX_FLAGS_QSIZE) + 1);

	u32 table;
	pci_read_config32(dev, dev->msix.cap_offset + PCI_MSIX_TABLE, &table);
	dev->msix.table_bir = (u8)(table & PCI_MSIX_TABLE_BIR);
	dev->msix.table_offset = table & PCI_MSIX_TABLE_OFFSET;

	Kprintf("[pcie] %s: device %04lx:%04lx MSI-X capable: %ld vectors, table BIR %ld offset 0x%lx\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device,
			(LONG)dev->msix.table_size, (LONG)dev->msix.table_bir,
			(ULONG)dev->msix.table_offset);

	/* Leave the device in a known-off state at probe. */
	if (control & PCI_MSIX_FLAGS_ENABLE)
		pci_msix_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);
}
