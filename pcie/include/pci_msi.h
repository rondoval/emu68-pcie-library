/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI Message Signaled Interrupt (MSI) management — device-side config-space
 * programming.  Vector reservation and ISR dispatch are owned by the pci_irq
 * core (pci_irq.h); this module is driven by it:
 *   1. pci_msi_init()        — locate cap_offset, ensure MSI is off at probe time
 *   2. pci_msi_capability_init() — program the cap for dev->active.nvec vectors (masked),
 *                              disable INTx, enable MSI
 *   3. pci_msi_shutdown()    — disable MSI and restore the all-unmasked state
 */

#ifndef _PCI_MSI_H
#define _PCI_MSI_H

#include <pci_types.h>
#include <exec/interrupts.h>

/**
 * pci_msi_init() - Discover and initialise a device's MSI capability
 *
 * Searches the capability list for PCI_CAP_ID_MSI and stores the offset in
 * dev->msi.cap_offset.  If MSI is already enabled in the capability flags it
 * is disabled, ensuring the device starts in a known-off state.  Does nothing
 * if the device does not implement MSI.
 *
 * Called once during device setup, before any interrupt server is registered.
 *
 * @dev: Device to initialise
 */
void pci_msi_init(struct pci_device *dev);

/**
 * pci_msi_alloc() - Reserve and program a multi-message MSI allocation
 *
 * Reserves a 2^k-aligned block of [@min,@max] demux slots from the pci_irq pool
 * (bounded by the device's Multiple-Message-Capable count), programs the MSI
 * capability for them with every vector masked, and records the result in
 * dev->active.  Rolls back fully on any failure.
 *
 * @dev: Device to allocate MSI for
 * @min: Minimum acceptable vector count (>= 1)
 * @max: Maximum desired vector count
 * Return: number of vectors allocated (>= @min), or negative errno
 *         (-ENODEV no controller demux / no MSI cap, -ERANGE bad range,
 *          -ENOSPC slots exhausted, -EIO capability programming failed)
 */
s32 pci_msi_alloc(struct pci_device *dev, u32 min, u32 max);

/**
 * pci_msi_shutdown() - Disable MSI and restore the all-unmasked state
 *
 * Disables MSI in the capability control register and unmasks all vectors.
 * Intended for cleanup during driver detach or error recovery.  Safe to call
 * if the device has no MSI capability (returns immediately).
 *
 * @dev: Device to shut down
 */
void pci_msi_shutdown(struct pci_device *dev);

/**
 * pci_msi_mask_irq() - Mask a single MSI vector
 *
 * Sets the corresponding bit in the per-vector mask register (if the device
 * supports per-vector masking).  Does nothing if dev->msi.maskable is
 * not set.
 *
 * @dev: Device whose MSI vector to mask
 * @irq: Vector index to mask (0-based)
 */
void pci_msi_mask_irq(struct pci_device *dev, int irq);

/**
 * pci_msi_unmask_irq() - Unmask a single MSI vector
 *
 * Clears the corresponding bit in the per-vector mask register.  Does nothing
 * if dev->msi.maskable is not set.
 *
 * @dev: Device whose MSI vector to unmask
 * @irq: Vector index to unmask (0-based)
 */
void pci_msi_unmask_irq(struct pci_device *dev, int irq);

#endif /* _PCI_MSI_H */
