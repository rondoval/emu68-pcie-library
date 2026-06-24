/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 * Copyright (C) 2016 Christoph Hellwig.
 *
 * Device-side programming of the (config-space) MSI capability, plus the MSI
 * allocation primitive (pci_msi_alloc).  The active vector allocation lives in
 * dev->active (reserved from the pci_irq slot pool); this module programs the
 * capability to match.  Multi-message MSI is supported: the device is given a
 * 2^k-aligned contiguous block of controller demux slots, the
 * Multiple-Message-Enable field is set to log2(nvec), and the device ORs the
 * vector index into the low bits of the base data so vector i lands on slot
 * base+i at the controller MSI demux.  The message (address/data) is supplied
 * by the controller back-end via brcm_pcie_compose_msi_msg() — this file holds
 * no controller specifics.
 */

#include <debug.h>
#include <bits.h>
#include <errors.h>

#include <pci.h>
#include <pci_irq.h>
#include <pci_msi.h>
#include <pci_capability.h>
#include <pci_io.h>
#include <pci_util.h>
#include <pci_int.h>
#include <pcie_brcmstb.h>

/* log2 of a power-of-two value. */
static inline u8 ilog2_pow2(u32 v)
{
	return (u8)__builtin_ctz(v);
}

/* Largest power of two <= v (v >= 1). */
static inline u32 rounddown_pow2(u32 v)
{
	return v ? (1u << (31u - (u32)__builtin_clz(v))) : 0;
}

/* Number of MSI vectors a device can send: reads the Multiple Message Capable
 * field, returns a power of two (max 32), or negative errno if no MSI cap. */
static s32 pci_msi_vec_count(struct pci_device *dev)
{
	if (!dev->msi.cap_offset)
		return -EINVAL;

	u16 msgctl;
	pci_read_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, &msgctl);
	return 1 << mask_extract(msgctl, PCI_MSI_FLAGS_QMASK);
}

/*
 * PCI 2.3 does not specify mask bits for each MSI interrupt.  Attempting to
 * mask all MSI interrupts by clearing the MSI enable bit does not work
 * reliably as devices without an INTx disable bit will then generate a
 * level IRQ which will never be cleared.
 */
static inline u32 msi_multi_mask(struct pci_device *dev)
{
	/* Don't shift by >= width of type */
	if (dev->msi.log2_max_vecs >= 5)
		return 0xffffffffu;
	return ((u32)1 << ((u32)1 << dev->msi.log2_max_vecs)) - 1;
}

/*
 * Helper functions for mask/unmask and MSI message handling
 */

static void pci_msi_update_mask(struct pci_device *dev, u32 clear, u32 set)
{
	if (!dev->msi.maskable)
		return;

	dev->msi.mask &= ~clear;
	dev->msi.mask |= set;
	pci_write_config32(dev, dev->msi.mask_offset, dev->msi.mask);
}

