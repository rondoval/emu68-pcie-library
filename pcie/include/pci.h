/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * (C) Copyright 2001 Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Andreas Heppel <aheppel@sysgo.de>
 *
 * (C) Copyright 2002
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 * Copyright (c) 2021  Maciej W. Rozycki <macro@orcam.me.uk>
 */

#ifndef _PCI_H
#define _PCI_H

#include <exec/types.h>
#include <bits.h>
#include <pci_types.h>
#include <libraries/pci_ids.h>

#include <libraries/pci_constants.h>

/*
 * Config Address for PCI Configuration Mechanism #1
 *
 * See PCI Local Bus Specification, Revision 3.0,
 * Section 3.2.2.3.2, Figure 3-2, p. 50.
 */

#define PCI_CONF1_BUS_SHIFT 16 /* Bus number */
#define PCI_CONF1_DEV_SHIFT 11 /* Device number */
#define PCI_CONF1_FUNC_SHIFT 8 /* Function number */

#define PCI_CONF1_BUS_MASK 0xff
#define PCI_CONF1_DEV_MASK 0x1f
#define PCI_CONF1_FUNC_MASK 0x7
#define PCI_CONF1_REG_MASK 0xfc /* Limit aligned offset to a maximum of 256B */

#define PCI_CONF1_ENABLE BIT(31)
#define PCI_CONF1_BUS(x) (((x) & PCI_CONF1_BUS_MASK) << PCI_CONF1_BUS_SHIFT)
#define PCI_CONF1_DEV(x) (((x) & PCI_CONF1_DEV_MASK) << PCI_CONF1_DEV_SHIFT)
#define PCI_CONF1_FUNC(x) (((x) & PCI_CONF1_FUNC_MASK) << PCI_CONF1_FUNC_SHIFT)
#define PCI_CONF1_REG(x) ((x) & PCI_CONF1_REG_MASK)

#define PCI_CONF1_ADDRESS(bus, dev, func, reg) \
	(PCI_CONF1_ENABLE |                        \
	 PCI_CONF1_BUS(bus) |                      \
	 PCI_CONF1_DEV(dev) |                      \
	 PCI_CONF1_FUNC(func) |                    \
	 PCI_CONF1_REG(reg))

/*
 * Extension of PCI Config Address for accessing extended PCIe registers
 *
 * No standardized specification, but used on lot of non-ECAM-compliant ARM SoCs
 * or on AMD Barcelona and new CPUs. Reserved bits [27:24] of PCI Config Address
 * are used for specifying additional 4 high bits of PCI Express register.
 */

#define PCI_CONF1_EXT_REG_SHIFT 16
#define PCI_CONF1_EXT_REG_MASK 0xf00
#define PCI_CONF1_EXT_REG(x) (((x) & PCI_CONF1_EXT_REG_MASK) << PCI_CONF1_EXT_REG_SHIFT)

#define PCI_CONF1_EXT_ADDRESS(bus, dev, func, reg) \
	(PCI_CONF1_ADDRESS(bus, dev, func, reg) |      \
	 PCI_CONF1_EXT_REG(reg))

/*
 * Enhanced Configuration Access Mechanism (ECAM)
 *
 * See PCI Express Base Specification, Revision 5.0, Version 1.0,
 * Section 7.2.2, Table 7-1, p. 677.
 */
#define PCIE_ECAM_BUS_SHIFT 20	/* Bus number */
#define PCIE_ECAM_DEV_SHIFT 15	/* Device number */
#define PCIE_ECAM_FUNC_SHIFT 12 /* Function number */

#define PCIE_ECAM_BUS_MASK 0xffu
#define PCIE_ECAM_DEV_MASK 0x1fu
#define PCIE_ECAM_FUNC_MASK 0x7u
#define PCIE_ECAM_REG_MASK 0xfffu /* Limit offset to a maximum of 4K */

#define PCIE_ECAM_BUS(x) (((x) & PCIE_ECAM_BUS_MASK) << PCIE_ECAM_BUS_SHIFT)
#define PCIE_ECAM_DEV(x) (((x) & PCIE_ECAM_DEV_MASK) << PCIE_ECAM_DEV_SHIFT)
#define PCIE_ECAM_FUNC(x) (((x) & PCIE_ECAM_FUNC_MASK) << PCIE_ECAM_FUNC_SHIFT)
#define PCIE_ECAM_REG(x) ((x) & PCIE_ECAM_REG_MASK)

