// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <pcie_brcmstb.h>
#include <pci.h>
#include <devtree.h>
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/utility_protos.h>
#else
#include <proto/exec.h>
#include <proto/utility.h>
#endif

#include <debug.h>
#include <compat.h>
#include <pci.h>

struct pci_bus *root_bus;

int device_probe(struct pci_device *dev)
{
	int ret;

	if (!dev)
		return -EINVAL;

	// if (dev_get_flags(dev) & DM_FLAG_ACTIVATED)
	// 	return 0;

	// ret = device_notify(dev, EVT_DM_PRE_PROBE);
	// if (ret)
	// 	return ret;


	// ret = device_of_to_plat(dev);
	// if (ret)
	// 	goto fail;

	/* Ensure all parents are probed */
	if (dev->bus) {
		ret = device_probe(dev->bus->pci_bridge);
		if (ret)
			goto fail;

		/*
		 * The device might have already been probed during
		 * the call to device_probe() on its parent device
		 * (e.g. PCI bridge devices). Test the flags again
		 * so that we don't mess up the device.
		 */
		// if (dev_get_flags(dev) & DM_FLAG_ACTIVATED)
		// 	return 0;
	}

	// dev_or_flags(dev, DM_FLAG_ACTIVATED);

	// if (CONFIG_IS_ENABLED(POWER_DOMAIN) && dev->parent &&
	//     (device_get_uclass_id(dev) != UCLASS_POWER_DOMAIN) &&
	//     !(drv->flags & DM_FLAG_DEFAULT_PD_CTRL_OFF)) {
	// 	ret = dev_power_domain_on(dev);
	// 	if (ret)
	// 		goto fail;
	// }

	/*
	 * Process pinctrl for everything except the root device, and
	 * continue regardless of the result of pinctrl. Don't process pinctrl
	 * settings for pinctrl devices since the device may not yet be
	 * probed.
	 *
	 * This call can produce some non-intuitive results. For example, on an
	 * x86 device where dev is the main PCI bus, the pinctrl device may be
	 * child or grandchild of that bus, meaning that the child will be
	 * probed here. If the child happens to be the P2SB and the pinctrl
	 * device is a child of that, then both the pinctrl and P2SB will be
	 * probed by this call. This works because the DM_FLAG_ACTIVATED flag
	 * is set just above. However, the PCI bus' probe() method and
	 * associated uclass methods have not yet been called.
	 */
	// if (dev->parent && device_get_uclass_id(dev) != UCLASS_PINCTRL) {
	// 	ret = pinctrl_select_state(dev, "default");
	// 	if (ret && ret != -ENOSYS)
	// 		log_debug("Device '%s' failed to configure default pinctrl: %d (%s)\n",
	// 			  dev->name, ret, errno_str(ret));
	// }

	// ret = device_get_dma_constraints(dev);
	// if (ret)
	// 	goto fail;

	// ret = uclass_pre_probe_device(dev);
	// if (ret)
	// 	goto fail;

	// if (dev->parent && dev->parent->driver->child_pre_probe) {
	// 	ret = dev->parent->driver->child_pre_probe(dev);
	// 	if (ret)
	// 		goto fail;
	// }

	// ret = uclass_post_probe_device(dev);
	// if (ret)
	// 	goto fail_uclass;

	// if (dev->parent && device_get_uclass_id(dev) == UCLASS_PINCTRL) {
	// 	ret = pinctrl_select_state(dev, "default");
	// 	if (ret && ret != -ENOSYS)
	// 		log_debug("Device '%s' failed to configure default pinctrl: %d (%s)\n",
	// 			  dev->name, ret, errno_str(ret));
	// }

	// ret = device_notify(dev, EVT_DM_POST_PROBE);
	// if (ret)
	// 	goto fail_event;

	return 0;
// fail_event:
// fail_uclass:
// 	if (device_remove(dev, DM_REMOVE_NORMAL)) {
// 		dm_warn("%s: Device '%s' failed to remove on error path\n",
// 			__func__, dev->name);
// 	}
fail:
// 	dev_bic_flags(dev, DM_FLAG_ACTIVATED);

// 	device_free(dev);

	return ret;
}

pci_dev_t dm_pci_get_bdf(const struct pci_device *dev)
{
	/*
	 * This error indicates that @dev is a device on an unprobed PCI bus.
	 * The bus likely has bus=seq == -1, so the PCI_ADD_BUS() macro below
	 * will produce a bad BDF>
	 *
	 * A common cause of this problem is that this function is called in the
	 * of_to_plat() method of @dev. Accessing the PCI bus in that
	 * method is not allowed, since it has not yet been probed. To fix this,
	 * move that access to the probe() method of @dev instead.
	 */
	//TODO check once probing is done if this is correct
	 if(dev->bus->bus_number == -1)
	{
		Kprintf("PCI: Device '%s' on unprobed bus '%s'\n", dev->name, dev->bus->name);
	}
	//TODO ensure bus numbers are assigned
	return PCI_ADD_BUS(dev->bus->bus_number, dev->devfn);
}

