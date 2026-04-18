// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>
#include <pci_capability.h>
#include <pci_io.h>

static u32 pci_find_next_capability(struct pci_device *dev, u32 pos, u8 cap)
{
	u8 ttl = PCI_FIND_CAP_TTL;

	u8 pos_byte;
	pci_read_config8(dev, pos, &pos_byte);
	pos = pos_byte;

	while (ttl--)
	{
		if (pos < PCI_STD_HEADER_SIZEOF)
			break;
		pos &= ~3u;
		u16 ent;
		pci_read_config16(dev, pos, &ent);

		u32 id = ent & 0xff;
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = (ent >> 8);
	}

	return 0;
}


u32 pci_find_capability(struct pci_device *dev, u8 cap)
{
	u16 status;
	u8 header_type;
	u8 pos;

	pci_read_config16(dev, PCI_STATUS, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	pci_read_config8(dev, PCI_HEADER_TYPE, &header_type);
	if ((header_type & 0x7f) == PCI_HEADER_TYPE_CARDBUS)
		pos = PCI_CB_CAPABILITY_LIST;
	else
		pos = PCI_CAPABILITY_LIST;

	return pci_find_next_capability(dev, pos, cap);
}

static u32 pci_find_next_ext_capability(struct pci_device *dev, u32 start, u16 cap)
{
	u16 pos = PCI_CFG_SPACE_SIZE;

	/* minimum 8 bytes per capability */
	u16 ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	if (start)
		pos = (u16)start;

	u32 header;
	pci_read_config32(dev, pos, &header);
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

		pci_read_config32(dev, pos, &header);
	}

	return 0;
}

u32 pci_find_ext_capability(struct pci_device *dev, u16 cap)
{
	return pci_find_next_ext_capability(dev, 0, cap);
}
