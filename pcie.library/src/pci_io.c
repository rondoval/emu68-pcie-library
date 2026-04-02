// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>
 
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

ULONG dm_pci_read_bar32(const struct pci_device *dev, int barnum)
{
	ULONG addr;

	int bar = PCI_BASE_ADDRESS_0 + barnum * 4;
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