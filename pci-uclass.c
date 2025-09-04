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

struct pci_bus *root_bus;
#include <compat.h>
#include <pcie_brcmstb.h>

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
	for (; bus->parent; bus = bus->parent)
		;

	return bus->controller;
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

int pci_last_busno(void)
{
	return pci_get_bus_max();
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

int pci_bus_find_devfn(const struct pci_bus *bus, pci_dev_t find_devfn, struct pci_device **devp)
{
	for (struct pci_device *dev = bus->devices; dev; dev = dev->next)
	{
		if (dev->devfn == find_devfn)
		{
			*devp = dev;
			return 0;
		}
	}
	return -ENODEV;
}

int dm_pci_bus_find_bdf(pci_dev_t bdf, struct pci_device **devp)
{
	struct pci_bus *bus;
	int ret;

	ret = pci_get_bus(PCI_BUS(bdf), &bus);
	if (ret)
		return ret;
	return pci_bus_find_devfn(bus, PCI_MASK_BUS(bdf), devp);
}

static int pci_device_matches_ids(struct pci_device *dev, const struct pci_device_id *ids)
{
	for (int i = 0; ids[i].vendor != 0; i++)
	{
		if (dev->vendor == ids[i].vendor &&
			dev->device == ids[i].device)
			return i;
	}

	return -EINVAL;
}

int pci_bus_find_devices(struct pci_bus *bus, const struct pci_device_id *ids, int *indexp, struct pci_device **devp)
{
	for (struct pci_device *dev = bus->devices; dev; dev = dev->next)
	{
		if (pci_device_matches_ids(dev, ids) >= 0)
		{
			if ((*indexp)-- <= 0)
			{
				*devp = dev;
				return 0;
			}
		}
	}
	return -ENODEV;
}

int pci_find_device_id(const struct pci_device_id *ids, int index, struct pci_device **devp)
{
	for (struct pci_bus *bus = root_bus; bus; bus = bus->next)
	{
		if (!pci_bus_find_devices(bus, ids, &index, devp))
			return 0;
	}
	*devp = NULL;

	return -ENODEV;
}

static int dm_pci_bus_find_device(struct pci_bus *bus, unsigned int vendor, unsigned int device, int *indexp, struct pci_device **devp)
{
	for (struct pci_device *dev = bus->devices; dev; dev = dev->next)
	{
		if (dev->vendor == vendor && dev->device == device)
		{
			if (!(*indexp)--)
			{
				*devp = dev;
				return 0;
			}
		}
	}

	return -ENODEV;
}

int dm_pci_find_device(unsigned int vendor, unsigned int device, int index, struct pci_device **devp)
{
	for (struct pci_bus *bus = root_bus; bus; bus = bus->next)
	{
		if (!dm_pci_bus_find_device(bus, vendor, device, &index, devp))
			return device_probe(*devp);
	}
	*devp = NULL;

	return -ENODEV;
}

int dm_pci_find_class(UWORD find_class, int index, struct pci_device **devp)
{
	for (struct pci_bus *bus = root_bus; bus; bus = bus->next)
	{
		for (struct pci_device *dev = bus->devices; dev; dev = dev->next)
		{
			if (dev->class == find_class && !index--)
			{
				*devp = dev;
				return device_probe(*devp);
			}
		}
	}
	*devp = NULL;

	return -ENODEV;
}

int pci_bus_clrset_config32(struct pci_bus *bus, pci_dev_t bdf, int offset, ULONG clr, ULONG set)
{
	struct pci_controller *ctlr = pci_get_controller(bus);

	ULONG val;
	int ret;

	ret = brcm_pcie_read_config(ctlr, bdf, offset, &val, PCI_SIZE_32);
	if (ret)
		return ret;
	val &= ~clr;
	val |= set;

	return brcm_pcie_write_config(ctlr, bdf, offset, val, PCI_SIZE_32);
}

int dm_pci_write_config(struct pci_device *dev, int offset, ULONG value, enum pci_size_t size)
{
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	return brcm_pcie_write_config(ctlr, dm_pci_get_bdf(dev), offset, value, size);
}

int dm_pci_write_config8(struct pci_device *dev, int offset, UBYTE value)
{
	return dm_pci_write_config(dev, offset, value, PCI_SIZE_8);
}

int dm_pci_write_config16(struct pci_device *dev, int offset, UWORD value)
{
	return dm_pci_write_config(dev, offset, value, PCI_SIZE_16);
}

int dm_pci_write_config32(struct pci_device *dev, int offset, ULONG value)
{
	return dm_pci_write_config(dev, offset, value, PCI_SIZE_32);
}

int dm_pci_read_config(const struct pci_device *dev, int offset, ULONG *valuep, enum pci_size_t size)
{
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	return brcm_pcie_read_config(ctlr, dm_pci_get_bdf(dev), offset, valuep, size);
}

int dm_pci_read_config8(const struct pci_device *dev, int offset, UBYTE *valuep)
{
	ULONG value;
	int ret;

	ret = dm_pci_read_config(dev, offset, &value, PCI_SIZE_8);
	if (ret)
	{
		*valuep = 0xff;
		return ret;
	}
	*valuep = value;

	return 0;
}

int dm_pci_read_config16(const struct pci_device *dev, int offset, UWORD *valuep)
{
	ULONG value;
	int ret;

	ret = dm_pci_read_config(dev, offset, &value, PCI_SIZE_16);
	if (ret)
	{
		*valuep = 0xffff;
		return ret;
	}
	*valuep = value;

	return 0;
}

int dm_pci_read_config32(const struct pci_device *dev, int offset, ULONG *valuep)
{
	ULONG value;
	int ret;

	ret = dm_pci_read_config(dev, offset, &value, PCI_SIZE_32);
	if (ret)
	{
		*valuep = 0xffffffff;
		return ret;
	}
	*valuep = value;

	return 0;
}

int dm_pci_clrset_config8(struct pci_device *dev, int offset, ULONG clr, ULONG set)
{
	UBYTE val;
	int ret;

	ret = dm_pci_read_config8(dev, offset, &val);
	if (ret)
		return ret;
	val &= ~clr;
	val |= set;

	return dm_pci_write_config8(dev, offset, val);
}

int dm_pci_clrset_config16(struct pci_device *dev, int offset, ULONG clr, ULONG set)
{
	UWORD val;
	int ret;

	ret = dm_pci_read_config16(dev, offset, &val);
	if (ret)
		return ret;
	val &= ~clr;
	val |= set;

	return dm_pci_write_config16(dev, offset, val);
}

int dm_pci_clrset_config32(struct pci_device *dev, int offset, ULONG clr, ULONG set)
{
	ULONG val;
	int ret;

	ret = dm_pci_read_config32(dev, offset, &val);
	if (ret)
		return ret;
	val &= ~clr;
	val |= set;

	return dm_pci_write_config32(dev, offset, val);
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

/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *                        PCI device id structure
 * @id: single PCI device id structure to match
 * @find: the PCI device id structure to match against
 *
 * Returns true if the finding pci_device_id structure matched or FALSE if
 * there is no match.
 */
static BOOL pci_match_one_id(const struct pci_device_id *id,
							 const struct pci_device_id *find)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == find->vendor) &&
		(id->device == PCI_ANY_ID || id->device == find->device) &&
		(id->subvendor == PCI_ANY_ID || id->subvendor == find->subvendor) &&
		(id->subdevice == PCI_ANY_ID || id->subdevice == find->subdevice) &&
		!((id->class ^ find->class) & id->class_mask))
		return TRUE;

	return FALSE;
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

