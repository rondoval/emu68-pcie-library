// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>
#include <debug.h>

phys_addr_t pci_bus_to_phys(struct pci_device *dev, pci_addr_t bus_addr, size_t len, u32 mask, u32 flags)
{
	/* The root controller has the region information */
	struct pci_controller *bus = pci_get_controller(dev->bus);

	if (bus->region_count == 0)
		return bus_addr;

	for (u32 i = 0; i < bus->region_count; i++)
	{
		struct pci_region *res = &bus->regions[i];

		if ((res->flags & mask) != flags)
			continue;

		if (bus_addr < res->bus_start)
			continue;

		pci_addr_t offset = bus_addr - res->bus_start;
		if (offset >= res->size)
			continue;

		if (len > res->size - offset)
			continue;

		return res->phys_start + offset;
	}

	Kprintf("[pcie] %s: invalid physical address\n", __func__);
	return 0;
}

pci_addr_t pci_phys_to_bus(struct pci_device *dev, phys_addr_t phys_addr, size_t len, u32 mask, u32 flags)
{
	/* The root controller has the region information */
	struct pci_controller *bus = pci_get_controller(dev->bus);

	if (bus->region_count == 0)
		return (pci_addr_t)phys_addr;

	for (u32 i = 0; i < bus->region_count; i++)
	{
		struct pci_region *res = &bus->regions[i];

		if ((res->flags & mask) != flags)
			continue;

		if (phys_addr < res->phys_start)
			continue;

		phys_addr_t offset = phys_addr - res->phys_start;
		if (offset >= res->size)
			continue;

		if (len > res->size - offset)
			continue;

		return (pci_addr_t)(res->bus_start + offset);
	}

	Kprintf("[pcie] %s: pci_phys_to_bus: invalid physical address\n", __func__);
	return 0;
}

void *pci_bus_to_virt(struct pci_device *dev, pci_addr_t bus_addr, size_t len, u32 mask, u32 flags)
{
	phys_addr_t phys_addr = pci_bus_to_phys(dev, bus_addr, len, mask, flags);
	if (!phys_addr)
		return NULL;

	KprintfH("[pcie] %s: bus_addr 0x%lx%08lx -> phys_addr 0x%lx%08lx\n", __func__, (ULONG)(bus_addr >> 32), (ULONG)(bus_addr & 0xffffffff), (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff));
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

	KprintfH("[pcie] %s: phys_addr 0x%lx%08lx -> virt_addr 0x%lx\n", __func__, (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff),
			(ULONG)(ctlr->mmio_window_virtual + (phys_addr - ctlr->mmio_window_phys)));

	return ctlr->mmio_window_virtual + (phys_addr - ctlr->mmio_window_phys);
}

pci_addr_t pci_virt_to_bus(struct pci_device *dev, void *virt_addr, size_t len, u32 mask, u32 flags)
{
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	u8 *virtual_address = (u8 *)virt_addr;
	if (virtual_address < ctlr->mmio_window_virtual || virtual_address >= ctlr->mmio_window_virtual + ctlr->mmio_window_size)
	{
		Kprintf("[pcie] %s: address 0x%lx not in MMIO window (0x%lx-0x%lx)\n", __func__, (ULONG)virt_addr,
				(ULONG)ctlr->mmio_window_virtual, (ULONG)(ctlr->mmio_window_virtual + ctlr->mmio_window_size));
		return 0;
	}

	phys_addr_t phys_addr = ctlr->mmio_window_phys + (ULONG)(virtual_address - ctlr->mmio_window_virtual);
	return pci_phys_to_bus(dev, phys_addr, len, mask, flags);
}

void *pci_map_bar(struct pci_device *dev, u32 bar, pci_addr_t offset, size_t len,
					 u32 mask, u32 flags)
{
	u32 bar_response;

	/* read BAR address */
	pci_read_config32(dev, bar, &bar_response);
	pci_addr_t pci_bus_addr = bar_response & ~0x0fUL;
	KprintfH("[pcie] %s: BAR%ld response %lx, addr %lx%08lx\n", __func__, (bar - PCI_BASE_ADDRESS_0) / 4, bar_response, (ULONG)(pci_bus_addr >> 32), (ULONG)(pci_bus_addr & 0xffffffff));

	/* This has a lot of baked in assumptions, but essentially tries
	 * to mirror the behavior of BAR assignment for 64 Bit enabled
	 * hosts and 64 bit placeable BARs in the auto assign code.
	 */
#if defined(CONFIG_SYS_PCI_64BIT)
	if (bar_response & PCI_BASE_ADDRESS_MEM_TYPE_64)
	{
		pci_read_config32(dev, bar + 4, &bar_response);
		pci_bus_addr |= (pci_addr_t)bar_response << 32;
	}
#endif /* CONFIG_SYS_PCI_64BIT */

	if (~((pci_addr_t)0) - pci_bus_addr < offset)
		return NULL;

	/*
	 * Forward the length argument to pci_bus_to_virt. The length will
	 * be used to check that the entire address range has been declared as
	 * a PCI range, but a better check would be to probe for the size of
	 * the bar and prevent overflow more locally.
	 */
	return pci_bus_to_virt(dev, pci_bus_addr + offset, len, mask, flags);
}
