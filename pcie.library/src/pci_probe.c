// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <exec/lists.h>

#include <debug.h>
#include <errors.h>
#include <format.h>
#include <pcie_brcmstb.h>
#include <pci.h>
#include <devtree.h>
#include <minlist.h>

int pci_create_bus(struct pci_bus **busp, struct pci_bus *parent, struct pci_device *bridge, struct pci_controller *ctlr)
{
	KprintfH("[pcie] %s: creating bus for bridge %lx, function %ld\n", __func__, PCI_DEV(bridge->bdf), PCI_FUNC(bridge->bdf));
	struct pci_bus *bus = AllocMem(sizeof(*bus), MEMF_CLEAR);
	if (!bus)
		return -ENOMEM;

	_NewMinList(&bus->devices);
	_SNPrintf(bus->name, 30, (CONST_STRPTR) "pci_bus_%lx:%lx.%lx", PCI_BUS(bridge->bdf), PCI_DEV(bridge->bdf), PCI_FUNC(bridge->bdf));
	bus->parent = parent;
	bus->pci_bridge = bridge;
	bus->controller = ctlr;

	AddTailMinList(&ctlr->buses, (struct MinNode *)bus);
	*busp = bus;
	return 0;
}

int pci_probe_bus(struct pci_bus *bus)
{
	int ret;

	if (!bus)
		return -EINVAL;

	if (bus->pci_bridge->flags & DM_FLAG_ACTIVATED)
		return 0;

	/* Ensure all parents are probed */
	if (bus->parent)
	{
		ret = pci_probe_bus(bus->parent);
		if (ret)
			goto fail;

		/*
		 * The device might have already been probed during
		 * the call to pci_bus_probe() on its parent device
		 * (e.g. PCI bridge devices). Test the flags again
		 * so that we don't mess up the device.
		 */
		if (bus->pci_bridge->flags & DM_FLAG_ACTIVATED)
			return 0;

		bus->controller = bus->parent->controller;
	}

	bus->pci_bridge->flags |= DM_FLAG_ACTIVATED;
	bus->bus_number = ++bus->controller->bus_number_last;
	bus->bus_number_last_sub = bus->bus_number;

	// ret = device_get_dma_constraints(dev);
	// if (ret)
	// 	goto fail;

	ret = pci_bind_bus_devices(bus);
	if (ret)
	{
		Kprintf("[pcie] %s: failed to bind devices on bus %ld\n", __func__, bus->bus_number);
		goto fail;
	}

	ret = pci_auto_config_devices(bus);
	if (ret < 0)
	{
		Kprintf("[pcie] %s: failed to configure devices on bus %ld\n", __func__, bus->bus_number);
		goto fail;
	}

	return 0;

fail:
	bus->pci_bridge->flags &= ~DM_FLAG_ACTIVATED;
	// device_free(dev);
	return ret;
}

int pci_auto_config_devices(struct pci_bus *bus)
{
	pciauto_config_init(bus->controller);
	for (struct MinNode *node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_device *device = (struct pci_device *)node;

		Kprintf("[pcie] %s: device %s\n", __func__, device->name);
		int ret = dm_pciauto_config_device(device);
		if (ret < 0)
		{
			Kprintf("[pcie] %s: Failed to configure device %s: %ld\n", __func__, device->name, ret);
			return ret;
		}
		bus->bus_number_last_sub = bus->bus_number_last_sub < ret ? ret : bus->bus_number_last_sub;
	}

	KprintfH("[pcie] %s: done, last_sub = %ld\n", __func__, bus->bus_number_last_sub);
	return 0;
}

