// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>

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

void *dm_pci_map_bar(struct pci_device *dev, int bar, size_t offset, size_t len,
					 ULONG mask, ULONG flags)
{
	struct pci_device *udev = dev;
	pci_addr_t pci_bus_addr;
	ULONG bar_response;

	/* read BAR address */
	dm_pci_read_config32(udev, bar, &bar_response);
	pci_bus_addr = (pci_addr_t)(bar_response & ~0xf);

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
	return dm_pci_bus_to_virt(udev, pci_bus_addr + offset, len, mask, flags, 0); //TODOMAP_NOCACHE);
}

void *map_physmem(phys_addr_t phys_addr, size_t len, int map_flags)
{
	void *virt_addr;

	// TODO emu68 needs to mmap our BAR window i think. It will likely update DT address to match
	//  this is likely to be removed
	virt_addr = (void *)(phys_addr + 0);

	return virt_addr;
}