/**
 * pci_get_bus_max() - returns the bus number of the last active bus
 *
 * Return: last bus number, or -1 if no active buses
 */
static int pci_get_bus_max(void)
{
	int ret = -1;

	for (struct pci_bus *bus = root_bus; bus; bus = bus->next)
	{
		if (bus->bus_number > ret)
			ret = bus->bus_number;
	}
	return ret;
}

int pci_auto_config_devices(struct pci_bus *bus)
{
	unsigned int sub_bus;

	//sub_bus = dev_seq(bus);
	sub_bus = bus->bus_number;

	Kprintf("%s: start\n", __func__);
	pciauto_config_init(bus->controller);
	for (struct pci_device *device = bus->devices; device; device = device->next)
	{
		unsigned int max_bus;
		int ret;

		Kprintf("%s: device %s\n", __func__, device->name);
		ret = dm_pciauto_config_device(device);
		if (ret < 0)
		{
			Kprintf("%s: Failed to configure device %s: %d\n", __func__, device->name, ret);
			return ret;
		}
		max_bus = ret;
		sub_bus = sub_bus < max_bus ? max_bus : sub_bus;
	}

	if (bus->last_busno < sub_bus)
		bus->last_busno = sub_bus;
	Kprintf("%s: done\n", __func__);

	Kprintf("%s: sub bus = %d\n", __func__, sub_bus);
	return sub_bus;
}

int dm_pci_hose_probe_bus(struct pci_bus *bus)
{
	UBYTE header_type;
	int sub_bus;
	int ret;

	Kprintf("%s\n", __func__);

	dm_pci_read_config8(bus->pci_bridge, PCI_HEADER_TYPE, &header_type);
	header_type &= 0x7f;
	if (header_type != PCI_HEADER_TYPE_BRIDGE)
	{
		Kprintf("%s: Skipping PCI device %d with Non-Bridge Header Type 0x%x\n",
				__func__, PCI_DEV(dm_pci_get_bdf(bus->pci_bridge)), header_type);
		return -EINVAL;
	}

	sub_bus = pci_get_bus_max() + 1;
	Kprintf("%s: bus = %d/%s\n", __func__, sub_bus, bus->name);
	dm_pciauto_prescan_setup_bridge(bus, sub_bus);

	ret = device_probe(bus->pci_bridge);
	if (ret)
	{
		Kprintf("%s: Cannot probe bus %s: %d\n", __func__, bus->name,
				ret);
		return ret;
	}

	sub_bus = pci_get_bus_max();

	dm_pciauto_postscan_setup_bridge(bus, sub_bus);

	return sub_bus;
}

static int device_bind_common(struct pci_bus *parent, 
			      STRPTR name,
			      struct pci_device **devp)
{
	struct pci_device *dev;
	// int size, ret = 0;

	if (devp)
		*devp = NULL;
	if (!name)
		return -EINVAL;

	dev = AllocMem(sizeof(struct pci_device), MEMF_CLEAR);
	if (!dev)
		return -ENOMEM;


	dev->bus = parent;


	// dev->seq_ = uclass_find_next_free_seq(uc);

	// ret = uclass_bind_device(dev);
	// if (ret)
	// 	goto fail_uclass_bind;


	if (devp)
		*devp = dev;

	// dev_or_flags(dev, DM_FLAG_BOUND);

	return 0;

// fail_uclass_post_bind:
// 	/* There is no child unbind() method, so no clean-up required */
// fail_child_post_bind:

// fail_bind:
// 	if (CONFIG_IS_ENABLED(DM_DEVICE_REMOVE)) {
// 		if (uclass_unbind_device(dev)) {
// 			dm_warn("Failed to unbind dev '%s' on error path\n",
// 				dev->name);
// 		}
// 	}
// fail_uclass_bind:
// 	if (CONFIG_IS_ENABLED(DM_DEVICE_REMOVE)) {
// 		list_del(&dev->sibling_node);
// 		if (dev_get_flags(dev) & DM_FLAG_ALLOC_PARENT_PDATA) {
// 			free(dev_get_parent_plat(dev));
// 			dev_set_parent_plat(dev, NULL);
// 		}
// 	}
// fail_alloc3:
// 	if (CONFIG_IS_ENABLED(DM_DEVICE_REMOVE)) {
// 		if (dev_get_flags(dev) & DM_FLAG_ALLOC_UCLASS_PDATA) {
// 			free(dev_get_uclass_plat(dev));
// 			dev_set_uclass_plat(dev, NULL);
// 		}
// 	}
// fail_alloc2:
// 	if (CONFIG_IS_ENABLED(DM_DEVICE_REMOVE)) {
// 		if (dev_get_flags(dev) & DM_FLAG_ALLOC_PDATA) {
// 			free(dev_get_plat(dev));
// 			dev_set_plat(dev, NULL);
// 		}
// 	}
// fail_alloc1:
// 	devres_release_all(dev);

// 	free(dev);

// 	return ret;
}

