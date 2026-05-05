/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI Message Signaled Interrupt (MSI) management.
 *
 * MSI setup sequence for a device:
 *   1. pci_msi_init()        — locate cap_offset, ensure MSI is off at probe time
 *   2. msi_capability_init() — enable MSI: mask all vectors, disable INTx, enable MSI
 *   3. pci_write_msg_msi()   — program address + data registers in config space
 *   4. add_int_server()      — register the ISR in the controller MSI dispatch table
 *
 * Teardown:
 *   rem_int_server()  — remove the ISR from the dispatch table
 *   pci_msi_shutdown() — disable MSI and restore masks to the all-unmasked state
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
 * msi_capability_init() - Enable MSI for a device
 *
 * Disables MSI, reads the capability flags to populate the msi_flags fields
 * (addr64, maskable, log2_max_vecs, mask_offset), masks all vectors, disables
 * the legacy INTx assertion, then re-enables MSI.  The vector assignment and
 * address/data programming must follow via pci_write_msg_msi().
 *
 * @dev:  Device to enable MSI on
 * @nvec: Number of vectors requested (currently only 1 is supported)
 * Return: 0 on success, negative on error (MSI remains disabled on failure)
 */
s32 msi_capability_init(struct pci_device *dev, u32 nvec);

/**
 * pci_write_msg_msi() - Program the MSI address and data registers
 *
 * Writes the controller's MSI target address (ctrl->msi.target_addr) and the
 * per-device data word (derived from dev->msi.vector) into the device's MSI
 * capability structure in config space.  Handles both 32-bit and 64-bit
 * address formats.  Must be called after msi_capability_init().
 *
 * @dev: Device whose MSI config-space registers should be programmed
 */
void pci_write_msg_msi(struct pci_device *dev);

/**
 * pci_msi_shutdown() - Disable MSI and restore the all-unmasked state
 *
 * Disables MSI in the capability control register, clears dev->msi.enabled,
 * and calls pci_msi_unmask() to unmask all vectors.  Intended for cleanup
 * during driver detach or error recovery.  Safe to call if MSI was never
 * enabled (returns immediately in that case).
 *
 * @dev: Device to shut down
 */
void pci_msi_shutdown(struct pci_device *dev);

/**
 * pci_msi_mask_irq() - Mask a single MSI vector
 *
 * Sets the corresponding bit in the per-vector mask register (if the device
 * supports per-vector masking).  Does nothing if dev->msi_flags.maskable is
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
 * if dev->msi_flags.maskable is not set.
 *
 * @dev: Device whose MSI vector to unmask
 * @irq: Vector index to unmask (0-based)
 */
void pci_msi_unmask_irq(struct pci_device *dev, int irq);

/**
 * add_int_server() - Register an interrupt server for a device using MSI
 *
 * Allocates a free slot in the controller's MSI vector table, calls
 * msi_capability_init() and pci_write_msg_msi() to configure the device, and
 * inserts @isr into the BCM2711 PCIe MSI dispatch table at the assigned slot.
 * The controller must have MSI enabled (brcm_pcie_enable_msi() called at
 * LibOpen time).
 *
 * @dev: Device requesting MSI
 * @isr: Interrupt server structure to register
 * Return: Assigned vector slot (>= 0) on success, negative on error
 */
s32 add_int_server(struct pci_device *dev, struct Interrupt *isr);

/**
 * rem_int_server() - Remove a previously registered MSI interrupt server
 *
 * Locates the vector slot assigned to @dev in the controller's MSI table,
 * removes the ISR, and calls pci_msi_shutdown() to disable MSI on the device.
 *
 * @dev: Device whose interrupt server should be removed
 * Return: 0 on success, negative if no server was registered for @dev
 */
s32 rem_int_server(struct pci_device *dev);

#endif /* _PCI_MSI_H */
