// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <pci.h>
#include <debug.h>
#include <errors.h>
#include <timing.h>

int pci_get_bus(struct pci_controller *controller, int busnum, struct pci_bus **busp)
{
	for(struct MinNode *node = controller->buses.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_bus *bus = (struct pci_bus *)node;
		if (bus->bus_number == busnum)
		{
			*busp = bus;
			return 0;
		}
	}

	return -ENODEV;
}

/**
 * pci_get_bus_max() - returns the bus number of the last active bus
 *
 * Return: last bus number, or -1 if no active buses
 */
int pci_get_bus_max(const struct pci_controller *controller)
{
	int ret = -1;

	for (struct MinNode *node = controller->buses.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_bus *bus = (struct pci_bus *)node;
		if (bus->bus_number > ret)
			ret = bus->bus_number;
	}
	return ret;
}

BOOL pci_is_root_bus(const struct pci_bus *bus)
{
	return bus->parent == NULL;
}

struct pci_controller *pci_get_controller(const struct pci_bus *bus)
{
	/* we're copying the controller from the parent bus on bind */
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

pci_dev_t dm_pci_get_bdf(const struct pci_device *dev)
{
	// /*
	//  * This error indicates that @dev is a device on an unprobed PCI bus.
	//  * The bus likely has bus=seq == -1, so the PCI_ADD_BUS() macro below
	//  * will produce a bad BDF>
	//  *
	//  * A common cause of this problem is that this function is called in the
	//  * of_to_plat() method of @dev. Accessing the PCI bus in that
	//  * method is not allowed, since it has not yet been probed. To fix this,
	//  * move that access to the probe() method of @dev instead.
	//  */
	// if (!(dev->bus->pci_bridge->flags & DM_FLAG_ACTIVATED))
	// {
	// 	Kprintf("[pcie] %s: Device '%s' on unprobed bus '%s'\n", __func__, dev->name, dev->bus->name);
	// }
	// return PCI_ADD_BUS(dev->bus->bus_number, dev->devfn);
	return dev->bdf;
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
