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
#include <pcie_brcmstb.h>
#include <pci_types.h>
#include <libraries/pci_ids.h>
#include <pci_auto.h>

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

s32 pci_skip_dev(struct pci_controller *hose, pci_dev_t dev);

/**
 * pci_get_bdf() - Get the BDF value for a device
 *
 * @dev:	Device to check
 * Return: bus/device/function value (see PCI_BDF())
 */
pci_dev_t pci_get_bdf(const struct pci_device *dev);

/**
 * pci_bind_bus_devices() - scan a PCI bus and bind devices
 *
 * Scan a PCI bus looking for devices. Bind each one that is found. If
 * devices are already bound that match the scanned devices, just update the
 * child data so that the device can be used correctly (this happens when
 * the device tree describes devices we expect to see on the bus).
 *
 * Devices that are bound in this way will use a generic PCI driver which
 * does nothing. The device can still be accessed but will not provide any
 * driver interface.
 *
 * @bus:	Bus containing devices to bind
 * Return: 0 if OK, -ve on error
 */
s32 pci_bind_bus_devices(struct pci_bus *bus);

/**
 * pci_auto_config_devices() - configure bus devices ready for use
 *
 * This works through all devices on a bus by scanning the driver model
 * data structures (normally these have been set up by pci_bind_bus_devices()
 * earlier).
 *
 * Space is allocated for each PCI base address register (BAR) so that the
 * devices are mapped into memory and I/O space ready for use.
 *
 * @bus:	Bus containing devices to bind
 * Return: 0 if OK, -ve on error
 */
s32 pci_auto_config_devices(struct pci_bus *bus);

/**
 * pci_bus_find_bdf() - Find a device given its PCI bus address
 *
 * @bdf:	PCI device address: bus, device and function -see PCI_BDF()
 * @devp:	Returns the device for this address, if found
 * Return: 0 if OK, -ENODEV if not found
 */
s32 pci_bus_find_bdf(struct pci_controller *controller, pci_dev_t bdf, struct pci_device **devp);

/**
 * pci_bus_find_devfn() - Find a device on a bus
 *
 * @find_devfn:		PCI device address (device and function only)
 * @devp:	Returns the device for this address, if found
 * Return: 0 if OK, -ENODEV if not found
 */
s32 pci_bus_find_devfn(const struct pci_bus *bus, pci_dev_t find_devfn,
					   struct pci_device **devp);

/**
 * pci_find_first_device() - return the first available PCI device
 *
 * This function and pci_find_next_device() allow iteration through all
 * available PCI devices on all buses. Assuming there are any, this will
 * return the first one.
 *
 * @devp:	Set to the first available device, or NULL if no more are left
 *		or we got an error
 * Return: 0 if all is OK, -ve on error (e.g. a bus/bridge failed to probe)
 */
s32 pci_find_first_device(struct pci_controller *controller, struct pci_device **devp);

/**
 * pci_find_next_device() - return the next available PCI device
 *
 * Finds the next available PCI device after the one supplied, or sets @devp
 * to NULL if there are no more.
 *
 * @devp:	On entry, the last device returned. Set to the next available
 *		device, or NULL if no more are left or we got an error
 * Return: 0 if all is OK, -ve on error (e.g. a bus/bridge failed to probe)
 */
s32 pci_find_next_device(struct pci_device **devp);

/**
 * pci_get_ff() - Returns a mask for the given access size
 *
 * @size:	Access size
 * Return: 0xff for PCI_SIZE_8, 0xffff for PCI_SIZE_16, 0xffffffff for
 * PCI_SIZE_32
 */
u32 pci_get_ff(enum pci_size_t size);

/**
 * pci_bus_find_devices () - Find devices on a bus
 *
 * @bus:	Bus to search
 * @ids:	PCI vendor/device IDs to look for, terminated by 0, 0 record
 * @indexp:	Pointer to device index to find. To find the first matching
 *		device, pass 0; to find the second, pass 1, etc. This
 *		parameter is decremented for each non-matching device so
 *		can be called repeatedly.
 * @devp:	Returns matching device if found
 * Return: 0 if found, -ENODEV if not
 */
