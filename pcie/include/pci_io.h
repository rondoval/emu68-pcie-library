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
 * pci_read_config() - Read from PCI configuration space
 *
 * Reads @size bytes from config register @offset of @dev into *@valuep.
 * On error *@valuep is set to the all-ones bus-idle value for @size.
 *
 * @dev:    Device to read from
 * @offset: Byte offset of the register
 * @valuep: Receives the value read
 * @size:   Transfer width (PCI_SIZE_8, PCI_SIZE_16, or PCI_SIZE_32)
 * Return:  0 on success, negative on error
 */
s32 pci_read_config(const struct pci_device *dev, u32 offset,
					u32 *valuep, enum pci_size_t size);

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
 * pci_write_config() - Write to PCI configuration space
 *
 * @dev:    Device to write to
 * @offset: Byte offset of the register
 * @value:  Value to write
 * @size:   Transfer width (PCI_SIZE_8, PCI_SIZE_16, or PCI_SIZE_32)
 * Return:  0 on success, negative on error
 */
s32 pci_write_config(struct pci_device *dev, u32 offset, u32 value,
					 enum pci_size_t size);

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

/**
 * pci_read_bar32() - Read the address stored in a 32-bit BAR register
 *
 * Reads BAR @barnum from config space and returns the assigned bus address
 * with the type/flag bits stripped.  For I/O BARs the PCI_BASE_ADDRESS_IO_MASK
 * is applied; for memory BARs the PCI_BASE_ADDRESS_MEM_MASK is applied.
 * Returns 0xffffffff if the register reads as all-ones (BAR not implemented).
 *
 * @dev:    Device to read from
 * @barnum: BAR index (0–5 for type-0 endpoints, 0–1 for bridges)
 * Return:  Assigned bus address with flags stripped, or 0xffffffff if absent
 */
u32 pci_read_bar32(const struct pci_device *dev, u32 barnum);

/**
 * pci_write_bar32() - Write a raw 32-bit value to a BAR register
 *
 * Writes @addr directly into BAR register @barnum without masking.  The
 * caller is responsible for preserving the BAR type bits when required.
 *
 * @dev:    Device to write to
 * @barnum: BAR index (0–5 for type-0 endpoints, 0–1 for bridges)
 * @addr:   Full 32-bit value to write to the BAR register
 */
void pci_write_bar32(struct pci_device *dev, u32 barnum, u32 addr);

#endif /* _PCI_IO_H */
