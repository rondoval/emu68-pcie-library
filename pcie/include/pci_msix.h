/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI MSI-X management.
 *
 * MSI-X is preferred over MSI when a device implements it, because the BCM2711
 * root complex (and many controllers) only deliver MSI-X reliably — single
 * message MSI is broken on some drives (e.g. the Micron 2300 / Sandisk SN5xx,
 * Linux quirk NVME_QUIRK_BROKEN_MSI).  At the bridge the two are identical:
 * both are memory writes to ctrl->msi.target_addr with data 0x6540 | vector,
 * demuxed by the same PCIE_MSI_INTR2 dispatcher.  The only difference is where
 * the message lives — MSI uses config space, MSI-X a BAR-mapped table.
 *
 * Setup sequence for a device (driven from the pci_irq core):
 *   1. pci_msix_init()         — locate cap_offset, cache table BIR/offset/size,
 *                                ensure MSI-X is off at probe time
 *   2. pci_msix_capability_init() — map the table, program the used entries with the
 *                                controller doorbell address/data, enable MSI-X
 *
 * Teardown:
 *   pci_msix_shutdown() — mask the function/vectors and disable MSI-X
 */

#ifndef _PCI_MSIX_H
#define _PCI_MSIX_H

#include <pci_types.h>

/**
 * pci_msix_init() - Discover and initialise a device's MSI-X capability
 *
 * Searches the capability list for PCI_CAP_ID_MSIX and, if present, caches the
 * capability offset, table size, and table BAR index / offset in dev->msix.
 * If MSI-X is already enabled it is disabled, leaving the device in a known-off
 * state.  Does nothing if the device does not implement MSI-X.
 *
 * Called once during device setup, alongside pci_msi_init().
 *
 * @dev: Device to initialise
 */
void pci_msix_init(struct pci_device *dev);

/**
 * pci_msix_alloc() - Reserve and program an MSI-X allocation
 *
 * Reserves [@min,@max] demux slots from the pci_irq pool (bounded by the
 * device's MSI-X table size), programs the table for them with every entry
 * masked, and records the result in dev->active.  Rolls back on failure.
 *
 * @dev: Device to allocate MSI-X for
 * @min: Minimum acceptable vector count (>= 1)
 * @max: Maximum desired vector count
 * Return: number of vectors allocated (>= @min), or negative errno
 *         (-ENODEV no controller demux / no MSI-X cap, -ERANGE bad range,
 *          -ENOSPC slots exhausted, -EIO table programming failed)
 */
s32 pci_msix_alloc(struct pci_device *dev, u32 min, u32 max);

/**
 * pci_msix_shutdown() - Disable MSI-X and mask the device
 *
 * Masks every allocated table entry and the function, then clears the MSI-X
 * enable bit.  Safe to call if the device has no MSI-X capability.
 *
 * @dev: Device to shut down
 */
void pci_msix_shutdown(struct pci_device *dev);

/**
 * pci_msix_mask_irq() / pci_msix_unmask_irq() - Mask/unmask one MSI-X vector
 *
 * Sets or clears the per-vector mask bit in the table entry's Vector Control
 * dword.  Does nothing if the table has not been mapped.
 *
 * @dev: Device whose MSI-X table entry to (un)mask
 * @idx: Table entry index (0-based)
 */
void pci_msix_mask_irq(struct pci_device *dev, int idx);
void pci_msix_unmask_irq(struct pci_device *dev, int idx);

#endif /* _PCI_MSIX_H */