s32 pci_bus_find_devices(struct pci_bus *bus, const struct pci_device_id *ids,
						 int *indexp, struct pci_device **devp);

/**
 * pci_find_device_id() - Find a device on any bus
 *
 * @ids:	PCI vendor/device IDs to look for, terminated by 0, 0 record
 * @index:	Index number of device to find, 0 for the first match, 1 for
 *		the second, etc.
 * @devp:	Returns matching device if found
 * Return: 0 if found, -ENODEV if not
 */
s32 pci_find_device_id(struct pci_controller *controller, const struct pci_device_id *ids, int index,
					   struct pci_device **devp);

/**
 * pci_bus_clrset_config32() - Update a configuration value for a device
 *
 * The register at @offset is updated to (oldvalue & ~clr) | set.
 *
 * @bus:	Bus to access
 * @bdf:	PCI device address: bus, device and function -see PCI_BDF()
 * @offset:	Register offset to update
 * @clr:	Bits to clear
 * @set:	Bits to set
 * Return: 0 if OK, -ve on error
 */
s32 pci_bus_clrset_config32(struct pci_bus *bus, pci_dev_t bdf, u32 offset,
							u32 clr, u32 set);

/**
 * Driver model PCI config access functions. Use these in preference to others
 * when you have a valid device
 */
s32 pci_read_config(const struct pci_device *dev, u32 offset,
					   u32 *valuep, enum pci_size_t size);

s32 pci_read_config8(const struct pci_device *dev, u32 offset, u8 *valuep);
s32 pci_read_config16(const struct pci_device *dev, u32 offset, u16 *valuep);
s32 pci_read_config32(const struct pci_device *dev, u32 offset, u32 *valuep);

s32 pci_write_config(struct pci_device *dev, u32 offset, u32 value,
						enum pci_size_t size);

s32 pci_write_config8(struct pci_device *dev, u32 offset, u32 value);
s32 pci_write_config16(struct pci_device *dev, u32 offset, u32 value);
s32 pci_write_config32(struct pci_device *dev, u32 offset, u32 value);

/**
 * These permit convenient read/modify/write on PCI configuration. The
 * register is updated to (oldvalue & ~clr) | set.
 */
s32 pci_clrset_config8(struct pci_device *dev, u32 offset, u32 clr, u32 set);
s32 pci_clrset_config16(struct pci_device *dev, u32 offset, u32 clr, u32 set);
s32 pci_clrset_config32(struct pci_device *dev, u32 offset, u32 clr, u32 set);

/**
 * pci_conv_32_to_size() - convert a 32-bit read value to the given size
 *
 * Some PCI buses must always perform 32-bit reads. The data must then be
 * shifted and masked to reflect the required access size and offset. This
 * function performs this transformation.
 *
 * @value:	Value to transform (32-bit value read from @offset & ~3)
 * @offset:	Register offset that was read
 * @size:	Required size of the result
 * Return: the value that would have been obtained if the read had been
 * performed at the given offset with the correct size
 */
u32 pci_conv_32_to_size(u32 value, u32 offset, enum pci_size_t size);

/**
 * pci_conv_size_to_32() - update a 32-bit value to prepare for a write
 *
 * Some PCI buses must always perform 32-bit writes. To emulate a smaller
 * write the old 32-bit data must be read, updated with the required new data
 * and written back as a 32-bit value. This function performs the
 * transformation from the old value to the new value.
 *
 * @value:	Value to transform (32-bit value read from @offset & ~3)
 * @offset:	Register offset that should be written
 * @size:	Required size of the write
 * Return: the value that should be written as a 32-bit access to @offset & ~3.
 */
u32 pci_conv_size_to_32(u32 old, u32 value, u32 offset,
						  enum pci_size_t size);

/**
 * pci_get_controller() - obtain the controller to use for a bus
 *
 * @dev:	Device to check
 * Return: pointer to the controller device for this bus
 */
struct pci_controller *pci_get_controller(const struct pci_bus *bus);

