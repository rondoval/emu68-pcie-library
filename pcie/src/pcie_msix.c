// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI MSI-X support.
 *
 * Mirrors pcie_msi.c, but the per-vector message lives in a BAR-mapped table
 * rather than config space.  Both MSI and MSI-X deliver the same memory write
 * to the controller doorbell; the message (address/data) is supplied by the
 * controller back-end via brcm_msi_compose_msg() and the demux ISR lives
 * in pcie_brcmstb_msi.c.  Only the device-side table programming differs and
 * lives here — this file holds no controller specifics.
 *
 * The table is located and validated once at probe (pci_msix_init), so
 * programming an allocation cannot fail.
 */

#define __NOLIBBASE__
#define EXEC_BASE_NAME SysBase /* a local in every function, from its context's sysBase */
#include <debug.h>
#include <bits.h>
#include <errors.h>

#include <pci.h>
#include <iomem.h>
#include <pci_irq.h>
#include <pci_capability.h>
#include <pci_io.h>
#include <pci_util.h>
#include <pcie_brcmstb.h>

/* Byte pointer to MSI-X table entry @vec. */
static inline volatile u8 *msix_entry(struct pci_device *dev, u32 vec)
{
	return (volatile u8 *)dev->msix.table_virt + (vec * PCI_MSIX_ENTRY_SIZE);
}

static void pci_msix_set_ctrl(struct pci_device *dev, u16 clear, u16 set)
{
	u16 control;

	pci_read_config16(dev, dev->msix.cap_offset + PCI_MSIX_FLAGS, &control);
	control = (u16)((control & ~clear) | set);
	pci_write_config16(dev, dev->msix.cap_offset + PCI_MSIX_FLAGS, control);
}

void pci_msix_entry_mask(struct pci_device *dev, u32 vec)
{
	KprintfT("[pcie] %s: device %04lx:%04lx mask MSI-X entry %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, (LONG)vec);
	mmio_write32(PCI_MSIX_ENTRY_CTRL_MASKBIT,
				 msix_entry(dev, vec) + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

void pci_msix_entry_unmask(struct pci_device *dev, u32 vec)
{
	KprintfT("[pcie] %s: device %04lx:%04lx unmask MSI-X entry %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, (LONG)vec);
	mmio_write32(0u, msix_entry(dev, vec) + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

/* Program table entries 0..@nvec-1 with the controller-composed message for
 * dev->active.slots[i] and enable MSI-X; every entry starts masked (pci_irq
 * opens each one when its server is added). */
static void pci_msix_program(struct pci_device *dev, u32 nvec)
{
	struct pci_controller *ctrl = pci_get_controller(dev->bus);

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

		brcm_msi_compose_msg(ctrl, dev->active.slots[i], &address_lo, &address_hi, &data);

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
}

/*
 * pci_msix_alloc - reserve and program an MSI-X allocation: as many free
 * slots as [min,max] and the table size allow (no alignment needed - every
 * entry carries its own message).
 */
s32 pci_msix_alloc(struct pci_device *dev, u32 min, u32 max)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (!dev->msix.table_virt)
		return -ENODEV;

	u32 n = max;
	if (n > dev->msix.table_size)
		n = dev->msix.table_size;

	n = pci_irq_slots_alloc_any(pcie, n, dev->active.slots);
	if (n < min)
	{
		pci_irq_slots_free(pcie, dev->active.slots, n);
		return -ENOSPC;
	}

	pci_msix_program(dev, n);
	return (s32)n;
}

void pci_msix_shutdown(struct pci_device *dev)
{
	Kprintf("[pcie] %s: device %04lx:%04lx shutting down MSI-X\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device);

	/* Mask the function and disable MSI-X.  Every entry is masked already:
	 * programmed masked, and closed again when its server was removed. */
	pci_msix_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, PCI_MSIX_FLAGS_MASKALL);
}

/*
 * Probe: locate the capability, make sure MSI-X is off, and resolve the
 * table.  It lives in a memory BAR that auto-config has already mapped to a
 * CPU-virtual address; if it does not, table_virt stays NULL, MSI-X is
 * unusable on this device and allocation falls back to MSI or INTx.
 */
void pci_msix_init(struct pci_device *dev)
{
	dev->msix.cap_offset = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!dev->msix.cap_offset)
		return;

	pci_msix_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);

	u16 control;
	pci_read_config16(dev, dev->msix.cap_offset + PCI_MSIX_FLAGS, &control);
	dev->msix.table_size = (u16)((control & PCI_MSIX_FLAGS_QSIZE) + 1);

	u32 table;
	pci_read_config32(dev, dev->msix.cap_offset + PCI_MSIX_TABLE, &table);
	u8 bir = (u8)(table & PCI_MSIX_TABLE_BIR);
	u32 offset = table & PCI_MSIX_TABLE_OFFSET;

	Kprintf("[pcie] %s: device %04lx:%04lx MSI-X capable: %ld vectors, table BIR %ld offset 0x%lx\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device,
			(LONG)dev->msix.table_size, (LONG)bir, (ULONG)offset);

	if (bir >= dev->bars_num || !dev->bars[bir].present ||
		dev->bars[bir].type != PCI_REGION_MEM || dev->bars[bir].virt_addr == NULL)
	{
		Kprintf("[pcie] %s: device %04lx:%04lx MSI-X table BIR %ld not mapped\n",
				__func__, (ULONG)dev->vendor, (ULONG)dev->device, (LONG)bir);
		return;
	}
	if (offset + (u32)dev->msix.table_size * PCI_MSIX_ENTRY_SIZE > (u32)dev->bars[bir].size)
	{
		Kprintf("[pcie] %s: device %04lx:%04lx MSI-X table overruns BAR %ld\n",
				__func__, (ULONG)dev->vendor, (ULONG)dev->device, (LONG)bir);
		return;
	}

	dev->msix.table_virt = (u8 *)dev->bars[bir].virt_addr + offset;
}