int dm_pci_hose_probe_bus(struct pci_bus *bus)
{
	UBYTE header_type;
	dm_pci_read_config8(bus->pci_bridge, PCI_HEADER_TYPE, &header_type);
	header_type &= 0x7f;
	if (header_type != PCI_HEADER_TYPE_BRIDGE)
	{
		Kprintf("[pcie] %s: Skipping PCI device %ld with Non-Bridge Header Type 0x%lx\n", __func__, PCI_DEV(dm_pci_get_bdf(bus->pci_bridge)), header_type);
		return -EINVAL;
	}

	// TODO merge this with pci_probe_bus
	bus->bus_number = bus->controller->bus_number_last + 1;
	Kprintf("[pcie] %s: bus = %ld/%s\n", __func__, bus->bus_number, bus->name);
	dm_pciauto_prescan_setup_bridge(bus);

	int ret = pci_probe_bus(bus);
	if (ret)
	{
		Kprintf("[pcie] %s: Cannot probe bus %s: %ld\n", __func__, bus->name, ret);
		return ret;
	}

	dm_pciauto_postscan_setup_bridge(bus);
	return bus->bus_number_last_sub;
}

int pci_create_device(struct pci_bus *bus, pci_dev_t bdf, UWORD vendor, UWORD device, ULONG class, struct pci_device **devp)
{
	KprintfH("[pcie] %s: creating pci_device %lx:%ld (vendor 0x%lx device 0x%lx)\n", __func__, PCI_DEV(bdf), PCI_FUNC(bdf), vendor, device);
	*devp = NULL;
	struct pci_device *dev = AllocMem(sizeof(struct pci_device), MEMF_CLEAR);
	if (!dev)
		return -ENOMEM;

	dev->bus = bus;
	dev->flags |= DM_FLAG_BOUND;
	_SNPrintf(dev->name, 30, (CONST_STRPTR) "pci_%lx:%lx.%lx", PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));
	dev->bdf = bdf;
	dev->devfn = PCI_MASK_BUS(bdf);
	dev->vendor = vendor;
	dev->device = device;
	dev->class = class;

	AddTailMinList(&bus->devices, (struct MinNode *)dev);
	*devp = dev;

	return 0;
}

int pci_bind_bus_devices(struct pci_bus *bus)
{
	int ret;

	BOOL found_multi = FALSE;
	pci_dev_t end = PCI_BDF(bus->bus_number, PCI_MAX_PCI_DEVICES - 1, PCI_MAX_PCI_FUNCTIONS - 1);
	for (pci_dev_t bdf = PCI_BDF(bus->bus_number, 0, 0); bdf <= end; bdf += PCI_BDF(0, 0, 1))
	{
		if (!PCI_FUNC(bdf))
			found_multi = FALSE;
		if (PCI_FUNC(bdf) && !found_multi)
			continue;

		KprintfH("[pcie] %s: checking device 0x%lx, function %ld\n", __func__, PCI_DEV(bdf), PCI_FUNC(bdf));

		/* Check only the first access, we don't expect problems */
		ULONG vendor;
		ret = brcm_pcie_read_config(bus->controller, bdf, PCI_VENDOR_ID, &vendor, PCI_SIZE_16);
		if (ret || vendor == 0xffff || vendor == 0x0000)
			continue;

		ULONG header_type;
		brcm_pcie_read_config(bus->controller, bdf, PCI_HEADER_TYPE, &header_type, PCI_SIZE_8);

		if (!PCI_FUNC(bdf))
			found_multi = header_type & 0x80;

		Kprintf("[pcie] %s: bus %ld/%s: found device %lx, function %ld\n", __func__, bus->bus_number, bus->name, PCI_DEV(bdf), PCI_FUNC(bdf));
		ULONG device, class;
		brcm_pcie_read_config(bus->controller, bdf, PCI_DEVICE_ID, &device, PCI_SIZE_16);
		brcm_pcie_read_config(bus->controller, bdf, PCI_CLASS_REVISION, &class, PCI_SIZE_32);
		class >>= 8;

		struct pci_device *dev;
		/* Find this device in the device tree */
		ret = pci_bus_find_devfn(bus, PCI_MASK_BUS(bdf), &dev);
		KprintfH("[pcie] %s: find ret=%ld\n", __func__, ret);

		/* If nothing in the device tree, bind a device */
		if (ret == -ENODEV)
		{
			ret = pci_create_device(bus, bdf, vendor, device, class, &dev);
		}
		else
		{
			Kprintf("[pcie] device: %s\n", dev->name);
		}
		if (ret == -EPERM)
			continue;
		else if (ret)
			return ret;
	}

	return 0;
}