/**
 * pci_get_regions() - obtain pointers to all the region types
 *
 * @dev:	Device to check
 * @iop:	Returns a pointer to the I/O region, or NULL if none
 * @memp:	Returns a pointer to the memory region, or NULL if none
 * @prefp:	Returns a pointer to the pre-fetch region, or NULL if none
 * Return: the number of non-NULL regions returned, normally 3
 */
u32 pci_get_regions(struct pci_device *dev, struct pci_region **iop,
					struct pci_region **memp, struct pci_region **prefp);

					/**
 * pci_write_bar32() - Write the address of a BAR
 *
 * This writes a raw address to a bar
 *
 * @dev:	PCI device to update
 * @barnum:	BAR number (0-5)
 * @addr:	BAR address
 */
void pci_write_bar32(struct pci_device *dev, u32 barnum, u32 addr);

/**
 * pci_read_bar32() - read a base address register from a device
 *
 * @dev:	Device to check
 * @barnum:	Bar number to read (numbered from 0)
 * @return: value of BAR
 */
u32 pci_read_bar32(const struct pci_device *dev, u32 barnum);

/**
 * pci_bus_to_phys() - convert a PCI bus address range to a physical address
 *
 * @dev:	Device containing the PCI address
 * @addr:	PCI address to convert
 * @len:	Length of the address range
 * @mask:	Mask to match flags for the region type
 * @flags:	Flags for the region type (PCI_REGION_...)
 * Return: physical address corresponding to that PCI bus address
 */
phys_addr_t pci_bus_to_phys(struct pci_device *dev, pci_addr_t addr, size_t len,
							   u32 mask, u32 flags);

/**
 * pci_phys_to_bus() - convert a physical address to a PCI bus address
 *
 * @dev:	Device containing the bus address
 * @addr:	Physical address to convert
 * @len:	Length of the address range
 * @mask:	Mask to match flags for the region type
 * @flags:	Flags for the region type (PCI_REGION_...)
 * Return: PCI bus address corresponding to that physical address
 */
pci_addr_t pci_phys_to_bus(struct pci_device *dev, phys_addr_t addr, size_t len,
							  u32 mask, u32 flags);

void *pci_bus_to_virt(struct pci_device *dev, pci_addr_t bus_addr, size_t len,
								 u32 mask, u32 flags);
pci_addr_t pci_virt_to_bus(struct pci_device *dev, void *virt_addr, size_t len,
								   u32 mask, u32 flags);
/**
 * pci_map_bar() - get a virtual address associated with a BAR region
 *
 * Looks up a base address register and finds the physical memory address
 * that corresponds to it.
 * Can be used for 32b BARs 0-5 on type 0 functions and for 32b BARs 0-1 on
 * type 1 functions.
 * Can also be used on type 0 functions that support Enhanced Allocation for
 * 32b/64b BARs.  Note that duplicate BEI entries are not supported.
 * Can also be used on 64b bars on type 0 functions.
 *
 * @dev:	Device to check
 * @bar:	Bar register offset (PCI_BASE_ADDRESS_...)
 * @offset:     Offset from the base to map
 * @len:        Length to map
 * @mask:       Mask to match flags for the region type
 * @flags:	Flags for the region type (PCI_REGION_...)
 * @return: pointer to the virtual address to use or 0 on error
 */
void *pci_map_bar(struct pci_device *dev, u32 bar, pci_addr_t offset, size_t len,
					 u32 mask, u32 flags);

/**
 * pci_find_next_capability() - find a capability starting from an offset
 *
 * Tell if a device supports a given PCI capability. Returns the
 * address of the requested capability structure within the device's
 * PCI configuration space or 0 in case the device does not support it.
 *
 * Possible values for @cap:
 *
 *  %PCI_CAP_ID_MSI	Message Signalled Interrupts
 *  %PCI_CAP_ID_PCIX	PCI-X
 *  %PCI_CAP_ID_EXP	PCI Express
 *  %PCI_CAP_ID_MSIX	MSI-X
 *
 * See PCI_CAP_ID_xxx for the complete capability ID codes.
 *
 * @dev:	PCI device to query
 * @start:	offset to start from
 * @cap:	capability code
 * @return:	capability address or 0 if not supported
 */
