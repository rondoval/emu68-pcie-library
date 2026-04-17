/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI address translation and BAR-mapping helpers.
 *
 * These functions translate between PCI bus addresses (used in BARs and DMA
 * descriptors), CPU physical addresses (seen by the ARM / m68k processor), and
 * driver virtual addresses that map into the controller's pre-mapped MMIO
 * window.
 *
 * Address translation uses the controller's outbound window table (populated
 * from the device tree at probe time).  Virtual-address functions additionally
 * check that the result falls within the MMIO window registered at init; they
 * return NULL / 0 on a miss.
 */

#ifndef _PCI_BAR_H
#define _PCI_BAR_H

#include <pci_types.h>
#include <libraries/pci_constants.h>

/**
 * pci_bus_to_phys() - Convert a PCI bus address to a CPU physical address
 *
 * Searches the controller's outbound region table for a window that contains
 * @bus_addr and applies the window offset to produce the CPU physical address.
 * Returns 0 if no matching region is found.
 *
 * @dev:      Device whose controller defines the region table
 * @bus_addr: PCI bus address to translate
 * @len:      Length of the access (used for end-of-window bounds checking)
 * @mask:     Mask applied to region flags during matching (e.g. PCI_REGION_TYPE)
 * @flags:    Required flag bits after masking (e.g. PCI_REGION_MEM)
 * Return:    CPU physical address, or 0 on failure
 */
phys_addr_t pci_bus_to_phys(struct pci_device *dev, pci_addr_t bus_addr,
							size_t len, u32 mask, u32 flags);

/**
 * pci_phys_to_bus() - Convert a CPU physical address to a PCI bus address
 *
 * Searches the controller's outbound region table for a window that contains
 * @phys_addr and applies the inverse offset to produce the PCI bus address
 * seen by DMA masters on the PCIe bus.  Returns 0 on failure.
 *
 * @dev:       Device whose controller defines the region table
 * @phys_addr: CPU physical address to translate
 * @len:       Length of the access (used for bounds checking)
 * @mask:      Mask applied to region flags during matching
 * @flags:     Required flag bits after masking
 * Return:     PCI bus address, or 0 on failure
 */
pci_addr_t pci_phys_to_bus(struct pci_device *dev, phys_addr_t phys_addr,
							size_t len, u32 mask, u32 flags);

/**
 * pci_bus_to_virt() - Convert a PCI bus address to a driver virtual address
 *
 * Translates @bus_addr to a CPU physical address via pci_bus_to_phys(), then
 * checks that it falls within the controller's pre-mapped MMIO window and
 * computes the corresponding virtual pointer.  Returns NULL if the physical
 * translation fails or the address falls outside the window.
 *
 * @dev:      Device whose controller defines the MMIO window
 * @bus_addr: PCI bus address to translate
 * @len:      Length of the access (used for window bounds checking)
 * @mask:     Mask applied to region flags during matching
 * @flags:    Required flag bits after masking
 * Return:    Virtual pointer into the MMIO window, or NULL on failure
 */
void *pci_bus_to_virt(struct pci_device *dev, pci_addr_t bus_addr,
					  size_t len, u32 mask, u32 flags);

/**
 * pci_virt_to_bus() - Convert a driver virtual address to a PCI bus address
 *
 * Verifies that @virt_addr falls within the controller's MMIO window, derives
 * the corresponding CPU physical address from the window base offset, and
 * translates it to a PCI bus address via pci_phys_to_bus().  Returns 0 if the
 * address is not within the window.
 *
 * @dev:       Device whose controller defines the MMIO window
 * @virt_addr: Driver virtual address to translate
 * @len:       Length of the access
 * @mask:      Mask applied to region flags during matching
 * @flags:     Required flag bits after masking
 * Return:     PCI bus address, or 0 on failure
 */
pci_addr_t pci_virt_to_bus(struct pci_device *dev, void *virt_addr,
							size_t len, u32 mask, u32 flags);

/**
 * pci_map_bar() - Map a window within a device BAR into virtual address space
 *
 * Reads the BAR register at config offset @bar, adds @offset to the decoded
 * bus base address, and translates the result to a driver virtual pointer via
 * pci_bus_to_virt().  Supports 32-bit BARs; 64-bit BARs are also handled when
 * CONFIG_SYS_PCI_64BIT is defined.  Returns NULL on any failure.
 *
 * @dev:    Device whose BAR to map
 * @bar:    BAR config register offset (e.g. PCI_BASE_ADDRESS_0 + n*4)
 * @offset: Byte offset from the BAR base bus address to start mapping from
 * @len:    Length of the window to map, in bytes
 * @mask:   Mask applied to region flags during matching
 * @flags:  Required flag bits after masking (e.g. PCI_REGION_MEM)
 * Return:  Virtual pointer, or NULL on failure
 */
void *pci_map_bar(struct pci_device *dev, u32 bar, pci_addr_t offset,
				  size_t len, u32 mask, u32 flags);

/*
 * Convenience wrappers that select the memory or I/O region automatically.
 * The pci_virt_to_mem / pci_io_to_virt / pci_mem_to_virt / pci_io_to_virt
 * macros below were originally ported from U-Boot and may carry an extra
 * parameter mismatch; they are preserved for API compatibility but are not
 * known to be called in this codebase.
 */
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

#endif /* _PCI_BAR_H */
