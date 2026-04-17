// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <pci.h>
#include <debug.h>
#include <errors.h>
#include <timing.h>
#include <pci_util.h>
#include <pci_capability.h>
#include <pci_io.h>

s32 pci_get_bus(struct pci_controller *controller, u32 busnum, struct pci_bus **busp)
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
s32 pci_get_bus_max(const struct pci_controller *controller)
{
	s32 ret = -1;

	for (struct MinNode *node = controller->buses.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_bus *bus = (struct pci_bus *)node;
		if (ret < 0 || bus->bus_number > (u32)ret)
			ret = (s32)bus->bus_number;
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

u32 pci_get_ff(enum pci_size_t size)
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

pci_dev_t pci_get_bdf(const struct pci_device *dev)
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

u32 pci_conv_32_to_size(u32 value, u32 offset, enum pci_size_t size)
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

u32 pci_conv_size_to_32(u32 old, u32 value, u32 offset, enum pci_size_t size)
{
	u32 off_mask;
	u32 val_mask;

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
	u32 shift = (offset & off_mask) * 8;
	u32 ldata = (value & val_mask) << shift;
	u32 mask = val_mask << shift;
	value = (old & ~mask) | ldata;

	return value;
}

u32 pci_get_regions(struct pci_device *dev, struct pci_region **iop, struct pci_region **memp, struct pci_region **prefp)
{
	struct pci_controller *ctrl = pci_get_controller(dev->bus);

	*iop = NULL;
	*memp = NULL;
	*prefp = NULL;
	for (u32 i = 0; i < ctrl->region_count; i++)
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

	return (u32)((*iop != NULL) + (*memp != NULL) + (*prefp != NULL));
}

/**
 * pci_flr() - Perform a Function Level Reset on a PCIe device
 *
 * @dev: PCI device to reset
 * Return: 0 if OK, -ve on error
 */
s32 pci_flr(struct pci_device *dev)
{
	/* look for PCI Express Capability */
	u32 pcie_off = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pcie_off)
		return -ENOENT;

	u32 cap;
	/* check FLR capability */
	pci_read_config32(dev, pcie_off + PCI_EXP_DEVCAP, &cap);
	if (!(cap & PCI_EXP_DEVCAP_FLR))
		return -ENOENT;

	pci_clrset_config16(dev, pcie_off + PCI_EXP_DEVCTL, 0,
						   PCI_EXP_DEVCTL_BCR_FLR);

	/* wait 100ms, per PCI spec */
	delay_us(100 * 1000);

	return 0;
}
