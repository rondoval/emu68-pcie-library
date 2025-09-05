// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <pci.h>

extern struct pci_bus *root_bus;

int pci_get_bus(int busnum, struct pci_bus **busp)
{
	if (root_bus == NULL)
	{
		device_probe(root_bus->pci_bridge); // TODO
	}

	struct pci_bus *bus;
	for (bus = root_bus; bus; bus = bus->next)
	{
		if (bus->bus_number == busnum)
		{
			*busp = bus;
			return 0;
		}
	}
	return -ENODEV;
}

struct pci_controller *pci_get_controller(const struct pci_bus *bus)
{
    //TODO just copy the controller to every bus
	for (; bus->parent; bus = bus->parent)
		;

	return bus->controller;
}

int pci_get_ff(enum pci_size_t size)
{
	switch (size)
	{
	case PCI_SIZE_8:
		return 0xff;
	case PCI_SIZE_16:
		return 0xffff;
	default:
		return 0xffffffff;
	}
}

ULONG pci_conv_32_to_size(ULONG value, UWORD offset, enum pci_size_t size)
{
	switch (size)
	{
	case PCI_SIZE_8:
		return (value >> ((offset & 3) * 8)) & 0xff;
	case PCI_SIZE_16:
		return (value >> ((offset & 2) * 8)) & 0xffff;
	default:
		return value;
	}
}

ULONG pci_conv_size_to_32(ULONG old, ULONG value, UWORD offset, enum pci_size_t size)
{
	UWORD off_mask;
	UWORD val_mask, shift;
	ULONG ldata, mask;

	switch (size)
	{
	case PCI_SIZE_8:
		off_mask = 3;
		val_mask = 0xff;
		break;
	case PCI_SIZE_16:
		off_mask = 2;
		val_mask = 0xffff;
		break;
	default:
		return value;
	}
	shift = (offset & off_mask) * 8;
	ldata = (value & val_mask) << shift;
	mask = val_mask << shift;
	value = (old & ~mask) | ldata;

	return value;
}

int pci_get_regions(struct pci_device *dev, struct pci_region **iop, struct pci_region **memp, struct pci_region **prefp)
{
	struct pci_controller *ctrl = pci_get_controller(dev->bus);
	int i;

	*iop = NULL;
	*memp = NULL;
	*prefp = NULL;
	for (i = 0; i < ctrl->region_count; i++)
	{
		switch (ctrl->regions[i].flags)
		{
		case PCI_REGION_IO:
			if (!*iop || (*iop)->size < ctrl->regions[i].size)
				*iop = ctrl->regions + i;
			break;
		case PCI_REGION_MEM:
			if (!*memp || (*memp)->size < ctrl->regions[i].size)
				*memp = ctrl->regions + i;
			break;
		case (PCI_REGION_MEM | PCI_REGION_PREFETCH):
			if (!*prefp || (*prefp)->size < ctrl->regions[i].size)
				*prefp = ctrl->regions + i;
			break;
		}
	}

	return (*iop != NULL) + (*memp != NULL) + (*prefp != NULL);
}

/**
 * dm_pci_flr() - Perform a Function Level Reset on a PCIe device
 *
 * @dev: PCI device to reset
 * Return: 0 if OK, -ve on error
 */
int dm_pci_flr(struct pci_device *dev)
{
	int pcie_off;
	ULONG cap;

	/* look for PCI Express Capability */
	pcie_off = dm_pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pcie_off)
		return -ENOENT;

	/* check FLR capability */
	dm_pci_read_config32(dev, pcie_off + PCI_EXP_DEVCAP, &cap);
	if (!(cap & PCI_EXP_DEVCAP_FLR))
		return -ENOENT;

	dm_pci_clrset_config16(dev, pcie_off + PCI_EXP_DEVCTL, 0,
						   PCI_EXP_DEVCTL_BCR_FLR);

	/* wait 100ms, per PCI spec */
	delay_us(100 * 1000);

	return 0;
}
