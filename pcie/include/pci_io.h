/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI configuration space read/write functions and BAR register helpers.
 *
 * All functions reach the BCM2711 PCIe controller via brcm_pcie_read_config /
 * brcm_pcie_write_config.  Width conversions are handled internally: 8- and
 * 16-bit variants read a full 32-bit word and extract or splice the requested
 * sub-field.  On read errors the output is set to the all-ones bus-idle value
 * for the requested width (0xff / 0xffff / 0xffffffff).
 */

#ifndef _PCI_IO_H
#define _PCI_IO_H

#include <pci_types.h>

/**
 * pci_read_config8() - Read an 8-bit PCI configuration register
 *
 * @dev:    Device to read from
 * @offset: Byte offset of the register
 * @valuep: Receives the 8-bit value; set to 0xff on error
 * Return:  0 on success, negative on error
 */
s32 pci_read_config8(const struct pci_device *dev, u32 offset, u8 *valuep);

/**
 * pci_read_config16() - Read a 16-bit PCI configuration register
 *
 * @dev:    Device to read from
 * @offset: Byte offset of the register; should be 2-byte aligned
 * @valuep: Receives the 16-bit value; set to 0xffff on error
 * Return:  0 on success, negative on error
 */
s32 pci_read_config16(const struct pci_device *dev, u32 offset, u16 *valuep);

/**
 * pci_read_config32() - Read a 32-bit PCI configuration register
 *
 * @dev:    Device to read from
 * @offset: Byte offset of the register; should be 4-byte aligned
 * @valuep: Receives the 32-bit value; set to 0xffffffff on error
 * Return:  0 on success, negative on error
 */
s32 pci_read_config32(const struct pci_device *dev, u32 offset, u32 *valuep);

/**
 * pci_write_config8() - Write an 8-bit PCI configuration register
 *
 * @dev:    Device to write to
 * @offset: Byte offset of the register
 * @value:  8-bit value to write
 * Return:  0 on success, negative on error
 */
s32 pci_write_config8(struct pci_device *dev, u32 offset, u32 value);

/**
 * pci_write_config16() - Write a 16-bit PCI configuration register
 *
 * @dev:    Device to write to
 * @offset: Byte offset of the register; should be 2-byte aligned
 * @value:  16-bit value to write
 * Return:  0 on success, negative on error
 */
s32 pci_write_config16(struct pci_device *dev, u32 offset, u32 value);

/**
 * pci_write_config32() - Write a 32-bit PCI configuration register
 *
 * @dev:    Device to write to
 * @offset: Byte offset of the register; should be 4-byte aligned
 * @value:  32-bit value to write
 * Return:  0 on success, negative on error
 */
s32 pci_write_config32(struct pci_device *dev, u32 offset, u32 value);

/**
 * pci_clrset_config8() - Read-modify-write an 8-bit PCI configuration register
 *
 * Updates the register at @offset to (old & ~@clr) | @set.
 *
 * @dev:    Device to update
 * @offset: Byte offset of the register
 * @clr:    Bits to clear
 * @set:    Bits to set
 * Return:  0 on success, negative on error
 */
s32 pci_clrset_config8(struct pci_device *dev, u32 offset, u32 clr, u32 set);

/**
 * pci_clrset_config16() - Read-modify-write a 16-bit PCI configuration register
 *
 * Updates the register at @offset to (old & ~@clr) | @set.
 *
 * @dev:    Device to update
 * @offset: Byte offset of the register; should be 2-byte aligned
 * @clr:    Bits to clear
 * @set:    Bits to set
 * Return:  0 on success, negative on error
 */
s32 pci_clrset_config16(struct pci_device *dev, u32 offset, u32 clr, u32 set);

/**
 * pci_clrset_config32() - Read-modify-write a 32-bit PCI configuration register
 *
 * Updates the register at @offset to (old & ~@clr) | @set.
 *
 * @dev:    Device to update
 * @offset: Byte offset of the register; should be 4-byte aligned
 * @clr:    Bits to clear
 * @set:    Bits to set
 * Return:  0 on success, negative on error
 */
s32 pci_clrset_config32(struct pci_device *dev, u32 offset, u32 clr, u32 set);

/**
 * pci_bus_clrset_config32() - Read-modify-write a 32-bit config register by BDF
 *
 * Performs a 32-bit read-modify-write without requiring a pci_device handle.
 * Updates the register at @offset to (old & ~@clr) | @set on the device
 * identified by @bdf on @bus.
 *
 * @bus:    Bus containing the target device
 * @bdf:    Bus/device/function address (see PCI_BDF())
 * @offset: Byte offset of the register; should be 4-byte aligned
 * @clr:    Bits to clear
 * @set:    Bits to set
 * Return:  0 on success, negative on error
 */
s32 pci_bus_clrset_config32(struct pci_bus *bus, pci_dev_t bdf, u32 offset,
							u32 clr, u32 set);

#endif /* _PCI_IO_H */