/**
 * pci_find_and_bind_driver() - Find and bind the right PCI driver
 *
 * This only looks at certain fields in the descriptor.
 *
 * @parent:	Parent bus
 * @bdf:	Bus/device/function addreess - see PCI_BDF()
 * @devp:	Returns a pointer to the device created
 * Return: 0 if OK, -EPERM if the device is not needed before relocation and
 *	   therefore was not created, other -ve value on error
 */
static int pci_find_and_bind_driver(struct pci_bus *parent, pci_dev_t bdf, struct pci_device **devp)
{
	int ret;
	*devp = NULL;

	/* Bind a generic driver so that the device can be used */
	// TODO free later
	STRPTR name = AllocMem(30, MEMF_CLEAR);
	if (!name)
		return -ENOMEM;
	SNPrintf(name, 30, (CONST_STRPTR) "pci_%lx:%lx.%lx", parent->bus_number, PCI_DEV(bdf), PCI_FUNC(bdf));

	ret = device_bind_common(parent, name, devp);
	if (ret)
	{
		Kprintf("%s: Failed to bind generic driver: %d\n", __func__, ret);
		FreeMem(name, 30);
		return ret;
	}
	Kprintf("%s: No match found: bound generic driver instead\n", __func__);

	return 0;
}


int pci_bind_bus_devices(struct pci_bus *bus)
{
	ULONG vendor, device;
	ULONG header_type;
	pci_dev_t bdf, end;
	BOOL found_multi;
	int ret;

	found_multi = FALSE;
	end = PCI_BDF(bus->bus_number, PCI_MAX_PCI_DEVICES - 1,
				  PCI_MAX_PCI_FUNCTIONS - 1);
	for (bdf = PCI_BDF(bus->bus_number, 0, 0); bdf <= end;
		 bdf += PCI_BDF(0, 0, 1))
	{
		struct pci_device *dev;
		ULONG class;

		if (!PCI_FUNC(bdf))
			found_multi = FALSE;
		if (PCI_FUNC(bdf) && !found_multi)
			continue;

		/* Check only the first access, we don't expect problems */
		ret = brcm_pcie_read_config(bus->controller, bdf, PCI_VENDOR_ID, &vendor,
									PCI_SIZE_16);
		if (ret || vendor == 0xffff || vendor == 0x0000)
			continue;

		brcm_pcie_read_config(bus->controller, bdf, PCI_HEADER_TYPE,
							  &header_type, PCI_SIZE_8);

		if (!PCI_FUNC(bdf))
			found_multi = header_type & 0x80;

		Kprintf("%s: bus %d/%s: found device %x, function %d", __func__,
				bus->bus_number, bus->name, PCI_DEV(bdf), PCI_FUNC(bdf));
		brcm_pcie_read_config(bus->controller, bdf, PCI_DEVICE_ID, &device,
							  PCI_SIZE_16);
		brcm_pcie_read_config(bus->controller, bdf, PCI_CLASS_REVISION, &class,
							  PCI_SIZE_32);
		class >>= 8;

		/* Find this device in the device tree */
		ret = pci_bus_find_devfn(bus, PCI_MASK_BUS(bdf), &dev);
		Kprintf(": find ret=%d\n", ret);

		/* If nothing in the device tree, bind a device */
		if (ret == -ENODEV)
		{
			ret = pci_find_and_bind_driver(bus, bdf, &dev);
		}
		else
		{
			Kprintf("device: %s\n", dev->name);
		}
		if (ret == -EPERM)
			continue;
		else if (ret)
			return ret;

		/* Update the platform data */
		dev->devfn = PCI_MASK_BUS(bdf);
		dev->vendor = vendor;
		dev->device = device;
		dev->class = class;
	}

	return 0;
}

static int pci_uclass_pre_probe(struct pci_bus *bus)
{
	Kprintf("%s, bus=%d/%s, parent=%s\n", __func__, bus->bus_number, bus->name, bus->parent->name);

	/*
	 * Set the sequence number, if device_bind() doesn't. We want control
	 * of this so that numbers are allocated as devices are probed. That
	 * ensures that sub-bus numbered is correct (sub-buses must get numbers
	 * higher than their parents)
	 */
	if (bus->bus_number == -1)
	{
		bus->bus_number = pci_get_bus_max()+1;
	}

	/* For bridges, use the top-level PCI controller */
	if (bus->parent != NULL)
	{
		struct pci_controller *parent_hose;

		parent_hose = bus->parent->controller;
		bus->controller = parent_hose;
	}

	bus->first_busno = bus->bus_number;
	bus->last_busno = bus->bus_number;

	return 0;
}

static int pci_uclass_post_probe(struct pci_bus *bus)
{
	Kprintf("%s: probing bus %d\n", __func__, bus->bus_number);
	int ret = pci_bind_bus_devices(bus);
	if (ret)
	{
		Kprintf("%s: failed to bind devices on bus %d\n", __func__, bus->bus_number);
		return ret;
	}

	ret = pci_auto_config_devices(bus);
	if (ret < 0)
	{
		Kprintf("%s: failed to configure devices on bus %d\n", __func__, bus->bus_number);
		return ret;
	}

	return 0;
}
