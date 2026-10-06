/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 * Copyright (C) 2016 Christoph Hellwig.
 *
 * Device-side programming of the (config-space) MSI capability, plus the MSI
 * allocation primitive (pci_msi_alloc).  The capability layout is decoded once
 * at probe (pci_msi_init), so programming an allocation cannot fail.
 *
 * Multi-message MSI is supported: the device is given a 2^k-aligned contiguous
 * block of controller demux slots, the Multiple-Message-Enable field is set to
 * log2(nvec), and the device ORs the vector index into the low bits of the base
 * data so vector i lands on slot base+i at the controller MSI demux.  The
 * message (address/data) is supplied by the controller back-end via
 * brcm_msi_compose_msg() — this file holds no controller specifics.
 */

#define __NOLIBBASE__
#define EXEC_BASE_NAME SysBase /* a local in every function, from its context's sysBase */
#include <debug.h>
#include <bits.h>
#include <errors.h>

#include <pci.h>
#include <pci_irq.h>
#include <pci_capability.h>
#include <pci_io.h>
#include <pci_util.h>
#include <pcie_brcmstb.h>

/* Largest power of two <= v (v >= 1). */
static inline u32 rounddown_pow2(u32 v)
{
	return 1u << (31u - (u32)__builtin_clz(v));
}

/* The mask bits of every vector the device is capable of. */
static inline u32 msi_multi_mask(const struct pci_device *dev)
{
	/* Don't shift by >= width of type */
	if (dev->msi.log2_max_vecs >= 5)
		return 0xffffffffu;
	return ((u32)1 << ((u32)1 << dev->msi.log2_max_vecs)) - 1;
}

/*
 * PCI 2.3 does not specify mask bits for each MSI interrupt.  Attempting to
 * mask all MSI interrupts by clearing the MSI enable bit does not work
 * reliably as devices without an INTx disable bit will then generate a
 * level IRQ which will never be cleared.  Per-vector masking is optional: a
 * device without mask bits ignores this.
 */
void pci_msi_update_mask(struct pci_device *dev, u32 clear, u32 set)
{
	if (!dev->msi.maskable)
		return;

	dev->msi.mask = (dev->msi.mask & ~clear) | set;
	KprintfT("[pcie] %s: device %04lx:%04lx mask 0x%08lx\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, dev->msi.mask);
	pci_write_config32(dev, dev->msi.mask_offset, dev->msi.mask);
}

static void pci_msi_set_enable(struct pci_device *dev, int enable)
{
	u16 control;

	pci_read_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, &control);
	control = (u16)(control & ~PCI_MSI_FLAGS_ENABLE);
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, control);
}

/* Program Multiple-Message-Enable and the address/data registers for the
 * @nvec vectors (a power of two) reserved in dev->active.slots[]. */
static void pci_msi_write_msg(struct pci_device *dev, u32 nvec)
{
	u32 pos = dev->msi.cap_offset;

	/* The controller composes the message for the block's base slot; the
	 * device ORs the vector index into the low bits (slots are 2^k-aligned). */
	u32 address_lo, address_hi;
	u16 data;
	brcm_msi_compose_msg(pci_get_controller(dev->bus), dev->active.slots[0],
						 &address_lo, &address_hi, &data);

	u32 mme = (u32)__builtin_ctz(nvec); /* log2 */
	u16 msgctl;
	pci_read_config16(dev, pos + PCI_MSI_FLAGS, &msgctl);
	msgctl = (u16)(msgctl & ~PCI_MSI_FLAGS_QSIZE);
	msgctl = (u16)(msgctl | mask_insert(mme, PCI_MSI_FLAGS_QSIZE));
	pci_write_config16(dev, pos + PCI_MSI_FLAGS, msgctl);

	pci_write_config32(dev, pos + PCI_MSI_ADDRESS_LO, address_lo);
	if (dev->msi.addr64)
	{
		pci_write_config32(dev, pos + PCI_MSI_ADDRESS_HI, address_hi);
		pci_write_config16(dev, pos + PCI_MSI_DATA_64, data);
	}
	else
	{
		pci_write_config16(dev, pos + PCI_MSI_DATA_32, data);
	}
	/* Ensure that the writes are visible in the device */
	pci_read_config16(dev, pos + PCI_MSI_FLAGS, &msgctl);
	KprintfT("[pcie] %s: device %04lx:%04lx MSI base addr 0x%08lx%08lx data 0x%04lx mme %ld\n",
			 __func__, (ULONG)dev->vendor, (ULONG)dev->device,
			 (ULONG)address_hi, (ULONG)address_lo, (ULONG)data, (LONG)mme);
}

