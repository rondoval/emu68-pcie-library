// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */
#include <pci.h>
#include <pcie_brcmstb.h>
#include <pci_util.h>
#include <pci_io.h>
 
s32 pci_bus_clrset_config32(struct pci_bus *bus, pci_dev_t bdf, u32 offset, u32 clr, u32 set)
{
	struct pci_controller *ctlr = pci_get_controller(bus);

	u32 val;
	s32 ret = brcm_pcie_read_config(ctlr, bdf, offset, &val, PCI_SIZE_32);
	if (ret)
		return ret;

	val &= ~clr;
	val |= set;

	return brcm_pcie_write_config(ctlr, bdf, offset, val, PCI_SIZE_32);
}

s32 pci_write_config(struct pci_device *dev, u32 offset, u32 value, enum pci_size_t size)
{
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	return brcm_pcie_write_config(ctlr, pci_get_bdf(dev), offset, value, size);
}

s32 pci_write_config8(struct pci_device *dev, u32 offset, u32 value)
{
	return pci_write_config(dev, offset, value, PCI_SIZE_8);
}

s32 pci_write_config16(struct pci_device *dev, u32 offset, u32 value)
{
	return pci_write_config(dev, offset, value, PCI_SIZE_16);
}

s32 pci_write_config32(struct pci_device *dev, u32 offset, u32 value)
{
	return pci_write_config(dev, offset, value, PCI_SIZE_32);
}

s32 pci_read_config(const struct pci_device *dev, u32 offset, u32 *valuep, enum pci_size_t size)
{
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	return brcm_pcie_read_config(ctlr, pci_get_bdf(dev), offset, valuep, size);
}

s32 pci_read_config8(const struct pci_device *dev, u32 offset, u8 *valuep)
{
	u32 value;
	s32 ret = pci_read_config(dev, offset, &value, PCI_SIZE_8);
	if (ret)
	{
		*valuep = 0xff;
		return ret;
	}
	*valuep = (u8)value;

	return 0;
}

s32 pci_read_config16(const struct pci_device *dev, u32 offset, u16 *valuep)
{
	u32 value;
	s32 ret = pci_read_config(dev, offset, &value, PCI_SIZE_16);
	if (ret)
	{
		*valuep = 0xffff;
		return ret;
	}
	*valuep = (u16)value;

	return 0;
}

s32 pci_read_config32(const struct pci_device *dev, u32 offset, u32 *valuep)
{
	u32 value;
	s32 ret = pci_read_config(dev, offset, &value, PCI_SIZE_32);
	if (ret)
	{
		*valuep = 0xffffffff;
		return ret;
	}
	*valuep = value;

	return 0;
}

s32 pci_clrset_config8(struct pci_device *dev, u32 offset, u32 clr, u32 set)
{
	u8 raw;
	s32 ret = pci_read_config8(dev, offset, &raw);
	if (ret)
		return ret;

	u32 val = raw;
	val &= ~clr;
	val |= set;

	return pci_write_config8(dev, offset, val);
}

s32 pci_clrset_config16(struct pci_device *dev, u32 offset, u32 clr, u32 set)
{
	u16 raw;
	s32 ret = pci_read_config16(dev, offset, &raw);
	if (ret)
		return ret;

	u32 val = raw;
	val &= ~clr;
	val |= set;

	return pci_write_config16(dev, offset, val);
}

s32 pci_clrset_config32(struct pci_device *dev, u32 offset, u32 clr, u32 set)
{
	u32 val;
	s32 ret;

	ret = pci_read_config32(dev, offset, &val);
	if (ret)
		return ret;

	val &= ~clr;
	val |= set;

	return pci_write_config32(dev, offset, val);
}

u32 pci_read_bar32(const struct pci_device *dev, u32 barnum)
{
	u32 bar = PCI_BASE_ADDRESS_0 + barnum * 4;
	u32 addr;
	pci_read_config32(dev, bar, &addr);

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

void pci_write_bar32(struct pci_device *dev, u32 barnum, u32 addr)
{
	u32 bar = PCI_BASE_ADDRESS_0 + barnum * 4;
	pci_write_config32(dev, bar, addr);
}