/* Mask/unmask helpers */
static inline void pci_msi_mask(struct pci_device *dev, u32 mask)
{
	KprintfH("[pcie] %s: device %04lx:%04lx mask 0x%08lx\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, mask);
	pci_msi_update_mask(dev, 0, mask);
}

static inline void pci_msi_unmask(struct pci_device *dev, u32 mask)
{
	KprintfH("[pcie] %s: device %04lx:%04lx unmask 0x%08lx\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, mask);
	pci_msi_update_mask(dev, mask, 0);
}

/**
 * pci_msi_mask_irq() - Mask a single MSI vector (by local vector index)
 *
 * @irq: vector index within the device's allocation (0..nvec-1)
 */
void pci_msi_mask_irq(struct pci_device *dev, int irq)
{
	KprintfH("[pcie] %s: device %04lx:%04lx mask IRQ %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, irq);
	pci_msi_mask(dev, BIT(irq));
}

/**
 * pci_msi_unmask_irq() - Unmask a single MSI vector (by local vector index)
 */
void pci_msi_unmask_irq(struct pci_device *dev, int irq)
{
	KprintfH("[pcie] %s: device %04lx:%04lx unmask IRQ %ld\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device, irq);
	pci_msi_unmask(dev, BIT(irq));
}

/* Program the MSI capability's address/data registers from dev->active (base
 * slot + Multiple-Message-Enable).  Internal to pci_msi_capability_init(). */
static void pci_write_msg_msi(struct pci_device *dev)
{
	struct pci_controller *ctrl = pci_get_controller(dev->bus);
	u32 pos = dev->msi.cap_offset;

	/* The controller composes the message for the block's base slot; the
	 * device ORs the vector index into the low bits (slots are 2^k-aligned). */
	u32 address_lo, address_hi;
	u16 data;
	brcm_pcie_compose_msi_msg(ctrl, dev->active.slots[0], &address_lo, &address_hi, &data);

	/* Program Multiple-Message-Enable = log2(nvec) */
	u16 msgctl;
	pci_read_config16(dev, pos + PCI_MSI_FLAGS, &msgctl);
	msgctl = (u16)(msgctl & ~PCI_MSI_FLAGS_QSIZE);
	msgctl = (u16)(msgctl | mask_insert(dev->msi.log2_num_vecs, PCI_MSI_FLAGS_QSIZE));
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
	KprintfH("[pcie] %s: device %04lx:%04lx MSI base addr 0x%08lx%08lx data 0x%04lx mme %ld\n",
			 __func__, (ULONG)dev->vendor, (ULONG)dev->device,
			 (ULONG)address_hi, (ULONG)address_lo, (ULONG)data,
			 (LONG)dev->msi.log2_num_vecs);
}

/* PCI/MSI specific functionality */

static void pci_msi_set_enable(struct pci_device *dev, int enable)
{
	u16 control;

	pci_read_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, &control);
	control = (u16)(control & ~PCI_MSI_FLAGS_ENABLE);
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, control);
}

static s32 msi_setup_msi_desc(struct pci_device *dev, u32 nvec)
{
	u16 control;

	pci_read_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, &control);

	dev->msi.addr64 = !!(control & PCI_MSI_FLAGS_64BIT);
	dev->msi.maskable = !!(control & PCI_MSI_FLAGS_MASKBIT);
	dev->msi.log2_max_vecs = (u8)mask_extract(control, PCI_MSI_FLAGS_QMASK);
	dev->msi.log2_num_vecs = ilog2_pow2(nvec);

	if (control & PCI_MSI_FLAGS_64BIT)
		dev->msi.mask_offset = (u16)(dev->msi.cap_offset + PCI_MSI_MASK_64);
	else
		dev->msi.mask_offset = (u16)(dev->msi.cap_offset + PCI_MSI_MASK_32);

	/* Save the initial mask status */
	if (dev->msi.maskable)
		pci_read_config32(dev, dev->msi.mask_offset, &dev->msi.mask);

	Kprintf("[pcie] %s: device %04lx:%04lx MSI flags: is_64=%ld can_mask=%ld multi_cap=%ld num=%ld mask_pos=0x%02lx\n",
			__func__, (ULONG)dev->vendor, (ULONG)dev->device,
			(LONG)dev->msi.addr64, (LONG)dev->msi.maskable,
			(LONG)dev->msi.log2_max_vecs, (LONG)dev->msi.log2_num_vecs,
			(ULONG)dev->msi.mask_offset);

	return 0;
}

/*
 * pci_msi_capability_init - configure a device's MSI capability (internal to
 * pci_msi_alloc).  Programs the capability for @nvec vectors (power of two),
 * with the controller demux slots already reserved in dev->active.slots[]; all
 * vectors start masked.  Returns 0 on success, negative on error.
 */
static s32 pci_msi_capability_init(struct pci_device *dev, u32 nvec)
{
	s32 ret;

	/* Disable MSI during setup. */
	pci_msi_set_enable(dev, 0);

	ret = msi_setup_msi_desc(dev, nvec);
	if (ret)
		return ret;

	/* All MSIs are unmasked by default; mask them all */
	pci_msi_mask(dev, msi_multi_mask(dev));

	/* Program the message (address/data) BEFORE enabling MSI: a device
	 * may latch its MSI address at enable time. */
	pci_write_msg_msi(dev);

	/* Drop legacy INTx, enable MSI. */
	pci_intx(dev, 0);
	pci_msi_set_enable(dev, 1);

	return 0;
}

/**
 * pci_msi_alloc - reserve and program a multi-message MSI allocation
 *
 * See pci_msi.h.  Carved from the former pci_irq_alloc_msi() MSI path: reserve
 * a 2^k-aligned block within the device's Multiple-Message-Capable count, then
 * program the capability; roll back and try a smaller block on failure.
 */
s32 pci_msi_alloc(struct pci_device *dev, u32 min, u32 max)
{
	struct pci_controller *pcie = pci_get_controller(dev->bus);

	if (!pcie || !pcie->msi.enabled || !dev->msi.cap_offset)
		return -ENODEV;
	if (min < 1)
		min = 1;
	if (max > MSI_MAX_VECTORS)
		max = MSI_MAX_VECTORS;
	if (max < min)
		return -ERANGE;

	s32 cap = pci_msi_vec_count(dev);
	if (cap <= 0)
		return -ENODEV;

	u32 n = max;
	if (n > (u32)cap)
		n = (u32)cap;

	for (n = rounddown_pow2(n); n >= min; n >>= 1)
	{
		s32 base = pci_irq_slots_alloc_aligned(pcie, n);
		if (base >= 0)
		{
			for (u32 i = 0; i < n; i++)
				dev->active.slots[i] = base + (s32)i;
			dev->active.mode = PCI_IRQT_MSI;
			dev->active.nvec = (u16)n;
			if (pci_msi_capability_init(dev, n) == 0)
				return (s32)n;
			pci_irq_slots_free(pcie, dev->active.slots, n);
			dev->active.mode = PCI_IRQT_NONE;
			dev->active.nvec = 0;
		}
		if (n == 1)
			break; /* avoid n >>= 1 underflowing the loop guard */
	}

	return -ENOSPC;
}

void pci_msi_shutdown(struct pci_device *dev)
{
	if (!dev || !dev->msi.cap_offset)
		return;

	Kprintf("[pcie] %s: device %04lx:%04lx shutting down MSI\n", __func__,
			(ULONG)dev->vendor, (ULONG)dev->device);

	pci_msi_set_enable(dev, 0);

	/* Return the device with MSI unmasked as initial state */
	pci_msi_unmask(dev, msi_multi_mask(dev));
}

/*
 * Disable the MSI hardware to avoid screaming interrupts during boot.
 * This is the power on reset default so usually this should be a noop.
 */
void pci_msi_init(struct pci_device *dev)
{
	dev->msi.cap_offset = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!dev->msi.cap_offset)
		return;

	KprintfH("[pcie] %s: device %04lx:%04lx is MSI capable\n", __func__,
			 (ULONG)dev->vendor, (ULONG)dev->device);

	u16 ctrl;
	pci_read_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS, &ctrl);
	if (ctrl & PCI_MSI_FLAGS_ENABLE)
	{
		pci_write_config16(dev, dev->msi.cap_offset + PCI_MSI_FLAGS,
						   ctrl & ~PCI_MSI_FLAGS_ENABLE);
	}
}
