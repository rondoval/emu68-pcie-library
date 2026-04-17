/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI INTx (legacy interrupt) management.
 *
 * Handles the INTX_DISABLE bit in the PCI command register, pin-to-IRQ
 * swizzling for devices behind bridges, and the atomic mask/status check used
 * for shared-interrupt handlers.
 */

#ifndef _PCI_INT_H
#define _PCI_INT_H

#include <pci_types.h>

/**
 * pci_intx() - Enable or disable a device's INTx assertion
 *
 * Manipulates the PCI_COMMAND_INTX_DISABLE bit:
 *   enable = 1 → clears the bit, allowing the device to assert INTx
 *   enable = 0 → sets the bit, masking INTx at the device
 *
 * The function is a no-op if the bit is already in the requested state.
 *
 * @pdev:   Device to update
 * @enable: Non-zero to enable INTx, zero to disable it
 */
void pci_intx(struct pci_device *pdev, int enable);

/**
 * pci_assign_irq() - Resolve a device's interrupt pin to a GIC IRQ number
 *
 * Reads the device's PCI_INTERRUPT_PIN register, then walks the bridge
 * chain to the root bus performing the standard PCI slot-based swizzle at
 * each hop.  The final pin index is looked up in the root controller's
 * INT_x_mapping[] array to obtain the GIC IRQ number, which is stored in
 * dev->irq_pin and dev->irq_line.
 *
 * Does nothing if PCI_INTERRUPT_PIN is 0 (device uses no interrupt pin).
 *
 * @dev: Device to configure
 */
void pci_assign_irq(struct pci_device *dev);

/**
 * pci_check_and_set_intx_mask() - Conditionally mask or unmask INTx
 *
 * Reads the command and status registers in a single 32-bit access to
 * atomically check whether an interrupt is pending before changing the mask.
 * Only updates the mask bit if the current pending state matches @mask:
 *   mask = TRUE  → writes INTX_DISABLE only if an interrupt is pending
 *   mask = FALSE → clears INTX_DISABLE only if no interrupt is pending
 *
 * @dev:   Device to update
 * @mask:  TRUE to request masking, FALSE to request unmasking
 * Return: TRUE if the mask register was actually written, FALSE if the
 *         interrupt pending state did not match the requested action
 */
BOOL pci_check_and_set_intx_mask(struct pci_device *dev, BOOL mask);

#endif /* _PCI_INT_H */