u32 pci_find_next_capability(struct pci_device *dev, u8 start, u8 cap);

/**
 * pci_find_capability() - find a capability
 *
 * Tell if a device supports a given PCI capability. Returns the
 * address of the requested capability structure within the device's
 * PCI configuration space or 0 in case the device does not support it.
 *
 * Possible values for @cap:
 *
 *  %PCI_CAP_ID_MSI	Message Signalled Interrupts
 *  %PCI_CAP_ID_PCIX	PCI-X
 *  %PCI_CAP_ID_EXP	PCI Express
 *  %PCI_CAP_ID_MSIX	MSI-X
 *
 * See PCI_CAP_ID_xxx for the complete capability ID codes.
 *
 * @dev:	PCI device to query
 * @cap:	capability code
 * @return:	capability address or 0 if not supported
 */
u32 pci_find_capability(struct pci_device *dev, u8 cap);

/**
 * pci_find_next_ext_capability() - find an extended capability
 *				       starting from an offset
 *
 * Tell if a device supports a given PCI express extended capability.
 * Returns the address of the requested extended capability structure
 * within the device's PCI configuration space or 0 in case the device
 * does not support it.
 *
 * Possible values for @cap:
 *
 *  %PCI_EXT_CAP_ID_ERR	Advanced Error Reporting
 *  %PCI_EXT_CAP_ID_VC	Virtual Channel
 *  %PCI_EXT_CAP_ID_DSN	Device Serial Number
 *  %PCI_EXT_CAP_ID_PWR	Power Budgeting
 *
 * See PCI_EXT_CAP_ID_xxx for the complete extended capability ID codes.
 *
 * @dev:	PCI device to query
 * @start:	offset to start from
 * @cap:	extended capability code
 * @return:	extended capability address or 0 if not supported
 */
u32 pci_find_next_ext_capability(struct pci_device *dev, u32 start, u16 cap);

/**
 * pci_find_ext_capability() - find an extended capability
 *
 * Tell if a device supports a given PCI express extended capability.
 * Returns the address of the requested extended capability structure
 * within the device's PCI configuration space or 0 in case the device
 * does not support it.
 *
 * Possible values for @cap:
 *
 *  %PCI_EXT_CAP_ID_ERR	Advanced Error Reporting
 *  %PCI_EXT_CAP_ID_VC	Virtual Channel
 *  %PCI_EXT_CAP_ID_DSN	Device Serial Number
 *  %PCI_EXT_CAP_ID_PWR	Power Budgeting
 *
 * See PCI_EXT_CAP_ID_xxx for the complete extended capability ID codes.
 *
 * @dev:	PCI device to query
 * @cap:	extended capability code
 * @return:	extended capability address or 0 if not supported
 */
u32 pci_find_ext_capability(struct pci_device *dev, u16 cap);

/**
 * pci_flr() - Perform FLR if the device suppoorts it
 *
 * @dev:	PCI device to reset
 * @return:	0 if OK, -ENOENT if FLR is not supported by dev
 */
s32 pci_flr(struct pci_device *dev);

#define pci_phys_to_mem(dev, addr) \
	pci_phys_to_bus((dev), (addr), 0, PCI_REGION_TYPE, PCI_REGION_MEM)
#define pci_mem_to_phys(dev, addr) \
	pci_bus_to_phys((dev), (addr), 0, PCI_REGION_TYPE, PCI_REGION_MEM)
#define pci_phys_to_io(dev, addr) \
	pci_phys_to_bus((dev), (addr), 0, PCI_REGION_TYPE, PCI_REGION_IO)
#define pci_io_to_phys(dev, addr) \
	pci_bus_to_phys((dev), (addr), 0, PCI_REGION_TYPE, PCI_REGION_IO)

#define pci_virt_to_mem(dev, addr) \
	pci_virt_to_bus((dev), (addr), PCI_REGION_MEM)
#define pci_mem_to_virt(dev, addr, len, map_flags)         \
	pci_bus_to_virt((dev), (addr), (len), PCI_REGION_TYPE, \
					   PCI_REGION_MEM, (map_flags))
