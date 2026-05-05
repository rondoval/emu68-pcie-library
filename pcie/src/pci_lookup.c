/* SPDX-License-Identifier: GPL-2.0-only */

#include <pci.h>
#include <errors.h>
#include <pci_lookup.h>
#include <pci_util.h>

s32 pci_bus_find_devfn(const struct pci_bus *bus, pci_dev_t find_devfn, struct pci_device **devp)
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

static BOOL pci_device_matches_ids(struct pci_device *dev, const struct pci_device_id *ids)
{
	for (u32 i = 0; ids[i].vendor != 0; i++)
	{
		if (dev->vendor == ids[i].vendor &&
			dev->device == ids[i].device)
			return TRUE;
	}

	return FALSE;
}

s32 pci_bus_find_devices(struct pci_bus *bus, const struct pci_device_id *ids, int *indexp, struct pci_device **devp)
{
	for (struct MinNode *node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
	{
		struct pci_device *dev = (struct pci_device *)node;
		if (pci_device_matches_ids(dev, ids))
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

