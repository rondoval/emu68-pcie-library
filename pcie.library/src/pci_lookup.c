#include <pci.h>

extern struct MinList pci_bus_list;

int pci_bus_find_devfn(const struct pci_bus *bus, pci_dev_t find_devfn, struct pci_device **devp)
{
	for (struct MinNode *node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_device *dev = (struct pci_device *)node;
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
	for (struct MinNode *node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_device *dev = (struct pci_device *)node;
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
	for (struct MinNode *node = pci_bus_list.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_bus *bus = (struct pci_bus *)node;
		if (!pci_bus_find_devices(bus, ids, &index, devp))
			return 0;
	}
	*devp = NULL;

	return -ENODEV;
}

static int dm_pci_bus_find_device(struct pci_bus *bus, unsigned int vendor, unsigned int device, int *indexp, struct pci_device **devp)
{
	for (struct MinNode *node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_device *dev = (struct pci_device *)node;
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
	for (struct MinNode *node = pci_bus_list.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_bus *bus = (struct pci_bus *)node;
		if (!dm_pci_bus_find_device(bus, vendor, device, &index, devp))
			return 0;
	}
	*devp = NULL;

	return -ENODEV;
}

int dm_pci_find_class(UWORD find_class, int index, struct pci_device **devp)
{
	for (struct MinNode *node = pci_bus_list.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_bus *bus = (struct pci_bus *)node;
		for(struct MinNode *dnode = bus->devices.mlh_Head; dnode->mln_Succ; dnode = dnode->mln_Succ)
		{
			struct pci_device *dev = (struct pci_device *)dnode;
			if (dev->class == find_class && !index--)
			{
				*devp = dev;
				return 0;
			}
		}
	}
	*devp = NULL;

	return -ENODEV;
}

static int skip_to_next_device(struct pci_bus *bus, struct pci_device **devp)
{
	struct pci_device *dev;

	/*
	 * Scan through all the PCI controllers. On x86 there will only be one
	 * but that is not necessarily true on other hardware.
	 */
	for (; bus->node.mln_Succ; bus = (struct pci_bus *)bus->node.mln_Succ)
	{
		dev = (struct pci_device *)bus->devices.mlh_Head;
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
	if (child->node.mln_Succ)
	{
		*devp = (struct pci_device *)child->node.mln_Succ;
		return 0;
	}

	bus = (struct pci_bus *)bus->node.mln_Succ;
	return bus ? skip_to_next_device(bus, devp) : 0;
}

int pci_find_first_device(struct pci_device **devp)
{
	*devp = NULL;
	return skip_to_next_device((struct pci_bus *)pci_bus_list.mlh_Head, devp);
}
