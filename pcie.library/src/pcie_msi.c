/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 * Copyright (C) 2016 Christoph Hellwig.
 */

#include <debug.h>
#include <emu_bits.h>
#include <emu_errors.h>
#include <emu_memory.h>

#include <pci.h>
#include <bcm2711.h> //TODO remove

/**
 * pci_msi_vec_count - Return the number of MSI vectors a device can send
 * @dev: device to report about
 *
 * This function returns the number of MSI vectors a device requested via
 * Multiple Message Capable register. It returns a negative errno if the
 * device is not capable sending MSI interrupts. Otherwise, the call succeeds
 * and returns a power of two, up to a maximum of 2^5 (32), according to the
 * MSI specification.
 **/
int pci_msi_vec_count(struct pci_device *dev)
{
	int ret;
	UWORD msgctl;

	if (!dev->msi.cap)
		return -EINVAL;

	dm_pci_read_config16(dev, dev->msi.cap + PCI_MSI_FLAGS, &msgctl);
	ret = 1 << FIELD_GET(PCI_MSI_FLAGS_QMASK, msgctl);
	KprintfH("[pcie] %s: device %04lx:%04lx supports %ld MSI vectors\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, ret);

	return ret;
}

/*
 * PCI 2.3 does not specify mask bits for each MSI interrupt.  Attempting to
 * mask all MSI interrupts by clearing the MSI enable bit does not work
 * reliably as devices without an INTx disable bit will then generate a
 * level IRQ which will never be cleared.
 */
static inline ULONG msi_multi_mask(struct pci_device *dev)
{
	/* Don't shift by >= width of type */
	if (dev->msi_flags.multi_cap >= 5)
		return 0xffffffff;
	return (1 << (1 << dev->msi_flags.multi_cap)) - 1;
}

/*
 * Helper functions for mask/unmask and MSI message handling
 */

static void pci_msi_update_mask(struct pci_device *dev, ULONG clear, ULONG set)
{
	if (!dev->msi_flags.can_mask)
		return;

	dev->msi.msi_mask &= ~clear;
	dev->msi.msi_mask |= set;
	dm_pci_write_config32(dev, dev->msi_flags.mask_pos,
						  dev->msi.msi_mask);
}

/* Mask/unmask helpers */
static inline void pci_msi_mask(struct pci_device *dev, ULONG mask)
{
	KprintfH("[pcie] %s: device %04lx:%04lx mask 0x%08lx\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, mask);
	pci_msi_update_mask(dev, 0, mask);
}

static inline void pci_msi_unmask(struct pci_device *dev, ULONG mask)
{
	KprintfH("[pcie] %s: device %04lx:%04lx unmask 0x%08lx\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, mask);
	pci_msi_update_mask(dev, mask, 0);
}

/**
 * pci_msi_mask_irq - Generic IRQ chip callback to mask PCI/MSI interrupts
 * @data:	pointer to irqdata associated to that interrupt
 */
void pci_msi_mask_irq(struct pci_device *dev, int irq)
{
	KprintfH("[pcie] %s: device %04lx:%04lx mask IRQ %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, irq);
	pci_msi_mask(dev, BIT(irq));
}

/**
 * pci_msi_unmask_irq - Generic IRQ chip callback to unmask PCI/MSI interrupts
 * @data:	pointer to irqdata associated to that interrupt
 */
void pci_msi_unmask_irq(struct pci_device *dev, int irq)
{
	KprintfH("[pcie] %s: device %04lx:%04lx unmask IRQ %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, irq);
	pci_msi_unmask(dev, BIT(irq));
}

void pci_write_msg_msi(struct pci_device *dev)
{
	KprintfH("[pcie] %s: device %04lx:%04lx writing MSI message\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device);
	struct pci_controller *ctrl = pci_get_controller(dev->bus);
	ULONG address_lo = lower_32_bits(ctrl->msi.msi_target_addr);
	ULONG address_hi = upper_32_bits(ctrl->msi.msi_target_addr);
	ULONG data = (0xffff & PCIE_MISC_MSI_DATA_CONFIG_VAL_32) | dev->msi.irq; // TODO BCM-specific

	int pos = dev->msi.cap;
	UWORD msgctl;

	dm_pci_read_config16(dev, pos + PCI_MSI_FLAGS, &msgctl);
	msgctl &= ~PCI_MSI_FLAGS_QSIZE;
	msgctl |= FIELD_PREP(PCI_MSI_FLAGS_QSIZE, dev->msi_flags.multiple);
	dm_pci_write_config16(dev, pos + PCI_MSI_FLAGS, msgctl);

	dm_pci_write_config32(dev, pos + PCI_MSI_ADDRESS_LO, address_lo);
	if (dev->msi_flags.is_64)
	{
		dm_pci_write_config32(dev, pos + PCI_MSI_ADDRESS_HI, address_hi);
		dm_pci_write_config16(dev, pos + PCI_MSI_DATA_64, data);
	}
	else
	{
		dm_pci_write_config16(dev, pos + PCI_MSI_DATA_32, data);
	}
	/* Ensure that the writes are visible in the device */
	dm_pci_read_config16(dev, pos + PCI_MSI_FLAGS, &msgctl);
	KprintfH("[pcie] %s: device %04lx:%04lx wrote MSI address 0x%08lx%08lx data 0x%04lx\n",
			 __func__, (ULONG)dev->vendor, (ULONG)dev->device,
			 (ULONG)address_hi, (ULONG)address_lo, (ULONG)data);
}

/* PCI/MSI specific functionality */

static void pci_msi_set_enable(struct pci_device *dev, int enable)
{
	Kprintf("[pcie] %s: device %04lx:%04lx %s MSI\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device,
			enable ? "enable" : "disable");
	UWORD control;

	dm_pci_read_config16(dev, dev->msi.cap + PCI_MSI_FLAGS, &control);
	control &= ~PCI_MSI_FLAGS_ENABLE;
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	dm_pci_write_config16(dev, dev->msi.cap + PCI_MSI_FLAGS, control);
}

static int msi_setup_msi_desc(struct pci_device *dev, int nvec)
{
	KprintfH("[pcie] %s: device %04lx:%04lx setting up MSI descriptor with %ld vectors\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, nvec);
	UWORD control;

	/* MSI Entry Initialization */
	_memset(&dev->msi_flags, 0, sizeof(dev->msi_flags));

	dm_pci_read_config16(dev, dev->msi.cap + PCI_MSI_FLAGS, &control);

	// desc.nvec_used = nvec;
	dev->msi_flags.is_64 = !!(control & PCI_MSI_FLAGS_64BIT);
	dev->msi_flags.can_mask = !!(control & PCI_MSI_FLAGS_MASKBIT);
	dev->msi_flags.multi_cap = FIELD_GET(PCI_MSI_FLAGS_QMASK, control);

	if (control & PCI_MSI_FLAGS_64BIT)
		dev->msi_flags.mask_pos = dev->msi.cap + PCI_MSI_MASK_64;
	else
		dev->msi_flags.mask_pos = dev->msi.cap + PCI_MSI_MASK_32;

	/* Save the initial mask status */
	if (dev->msi_flags.can_mask)
		dm_pci_read_config32(dev, dev->msi_flags.mask_pos, &dev->msi.msi_mask);

	Kprintf("[pcie] %s: device %04lx:%04lx MSI flags: is_64=%ld can_mask=%ld multi_cap=%ld mask_pos=0x%02lx\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device,
			dev->msi_flags.is_64,
			dev->msi_flags.can_mask,
			dev->msi_flags.multi_cap,
			(ULONG)dev->msi_flags.mask_pos);

	return 0;
}

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_device data structure of MSI device function
 * @nvec: number of interrupts to allocate
 * @affd: description of automatic IRQ affinity assignments (may be %NULL)
 *
 * Setup the MSI capability structure of the device with the requested
 * number of interrupts.  A return value of zero indicates the successful
 * setup of an entry with the new MSI IRQ.  A negative return value indicates
 * an error, and a positive return value indicates the number of interrupts
 * which could have been allocated.
 */
int msi_capability_init(struct pci_device *dev, int nvec)
{
	KprintfH("[pcie] %s: device %04lx:%04lx setting up MSI capability with %ld vectors\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, nvec);
	int ret;

	/*
	 * Disable MSI during setup in the hardware, but mark it enabled
	 * so that setup code can evaluate it.
	 */
	pci_msi_set_enable(dev, 0);
	dev->msi.enabled = TRUE;

	ret = msi_setup_msi_desc(dev, nvec);
	if (ret)
		goto fail;

	/* All MSIs are unmasked by default; mask them all */
	pci_msi_mask(dev, msi_multi_mask(dev));

	/* Set MSI enabled bits	*/
	//	pci_intx(dev, 0);
	pci_msi_set_enable(dev, 1);

	goto unlock;

fail:
	dev->msi.enabled = FALSE;
unlock:
	return ret;
}

int __pci_enable_msi_range(struct pci_device *dev, int minvec, int maxvec)
{
	int nvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	if (dev->msi.enabled)
	{
		Kprintf("[pcie] %s: MSI already enabled for device %04x:%04x\n",
				__func__, dev->vendor, dev->device);
		return -EINVAL;
	}

	nvec = pci_msi_vec_count(dev);
	if (nvec < 0)
		return nvec;
	if (nvec < minvec)
		return -ENOSPC;

	if (nvec > maxvec)
		nvec = maxvec;

	for (;;)
	{
		rc = msi_capability_init(dev, nvec);
		if (rc == 0)
			return nvec;

		if (rc < 0)
			return rc;
		if (rc < minvec)
			return -ENOSPC;

		nvec = rc;
	}
}

void pci_msi_shutdown(struct pci_device *dev)
{
	Kprintf("[pcie] %s: device %04lx:%04lx shutting down MSI\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device);
	if (!dev || !dev->msi.enabled)
		return;

	pci_msi_set_enable(dev, 0);
	//	pci_intx(dev, 1);
	dev->msi.enabled = FALSE;

	/* Return the device with MSI unmasked as initial states */
	pci_msi_unmask(dev, msi_multi_mask(dev));
}

/*
 * Disable the MSI hardware to avoid screaming interrupts during boot.
 * This is the power on reset default so usually this should be a noop.
 */
void pci_msi_init(struct pci_device *dev)
{
	UWORD ctrl;

	dev->msi.cap = dm_pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!dev->msi.cap)
		return;

	Kprintf("[pcie] %s: device %04lx:%04lx is MSI capable\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device);

	dm_pci_read_config16(dev, dev->msi.cap + PCI_MSI_FLAGS, &ctrl);
	if (ctrl & PCI_MSI_FLAGS_ENABLE)
	{
		dm_pci_write_config16(dev, dev->msi.cap + PCI_MSI_FLAGS,
							  ctrl & ~PCI_MSI_FLAGS_ENABLE);
	}
}