static int decode_regions(struct pci_controller *hose)
{
	int cells_per_record;
	int max_regions;
	int i;

	APTR key = DT_OpenKey(hose->dt_node_name);
	APTR prop = DT_FindProperty(key, (CONST_STRPTR) "ranges");
	if (!prop)
	{
		Kprintf("%s: Cannot find 'ranges' property in device tree\n", __func__);
		DT_CloseKey(key);
		return -EINVAL;
	}

	ULONG *ranges = (ULONG *)DT_GetPropValue(prop);
	int len = DT_GetPropLen(prop);

	ULONG pci_addr_cells = DT_GetPropertyValueULONG(key, "#address-cells", 2, FALSE);
	ULONG addr_cells = DT_GetPropertyValueULONG(DT_GetParent(key), "#address-cells", 2, FALSE);
	ULONG size_cells = DT_GetPropertyValueULONG(key, "#size-cells", 1, FALSE);

	/* PCI addresses are always 3-cells */
	len /= sizeof(ULONG);
	cells_per_record = pci_addr_cells + addr_cells + size_cells;
	hose->region_count = 0;
	Kprintf("%s: len=%d, cells_per_record=%d\n", __func__, len, cells_per_record);

	/* Dynamically allocate the regions array */
	max_regions = len / cells_per_record + CONFIG_NR_DRAM_BANKS;
	// TODO free this mem
	hose->regions = (struct pci_region *)AllocMem(max_regions * sizeof(struct pci_region), MEMF_CLEAR);
	if (!hose->regions)
		return -ENOMEM;

	for (i = 0; i < max_regions; i++, len -= cells_per_record)
	{
		u64 pci_addr, addr, size;
		int space_code;
		ULONG flags;
		int type;
		int pos;

		if (len < cells_per_record)
			break;
		flags = ranges[0]; // TODO BE->LE?
		space_code = (flags >> 24) & 3;
		pci_addr = DT_GetNumber(ranges + 1, 2);
		prop += pci_addr_cells;
		addr = DT_GetNumber(ranges, addr_cells);
		prop += addr_cells;
		size = DT_GetNumber(ranges, size_cells);
		prop += size_cells;
		Kprintf("%s: region %d, pci_addr=%llx, addr=%llx, size=%llx, space_code=%d\n",
				__func__, hose->region_count, pci_addr, addr, size, space_code);
		if (space_code & 2)
		{
			type = flags & (1U << 30) ? PCI_REGION_PREFETCH : PCI_REGION_MEM;
		}
		else if (space_code & 1)
		{
			type = PCI_REGION_IO;
		}
		else
		{
			continue;
		}

#ifndef CONFIG_SYS_PCI_64BIT
		if (type == PCI_REGION_MEM && upper_32_bits(pci_addr))
		{
			Kprintf(" - pci_addr beyond the 32-bit boundary, ignoring\n");
			continue;
		}
#endif

#ifndef CONFIG_PHYS_64BIT
		if (upper_32_bits(addr))
		{
			Kprintf(" - addr beyond the 32-bit boundary, ignoring\n");
			continue;
		}
#endif

		if (~((pci_addr_t)0) - pci_addr < size)
		{
			Kprintf(" - PCI range exceeds max address, ignoring\n");
			continue;
		}

		if (~((phys_addr_t)0) - addr < size)
		{
			Kprintf(" - phys range exceeds max address, ignoring\n");
			continue;
		}

		pos = hose->region_count++;
		Kprintf(" - type=%d, pos=%d\n", type, pos);
		pci_set_region(hose->regions + pos, pci_addr, addr, size, type);
	}

	/* Add a region for our local memory */
	struct MemHeader *mh = (struct MemHeader *)SysBase->MemList.lh_Head;
	for (i = 0; i < CONFIG_NR_DRAM_BANKS && mh != NULL; i++)
	{
		if (mh->mh_Attributes & MEMF_FAST)
		{
			phys_addr_t start = (phys_addr_t)mh->mh_Lower;
			phys_addr_t end = (phys_addr_t)mh->mh_Upper;
			phys_addr_t size = end - start + 1;

			if (size == 0)
				continue;
			int pos = hose->region_count++;
			Kprintf(" - DRAM region %d: start=%llx, size=%llx\n", pos, (u64)start, (u64)size);
#ifdef CONFIG_PCI_MAP_SYSTEM_MEMORY
			start = virt_to_phys((void *)(uintptr_t)bd->bi_dram[i].start);
#endif
			pci_set_region(hose->regions + pos, start, start, size,
						   PCI_REGION_MEM | PCI_REGION_SYS_MEMORY);
		}
		mh = (struct MemHeader *)mh->mh_Node.ln_Succ;
	}

	return 0;
}

