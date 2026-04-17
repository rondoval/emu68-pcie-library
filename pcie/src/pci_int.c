// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI IRQ handling code
 *
 * Copyright (c) 2008 James Bottomley <James.Bottomley@HansenPartnership.com>
 * Copyright (C) 2017 Christoph Hellwig.
 */

#include <debug.h>

#include <pcie_brcmstb.h>
#include <pci.h>

/**
 * pci_intx - enables/disables PCI INTx for device dev
 * @pdev: the PCI device to operate on
 * @enable: boolean: whether to enable or disable PCI INTx
 *
 * Enables/disables PCI INTx for device @pdev
 */
void pci_intx(struct pci_device *pdev, int enable)
{
	u16 raw_command;
	u32 pci_command, new_command;

	pci_read_config16(pdev, PCI_COMMAND, &raw_command);
	pci_command = raw_command;

	if (enable)
		new_command = pci_command & ~PCI_COMMAND_INTX_DISABLE;
	else
		new_command = pci_command | PCI_COMMAND_INTX_DISABLE;

	if (new_command == pci_command)
		return;

	pci_write_config16(pdev, PCI_COMMAND, new_command);
}

/**
 * pci_swizzle_interrupt_pin - swizzle INTx for device behind bridge
 * @dev: the PCI device
 * @pin: the INTx pin (1=INTA, 2=INTB, 3=INTC, 4=INTD)
 *
 * Perform INTx swizzling for a device behind one level of bridge.  This is
 * required by section 9.1 of the PCI-to-PCI bridge specification for devices
 * behind bridges on add-in cards.  For devices with ARI enabled, the slot
 * number is always 0 (see the Implementation Note in section 2.2.8.1 of
 * the PCI Express Base Specification, Revision 2.1)
 */
static u32 pci_swizzle_interrupt_pin(const struct pci_device *dev, u32 pin)
{
	// TODO We don't support ARI yet
	// if (pci_ari_enabled(dev->bus))
	// 	slot = 0;
	// else
		//slot = PCI_SLOT(dev->devfn);
		u32 slot = PCI_DEV(dev->devfn);

	return (((pin - 1) + slot) % 4) + 1;
}

void pci_assign_irq(struct pci_device *dev)
{
	struct pci_device *target = dev;
	struct pci_device *walker = dev;
	u8 pin_byte;
	u32 pin;
	s32 irq = 0;

	/*
	 * If this device is not on the primary bus, we need to figure out
	 * which interrupt pin it will come in on. We know which slot it
	 * will come in on because that slot is where the bridge is. Each
	 * time the interrupt line passes through a PCI-PCI bridge we must
	 * apply the swizzle function.
	 */
	pci_read_config8(dev, PCI_INTERRUPT_PIN, &pin_byte);
	pin = pin_byte;
	/* Cope with illegal. */
	if (pin > 4)
		pin = 1;

	if (pin == 0)
		return;
	Kprintf("[pcie] %s: initial pin %ld\n", __func__, pin);

	/* Follow the chain of bridges, swizzling as we go. */
	while (!pci_is_root_bus(walker->bus)) {
		pin = pci_swizzle_interrupt_pin(walker, pin);
		Kprintf("[pcie] %s: swizzle to pin %ld\n", __func__, pin);
		walker = walker->bus->pci_bridge;
	}

	/* Map the pin to INT line */
	irq = pci_get_controller(walker->bus)->INT_x_mapping[pin - 1];
	Kprintf("[pcie] %s: assign IRQ: got %ld\n", __func__, irq);
	target->irq_pin = (u8)pin;
	target->irq_line = (u8)irq;

	/*
	 * Always tell the device, so the driver knows what is the real IRQ
	 * to use; the device does not use it.
	 */
	pci_write_config8(target, PCI_INTERRUPT_LINE, (u32)pin);
}

BOOL pci_check_and_set_intx_mask(struct pci_device *dev, BOOL mask)
{
	BOOL mask_updated = TRUE;
	u32 cmd_status_dword;
	u32 origcmd, newcmd;
	BOOL irq_pending;

	/*
	 * We do a single dword read to retrieve both command and status.
	 * Document assumptions that make this possible.
	 * BUILD_BUG_ON(PCI_COMMAND % 4);
	 * BUILD_BUG_ON(PCI_COMMAND + 2 != PCI_STATUS);
	 */

	pci_read_config32(dev, PCI_COMMAND, &cmd_status_dword);

	irq_pending = (cmd_status_dword >> 16) & PCI_STATUS_INTERRUPT;

	/*
	 * Check interrupt status register to see whether our device
	 * triggered the interrupt (when masking) or the next IRQ is
	 * already pending (when unmasking).
	 */
	if (mask != irq_pending)
	{
		mask_updated = FALSE;
		goto done;
	}

	origcmd = cmd_status_dword;
	newcmd = origcmd & ~PCI_COMMAND_INTX_DISABLE;
	if (mask)
		newcmd |= PCI_COMMAND_INTX_DISABLE;
	if (newcmd != origcmd)
		pci_write_config16(dev, PCI_COMMAND, newcmd);

done:
	return mask_updated;
}
