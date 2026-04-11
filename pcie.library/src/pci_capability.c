// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>

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
