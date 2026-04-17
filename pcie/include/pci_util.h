/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI utility functions — type conversion, region access, bus/device
 * accessors, and Function-Level Reset.
 */

#ifndef _PCI_UTIL_H
#define _PCI_UTIL_H

#include <pci_types.h>

/**
 * pci_get_bdf() - Get the BDF address of a device
 *
 * @dev:   Device to query
 * Return: Bus/device/function value encoded as returned by PCI_BDF()
 */
pci_dev_t pci_get_bdf(const struct pci_device *dev);

/**
 * pci_get_controller() - Get the controller that owns a bus
 *
 * @bus:   Bus to query
 * Return: Pointer to the pci_controller this bus is registered under
 */
struct pci_controller *pci_get_controller(const struct pci_bus *bus);

/**
 * pci_get_bus() - Find a bus by bus number within a controller
 *
 * Walks the bus list of @controller looking for a bus whose bus_number
 * field equals @busnum.
 *
 * @controller: Controller to search
 * @busnum:     Bus number to find
 * @busp:       Set to the matching bus on success
 * Return:      0 on success, -ENODEV if not found
 */
s32 pci_get_bus(struct pci_controller *controller, u32 busnum,
				struct pci_bus **busp);

/**
 * pci_get_bus_max() - Return the highest bus number registered in a controller
 *
 * Iterates the bus list and returns the largest bus_number value seen.
 *
 * @controller: Controller to inspect
 * Return:      Highest bus number, or -1 if no buses have been registered
 */
s32 pci_get_bus_max(const struct pci_controller *controller);

/**
 * pci_is_root_bus() - Test whether a bus is the root bus of its controller
 *
 * The root bus is identified by having a NULL parent pointer.
 *
 * @bus:   Bus to test
 * Return: TRUE if this is the root bus, FALSE otherwise
 */
BOOL pci_is_root_bus(const struct pci_bus *bus);

/**
 * pci_get_ff() - Return an all-ones mask for a given access size
 *
 * Used internally to produce bus-idle sentinel values for failed reads.
 *
 * @size:   Access width
 * Return:  0xff for PCI_SIZE_8, 0xffff for PCI_SIZE_16,
 *          0xffffffff for PCI_SIZE_32
 */
u32 pci_get_ff(enum pci_size_t size);

/**
 * pci_conv_32_to_size() - Extract a sub-word value from a 32-bit config read
 *
 * PCI controllers that can only perform 32-bit reads must use this function to
 * extract the 8- or 16-bit sub-field at the requested @offset alignment.
 *
 * @value:  32-bit value read from (@offset & ~3)
 * @offset: Actual byte offset of the register that was requested
 * @size:   Requested access size
 * Return:  Value extracted at the correct byte position for @size
 */
u32 pci_conv_32_to_size(u32 value, u32 offset, enum pci_size_t size);

/**
 * pci_conv_size_to_32() - Merge a sub-word value into a 32-bit config word
 *
 * PCI controllers that can only perform 32-bit writes must use this function
 * to splice the narrow @value into the existing 32-bit @old word at the
 * correct byte lane for @offset.
 *
 * @old:    Existing 32-bit value read from (@offset & ~3)
 * @value:  New narrow value to write
 * @offset: Actual byte offset of the register being written
 * @size:   Access size of the write
 * Return:  Updated 32-bit word ready to write back to (@offset & ~3)
 */
u32 pci_conv_size_to_32(u32 old, u32 value, u32 offset, enum pci_size_t size);

/**
 * pci_get_regions() - Obtain pointers to the I/O, memory, and prefetch regions
 *
 * Scans the controller's region table and selects the largest region of each
 * type.  Any output pointer may be set to NULL if no region of that type
 * exists.
 *
 * @dev:   Device whose controller provides the region table
 * @iop:   Set to the largest I/O region, or NULL if none
 * @memp:  Set to the largest non-prefetchable memory region, or NULL if none
 * @prefp: Set to the largest prefetchable memory region, or NULL if none
 * Return: Number of non-NULL output pointers (0–3)
 */
u32 pci_get_regions(struct pci_device *dev, struct pci_region **iop,
					struct pci_region **memp, struct pci_region **prefp);

/**
 * pci_flr() - Perform a Function-Level Reset on a PCIe device
 *
 * Checks the PCIe Device Capabilities register for FLR support, then issues
 * the reset by setting PCI_EXP_DEVCTL_BCR_FLR.  Waits 100 ms after the reset
 * bit is written, as required by the PCIe base specification.
 *
 * @dev:   PCIe device to reset
 * Return: 0 on success, -ENOENT if FLR is not supported by the device
 */
s32 pci_flr(struct pci_device *dev);

#endif /* _PCI_UTIL_H */