#define pci_virt_to_io(dev, addr) \
	pci_virt_to_bus((dev), (addr), PCI_REGION_IO)
#define pci_io_to_virt(dev, addr, len, map_flags)          \
	pci_bus_to_virt((dev), (addr), (len), PCI_REGION_TYPE, \
					   PCI_REGION_IO, (map_flags))

/**
 * pci_find_device() - find a device by vendor/device ID
 *
 * @vendor:	Vendor ID
 * @device:	Device ID
 * @index:	0 to find the first match, 1 for second, etc.
 * @devp:	Returns pointer to the device, if found
 * Return: 0 if found, -ve on error
 */
s32 pci_find_device(struct pci_controller *controller, u16 vendor, u16 device, int index, struct pci_device **devp);

/**
 * pci_find_class() - find a device by class
 *
 * @find_class: 3-byte (24-bit) class value to find
 * @index:	0 to find the first match, 1 for second, etc.
 * @devp:	Returns pointer to the device, if found
 * Return: 0 if found, -ve on error
 */
s32 pci_find_class(struct pci_controller *controller, u32 find_class, int index, struct pci_device **devp);

/**
 * PCI_DEVICE - macro used to describe a specific pci device
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device.  The subvendor and subdevice fields will be set to
 * PCI_ANY_ID.
 */
#define PCI_DEVICE(vend, dev)          \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_DEVICE_SUB - macro used to describe a specific pci device with subsystem
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 * @subvend: the 16 bit PCI Subvendor ID
 * @subdev: the 16 bit PCI Subdevice ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device with subsystem information.
 */
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev),             \
	.subvendor = (subvend), .subdevice = (subdev)

/**
 * PCI_DEVICE_CLASS - macro used to describe a specific pci device class
 * @dev_class: the class, subclass, prog-if triple for this device
 * @dev_class_mask: the class mask for this device
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI class.  The vendor, device, subvendor, and subdevice
 * fields will be set to PCI_ANY_ID.
 */
#define PCI_DEVICE_CLASS(dev_class, dev_class_mask)       \
	.class = (dev_class), .class_mask = (dev_class_mask), \
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID,           \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_VDEVICE - macro used to describe a specific pci device in short form
 * @vend: the vendor name
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI device.  The subvendor, and subdevice fields will be set
 * to PCI_ANY_ID. The macro allows the next field to follow as the device
 * private data.
 */

#define PCI_VDEVICE(vend, dev)                       \
	.vendor = PCI_VENDOR_ID_##vend, .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0

s32 pci_create_bus(struct pci_bus **busp, struct pci_bus *parent, struct pci_device *bridge, struct pci_controller *ctlr);
s32 pci_probe_bus(struct pci_bus *bus);
s32 pci_get_bus(struct pci_controller *controller, u32 busnum, struct pci_bus **busp);
s32 pci_create_device(struct pci_bus *bus, pci_dev_t bdf, u16 vendor, u16 device, u32 class, struct pci_device **devp);
s32 pci_get_bus_max(const struct pci_controller *controller);
BOOL pci_is_root_bus(const struct pci_bus *bus);


/* Legacy INTx */
void pci_intx(struct pci_device *pdev, int enable);
void pci_assign_irq(struct pci_device *dev); // called it in initialization of device
BOOL pci_check_and_set_intx_mask(struct pci_device *dev, BOOL mask);

/*MSI interrupt handling */
s32 add_int_server(struct pci_device *dev, struct Interrupt *isr);
s32 rem_int_server(struct pci_device *dev);
void pci_msi_mask_irq(struct pci_device *dev, int irq);
void pci_msi_unmask_irq(struct pci_device *dev, int irq);

void pci_write_msg_msi(struct pci_device *dev); // for discovery purpose
void pci_msi_init(struct pci_device *dev); // for discovery purpose
s32 msi_capability_init(struct pci_device *dev, u32 nvec); // for add_int_server
void pci_msi_shutdown(struct pci_device *dev); // for rem_int_server

#endif /* _PCI_H */