static int pci_uclass_pre_probe(struct pci_bus *bus)
{
	int ret;

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
	if (bus->parent == NULL)
	{
		ret = decode_regions(bus->controller);
		if (ret)
			return ret;
	}
	else
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

static int pci_uclass_child_post_bind(struct pci_device *dev)
{
	return 0;
}

static int pci_bridge_read_config(const struct pci_bus *bus, pci_dev_t bdf, UWORD offset, ULONG *valuep, enum pci_size_t size)
{
	return brcm_pcie_read_config(bus->controller, bdf, offset, valuep, size);
}

static int pci_bridge_write_config(struct pci_bus *bus, pci_dev_t bdf, UWORD offset, ULONG value, enum pci_size_t size)
{
	return brcm_pcie_write_config(bus->controller, bdf, offset, value, size);
}

static int skip_to_next_device(struct pci_bus *bus, struct pci_device **devp)
{
	struct pci_device *dev;

	/*
	 * Scan through all the PCI controllers. On x86 there will only be one
	 * but that is not necessarily true on other hardware.
	 */
	for (; bus; bus = bus->next)
	{
		dev = bus->devices;
		if (dev)
		{
			*devp = dev;
			return 0;
		}
	}

	return 0;
}

int pci_find_next_device(struct pci_device **devp)
{
	struct pci_device *child = *devp;
	struct pci_bus *bus = child->bus;

	/* First try all the siblings */
	*devp = NULL;
	if (child->next)
	{
		*devp = child->next;
		return 0;
	}

	bus = bus->next;
	return bus ? skip_to_next_device(bus, devp) : 0;
}

int pci_find_first_device(struct pci_device **devp)
{
	*devp = NULL;
	return skip_to_next_device(root_bus, devp);
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

int pci_get_dma_regions(struct pci_controller *ctlr, struct pci_region *memp, int index)
{
	int cells_per_record;
	int i = 0;

	APTR key = DT_OpenKey(ctlr->dt_node_name);
	APTR prop = DT_FindProperty(key, (CONST_STRPTR) "dma-ranges");
	if (!prop)
	{
		Kprintf("PCI: Device '%s': Cannot decode dma-ranges\n", ctlr->dt_node_name);
		DT_CloseKey(key);
		return -EINVAL;
	}

	ULONG *dma_ranges = (ULONG *)DT_GetPropValue(prop);
	int len = DT_GetPropLen(prop);

	ULONG pci_addr_cells = DT_GetPropertyValueULONG(key, "#address-cells", 2, FALSE);
	ULONG addr_cells = DT_GetPropertyValueULONG(DT_GetParent(key), "#address-cells", 2, FALSE);
	ULONG size_cells = DT_GetPropertyValueULONG(key, "#size-cells", 1, FALSE);

	/* PCI addresses are always 3-cells */
	len /= sizeof(ULONG);
	cells_per_record = pci_addr_cells + addr_cells + size_cells;
	Kprintf("%s: len=%ld, cells_per_record=%ld\n", __func__, len, cells_per_record);

	while (len)
	{
		memp->bus_start = DT_GetNumber(dma_ranges + 1, 2);
		dma_ranges += pci_addr_cells;
		memp->phys_start = DT_GetNumber(dma_ranges, addr_cells);
		dma_ranges += addr_cells;
		memp->size = DT_GetNumber(dma_ranges, size_cells);
		dma_ranges += size_cells;

		if (i == index)
			return 0;
		i++;
		len -= cells_per_record;
	}

	return -EINVAL;
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

ULONG dm_pci_read_bar32(const struct pci_device *dev, int barnum)
{
	ULONG addr;
	int bar;

	bar = PCI_BASE_ADDRESS_0 + barnum * 4;
	dm_pci_read_config32(dev, bar, &addr);

	/*
	 * If we get an invalid address, return this so that comparisons with
	 * FDT_ADDR_T_NONE work correctly
	 */
	if (addr == 0xffffffff)
		return addr;
	else if (addr & PCI_BASE_ADDRESS_SPACE_IO)
		return addr & PCI_BASE_ADDRESS_IO_MASK;
	else
		return addr & PCI_BASE_ADDRESS_MEM_MASK;
}

void dm_pci_write_bar32(struct pci_device *dev, int barnum, ULONG addr)
{
	int bar;

	bar = PCI_BASE_ADDRESS_0 + barnum * 4;
	dm_pci_write_config32(dev, bar, addr);
}

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

	Kprintf("dm_pci_bus_to_phys: invalid physical address\n");
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

	Kprintf("dm_pci_phys_to_bus: invalid physical address\n");
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

static int _dm_pci_find_next_capability(struct pci_device *dev, UBYTE pos, int cap)
{
	int ttl = PCI_FIND_CAP_TTL;
	UBYTE id;
	UWORD ent;

	dm_pci_read_config8(dev, pos, &pos);

	while (ttl--)
	{
		if (pos < PCI_STD_HEADER_SIZEOF)
			break;
		pos &= ~3;
		dm_pci_read_config16(dev, pos, &ent);

		id = ent & 0xff;
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = (ent >> 8);
	}

	return 0;
}

int dm_pci_find_next_capability(struct pci_device *dev, UBYTE start, int cap)
{
	return _dm_pci_find_next_capability(dev, start + PCI_CAP_LIST_NEXT,
										cap);
}

int dm_pci_find_capability(struct pci_device *dev, int cap)
{
	UWORD status;
	UBYTE header_type;
	UBYTE pos;

	dm_pci_read_config16(dev, PCI_STATUS, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	dm_pci_read_config8(dev, PCI_HEADER_TYPE, &header_type);
	if ((header_type & 0x7f) == PCI_HEADER_TYPE_CARDBUS)
		pos = PCI_CB_CAPABILITY_LIST;
	else
		pos = PCI_CAPABILITY_LIST;

	return _dm_pci_find_next_capability(dev, pos, cap);
}

int dm_pci_find_next_ext_capability(struct pci_device *dev, int start, int cap)
{
	ULONG header;
	int ttl;
	int pos = PCI_CFG_SPACE_SIZE;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	if (start)
		pos = start;

	dm_pci_read_config32(dev, pos, &header);
	/*
	 * If we have no capabilities, this is indicated by cap ID,
	 * cap version and next pointer all being 0.
	 */
	if (header == 0)
		return 0;

	while (ttl--)
	{
		if (PCI_EXT_CAP_ID(header) == cap)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < PCI_CFG_SPACE_SIZE)
			break;

		dm_pci_read_config32(dev, pos, &header);
	}

	return 0;
}

int dm_pci_find_ext_capability(struct pci_device *dev, int cap)
{
	return dm_pci_find_next_ext_capability(dev, 0, cap);
}

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