/* Program the capability for the @nvec vectors reserved in dev->active.slots[]
 * and enable MSI; every vector starts masked at the device (pci_irq opens each
 * one when its server is added). */
static void pci_msi_program(struct pci_device *dev, u32 nvec)
{
	/* Disable MSI during setup. */
	pci_msi_set_enable(dev, 0);

	/* All MSIs are unmasked by default; mask them all */
	pci_msi_update_mask(dev, 0, msi_multi_mask(dev));

	/* Program the message (address/data) BEFORE enabling MSI: a device
	 * may latch its MSI address at enable time. */
	pci_msi_write_msg(dev, nvec);

	/* Drop legacy INTx, enable MSI. */
	pci_intx(dev, 0);
	pci_msi_set_enable(dev, 1);

	Kprintf("[pcie] %s: device %04lx:%04lx MSI enabled, %ld vector(s)\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device, (LONG)nvec);
}

/*
 * pci_msi_alloc - reserve and program a multi-message MSI allocation: the
 * largest 2^k-aligned slot block within [min,max] and the device's
 * Multiple-Message-Capable count.
 */
s32 pci_msi_alloc(struct pci_device *dev, u32 min, u32 max)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (!dev->msi.cap_offset)
		return -ENODEV;

	u32 n = max;
	if (dev->msi.log2_max_vecs < 5 && n > (1u << dev->msi.log2_max_vecs))
		n = 1u << dev->msi.log2_max_vecs;

	for (n = rounddown_pow2(n); n >= min; n >>= 1)
	{
		s32 base = pci_irq_slots_alloc_aligned(pcie, n);
		if (base < 0)
			continue;

		for (u32 i = 0; i < n; i++)
			dev->active.slots[i] = base + (s32)i;
		pci_msi_program(dev, n);
		return (s32)n;
	}

	return -ENOSPC;
}

void pci_msi_shutdown(struct pci_device *dev)
{
	Kprintf("[pcie] %s: device %04lx:%04lx shutting down MSI\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device);

	pci_msi_set_enable(dev, 0);

	/* Return the device with MSI unmasked as initial state */
	pci_msi_update_mask(dev, msi_multi_mask(dev), 0);
}

/*
 * Probe: locate the capability, make sure MSI is off (the power-on default,
 * so usually a no-op - it avoids screaming interrupts during boot), and decode
 * the capability layout, which never changes.
 */
void pci_msi_init(struct pci_device *dev)
{
	dev->msi.cap_offset = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!dev->msi.cap_offset)
		return;

	pci_msi_set_enable(dev, 0);

	u16 control;
	pci_read_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, &control);

	dev->msi.addr64 = !!(control & PCI_MSI_FLAGS_64BIT);
	dev->msi.maskable = !!(control & PCI_MSI_FLAGS_MASKBIT);
	dev->msi.log2_max_vecs = (u8)mask_extract(control, PCI_MSI_FLAGS_QMASK);
	dev->msi.mask_offset = (u16)(dev->msi.cap_offset +
								 (dev->msi.addr64 ? PCI_MSI_MASK_64 : PCI_MSI_MASK_32));

	/* The mask register's reset state seeds the shadow */
	if (dev->msi.maskable)
		pci_read_config32(dev, dev->msi.mask_offset, &dev->msi.mask);

	Kprintf("[pcie] %s: device %04lx:%04lx MSI capable: is_64=%ld can_mask=%ld multi_cap=%ld mask_pos=0x%02lx\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device,
			(LONG)dev->msi.addr64, (LONG)dev->msi.maskable,
			(LONG)dev->msi.log2_max_vecs, (ULONG)dev->msi.mask_offset);
}