#define PCIE_ECAM_OFFSET(bus, dev, func, where) \
	(PCIE_ECAM_BUS(bus) |                       \
	 PCIE_ECAM_DEV(dev) |                       \
	 PCIE_ECAM_FUNC(func) |                     \
	 PCIE_ECAM_REG(where))

static inline void pci_set_region(struct pci_region *reg,
								  pci_addr_t bus_start,
								  phys_addr_t phys_start,
								  pci_size_t size,
								  u32 flags)
{
	reg->bus_start = bus_start;
	reg->phys_start = phys_start;
	reg->size = size;
	reg->flags = flags;
}

#define PCI_BUS(d) ((UBYTE)((d) >> 16))

/*
 * Please note the difference in DEVFN usage in U-Boot vs Linux. U-Boot
 * uses DEVFN in bits 15-8 but Linux instead expects DEVFN in bits 7-0.
 * Please see the Linux header include/uapi/linux/pci.h for more details.
 * This is relevant for the following macros:
 * PCI_DEV, PCI_FUNC, PCI_DEVFN
 * The U-Boot macro PCI_DEV is equivalent to the Linux PCI_SLOT version with
 * the remark from above (input is in bits 15-8 instead of 7-0.
 */
#define PCI_DEV(d) (((d) >> 11) & 0x1f)
#define PCI_FUNC(d) (((d) >> 8) & 0x7)
#define PCI_DEVFN(d, f) ((d) << 11 | (f) << 8)

#define PCI_MASK_BUS(bdf) ((bdf) & 0xffff)
#define PCI_ADD_BUS(bus, devfn) (((bus) << 16) | (devfn))
#define PCI_BDF(b, d, f) ((b) << 16 | PCI_DEVFN(d, f))
#define PCI_ANY_ID (~0)

/* Convert from Linux format to U-Boot format */
#define PCI_TO_BDF(val) ((val) << 8)

#define INDIRECT_TYPE_NO_PCIE_LINK 1

/*
 * Function declarations have been moved to per-unit headers:
 *   pci_io.h         — config space read/write, BAR register helpers
 *   pci_bar.h        — address translation (bus<->phys<->virt), pci_map_bar
 *   pci_capability.h — PCI/PCIe capability list search
 *   pci_lookup.h     — device lookup by BDF, vendor/device ID, class
 *   pci_probe.h      — bus/device creation and probe
 *   pci_util.h       — BDF accessor, region helpers, pci_flr, pci_conv_*
 *   pci_int.h        — INTx enable/disable, IRQ assignment
 *   pci_msi.h        — MSI setup, teardown, and interrupt registration
 * Include the relevant headers directly instead of relying on this file.
 */

/**
 * pci_get_bdf() - Get the BDF value for a device
 *
 * @dev:	Device to check
 * Return: bus/device/function value (see PCI_BDF())
 */

/**
 * PCI_DEVICE - macro used to describe a specific PCI device
 * @vend: the 16 bit PCI Vendor ID
 * @dev:  the 16 bit PCI Device ID
 *
 * Creates a struct pci_device_id that matches @vend and @dev.  Subvendor and
 * subdevice are set to PCI_ANY_ID.
 */
#define PCI_DEVICE(vend, dev)          \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_DEVICE_SUB - macro used to describe a PCI device with subsystem IDs
 * @vend:    16-bit PCI Vendor ID
 * @dev:     16-bit PCI Device ID
 * @subvend: 16-bit PCI Subsystem Vendor ID
 * @subdev:  16-bit PCI Subsystem Device ID
 */
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev),             \
	.subvendor = (subvend), .subdevice = (subdev)

/**
 * PCI_DEVICE_CLASS - macro used to match a PCI device by class code
 * @dev_class:      the class/subclass/prog-if triple
 * @dev_class_mask: mask applied to the class field before comparison
 *
 * Vendor, device, subvendor, and subdevice are set to PCI_ANY_ID.
 */
#define PCI_DEVICE_CLASS(dev_class, dev_class_mask)       \
	.class = (dev_class), .class_mask = (dev_class_mask), \
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID,           \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_VDEVICE - short-form macro to match a specific PCI device
 * @vend: vendor name token used to expand PCI_VENDOR_ID_##vend
 * @dev:  16-bit PCI Device ID
 *
 * Subvendor and subdevice are set to PCI_ANY_ID.
 */
#define PCI_VDEVICE(vend, dev)                       \
	.vendor = PCI_VENDOR_ID_##vend, .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0

#endif /* _PCI_H */
