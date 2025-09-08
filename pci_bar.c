// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>
#include <debug.h>

phys_addr_t dm_pci_bus_to_phys(struct pci_device *dev, pci_addr_t bus_addr, size_t len, ULONG mask, ULONG flags)
{
	struct pci_region *res;
	pci_addr_t offset;
	int i;

	/* The root controller has the region information */
	struct pci_controller *bus = pci_get_controller(dev->bus);

	if (bus->region_count == 0)
		return bus_addr;

	for (i = 0; i < bus->region_count; i++)
	{
		res = &bus->regions[i];

		if ((res->flags & mask) != flags)
			continue;

		if (bus_addr < res->bus_start)
			continue;

		offset = bus_addr - res->bus_start;
		if (offset >= res->size)
			continue;

		if (len > res->size - offset)
			continue;

		return res->phys_start + offset;
	}

	Kprintf("[pcie] %s: invalid physical address\n", __func__);
	return 0;
}

pci_addr_t dm_pci_phys_to_bus(struct pci_device *dev, phys_addr_t phys_addr, size_t len, ULONG mask, ULONG flags)
{
	struct pci_region *res;
	phys_addr_t offset;
	int i;

	/* The root controller has the region information */
	struct pci_controller *bus = pci_get_controller(dev->bus);

	if (bus->region_count == 0)
		return phys_addr;

	for (i = 0; i < bus->region_count; i++)
	{
		res = &bus->regions[i];

		if ((res->flags & mask) != flags)
			continue;

		if (phys_addr < res->phys_start)
			continue;

		offset = phys_addr - res->phys_start;
		if (offset >= res->size)
			continue;

		if (len > res->size - offset)
			continue;

		return res->bus_start + offset;
	}

	Kprintf("[pcie] %s: dm_pci_phys_to_bus: invalid physical address\n", __func__);
	return 0;
}

void *dm_pci_bus_to_virt(struct pci_device *dev, pci_addr_t bus_addr, size_t len, ULONG mask, ULONG flags)
{
	phys_addr_t phys_addr = dm_pci_bus_to_phys(dev, bus_addr, len, mask, flags);
	if (!phys_addr)
		return NULL;

	Kprintf("[pcie] %s: bus_addr 0x%lx%08lx -> phys_addr 0x%lx%08lx\n", __func__, (ULONG)(bus_addr >> 32), (ULONG)(bus_addr & 0xffffffff), (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff));
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	if (phys_addr < ctlr->mmio_window_phys || phys_addr >= ctlr->mmio_window_phys + ctlr->mmio_window_size)
	{
		Kprintf("[pcie] %s: address 0x%lx%08lx not in MMIO window (0x%lx%08lx-0x%lx%08lx)\n", __func__, (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff),
				(ULONG)(ctlr->mmio_window_phys >> 32), (ULONG)(ctlr->mmio_window_phys & 0xffffffff),
				(ULONG)((ctlr->mmio_window_phys + ctlr->mmio_window_size) >> 32), (ULONG)((ctlr->mmio_window_phys + ctlr->mmio_window_size) & 0xffffffff));
		return NULL;
	}

	if (phys_addr + len > ctlr->mmio_window_phys + ctlr->mmio_window_size)
	{
		Kprintf("[pcie] %s: address 0x%lx%08lx+0x%lx%08lx exceeds MMIO window (0x%lx%08lx-0x%lx%08lx)\n", __func__, (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff), (ULONG)(len >> 32), (ULONG)(len & 0xffffffff), (ULONG)(ctlr->mmio_window_phys >> 32), (ULONG)(ctlr->mmio_window_phys & 0xffffffff), (ULONG)((ctlr->mmio_window_phys + ctlr->mmio_window_size) >> 32), (ULONG)((ctlr->mmio_window_phys + ctlr->mmio_window_size) & 0xffffffff));
		return NULL;
	}

	Kprintf("[pcie] %s: phys_addr 0x%lx%08lx -> virt_addr 0x%lx\n", __func__, (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff),
			(ULONG)(ctlr->mmio_window_virtual + (phys_addr - ctlr->mmio_window_phys)));

	return (void *)ctlr->mmio_window_virtual + (phys_addr - ctlr->mmio_window_phys);
}

pci_addr_t dm_pci_virt_to_bus(struct pci_device *dev, void *virt_addr, size_t len, ULONG mask, ULONG flags)
{
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	if ((ULONG)virt_addr < ctlr->mmio_window_virtual || (ULONG)virt_addr >= ctlr->mmio_window_virtual + ctlr->mmio_window_size)
	{
		Kprintf("[pcie] %s: address 0x%lx not in MMIO window (0x%lx-0x%lx)\n", __func__, (ULONG)virt_addr,
				ctlr->mmio_window_virtual, ctlr->mmio_window_virtual + ctlr->mmio_window_size);
		return 0;
	}

	phys_addr_t phys_addr = ctlr->mmio_window_phys + ((ULONG)virt_addr - ctlr->mmio_window_virtual);
	return dm_pci_phys_to_bus(dev, phys_addr, len, mask, flags);
}

void *dm_pci_map_bar(struct pci_device *dev, int bar, size_t offset, size_t len,
					 ULONG mask, ULONG flags)
{
	struct pci_device *udev = dev;
	pci_addr_t pci_bus_addr;
	ULONG bar_response;

	/* read BAR address */
	dm_pci_read_config32(udev, bar, &bar_response);
	pci_bus_addr = (pci_addr_t)(bar_response & ~0xf);
	Kprintf("[pcie] %s: BAR%ld response %lx, addr %lx%08lx\n", __func__, (bar - PCI_BASE_ADDRESS_0) / 4, bar_response, (ULONG)(pci_bus_addr >> 32), (ULONG)(pci_bus_addr & 0xffffffff));

	/* This has a lot of baked in assumptions, but essentially tries
	 * to mirror the behavior of BAR assignment for 64 Bit enabled
	 * hosts and 64 bit placeable BARs in the auto assign code.
	 */
#if defined(CONFIG_SYS_PCI_64BIT)
	if (bar_response & PCI_BASE_ADDRESS_MEM_TYPE_64)
	{
		dm_pci_read_config32(udev, bar + 4, &bar_response);
		pci_bus_addr |= (pci_addr_t)bar_response << 32;
	}
#endif /* CONFIG_SYS_PCI_64BIT */

	if (~((pci_addr_t)0) - pci_bus_addr < offset)
		return NULL;

	/*
	 * Forward the length argument to dm_pci_bus_to_virt. The length will
	 * be used to check that the entire address range has been declared as
	 * a PCI range, but a better check would be to probe for the size of
	 * the bar and prevent overflow more locally.
	 */
	return dm_pci_bus_to_virt(udev, pci_bus_addr + offset, len, mask, flags);
}
