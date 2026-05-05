// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2009 - 2019 Broadcom */
#ifdef __INTELLISENSE__
#include <clib/gic400_protos.h>
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#define GIC400_BASE_NAME pcie->gic400Base
#include <proto/gic400.h>
#include <proto/exec.h>
#endif

#include <exec/interrupts.h>

#include <debug.h>

#include <bits.h>
#include <errors.h>
#include <iomem.h>
#include <pci.h>
#include <bcm2711.h>
#include <pci_msi.h>
#include <pci_util.h>
#include <pcie_brcmstb.h>

/* call_interrupt: Invoke interrupt server with Exec ABI.
 * Args: interrupt - Exec interrupt entry; irq - source IRQ number.
 * Returns: void.
 */
static inline void call_interrupt(struct Interrupt *interrupt, ULONG irq)
{
	struct ExecBase *sysBase = *(struct ExecBase **)4UL;

	if (interrupt == NULL || interrupt->is_Code == NULL)
		return;

	__asm__ __volatile__(
		"move.l %[sysbase],%%a6\n\t"
		"move.l %[irq],%%d0\n\t"
		"move.l %[data],%%a1\n\t"
		"jsr (%[code])\n\t"
		:
		: [code] "a"(interrupt->is_Code),
		  [data] "r"(interrupt->is_Data),
		  [irq] "r"(irq),
		  [sysbase] "r"(sysBase)
		: "d0", "d1", "a0", "a1", "a6");
}

static ULONG brcm_pcie_msi_isr(struct ExecBase *execBase asm("a6"), struct pci_controller *pcie asm("a1"), ULONG irq asm("d0"))
{
	(void)execBase;
	(void)irq;

	if (!pcie)
		return 0;

	u32 status = mmio_read32(pcie->base + PCIE_MSI_INTR2_STATUS);

	while (status)
	{
		u32 bit = (u32)__builtin_ctz(status);
		status &= status - 1u;

		/* Clear before calling so a re-assertion during the handler is
		 * not lost — the hardware will re-set the status bit. */
		mmio_write32(1u << bit, pcie->base + PCIE_MSI_INTR2_CLR);

		if (pcie->msi.vectors[bit] != NULL)
			call_interrupt(pcie->msi.vectors[bit], bit);
	}

	return 1;
}

static s32 brcm_pcie_open_gic400(struct pci_controller *pcie)
{
	if (pcie->gic400Base != NULL)
		return 0;

	pcie->gic400Base = OpenLibrary((CONST_STRPTR) "gic400.library", 0);
	if (pcie->gic400Base == NULL)
	{
		Kprintf("[pcie] %s: can't open gic400.library\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static void brcm_pcie_close_gic400(struct pci_controller *pcie)
{
	if (pcie->gic400Base == NULL)
		return;

	CloseLibrary(pcie->gic400Base);
	pcie->gic400Base = NULL;
}

// TODO assign multiple MSI vectors per device
//      see __pci_enable_msi_range()
s32 add_int_server(struct pci_device *dev, struct Interrupt *isr)
{
	KprintfH("[pcie] %s: adding MSI interrupt server for device %04lx:%04lx\n", __func__, dev->vendor, dev->device);
	struct pci_controller *pcie = pci_get_controller(dev->bus);
	if (pcie->msi.num_vectors >= MSI_MAX_VECTORS)
		return -1;

	s32 vector = 0;
	while (vector < MSI_MAX_VECTORS && pcie->msi.vectors[vector])
		vector++;

	if (vector == MSI_MAX_VECTORS)
		return -1;

	pcie->msi.vectors[vector] = isr;
	dev->msi.vector = vector;
	pcie->msi.num_vectors++;
	Kprintf("[pcie] %s: assigned MSI vector %ld to device %04lx:%04lx\n", __func__, vector, dev->vendor, dev->device);

	// TODO set msi_flags.log2_num_vecs to actual number of vectors assigned
	//  dev->msi_flags.log2_num_vecs = ilog2(__roundup_pow_of_two(nvec));
	msi_capability_init(dev, 1);
	pci_write_msg_msi(dev);

	return vector;
}

s32 rem_int_server(struct pci_device *dev)
{
	KprintfH("[pcie] %s: removing MSI interrupt server for device %04x:%04x\n", __func__, dev->vendor, dev->device);
	struct pci_controller *pcie = pci_get_controller(dev->bus);
	if (!pcie)
		return -1;

	pci_msi_shutdown(dev);

	s32 vector = dev->msi.vector;
	if (vector < 0 || vector >= MSI_MAX_VECTORS)
		return -1;

	pcie->msi.vectors[vector] = NULL;

	if (pcie->msi.num_vectors > 0)
		pcie->msi.num_vectors--;

	return 0;
}

void brcm_pcie_disable_msi(struct pci_controller *pcie)
{
	Kprintf("[pcie] %s: disabling MSI\n", __func__);
	if (pcie->msi.enabled)
	{
		RemIntServerEx((ULONG)pcie->msi.gic_irq, &pcie->msi.isr);
		pcie->msi.enabled = FALSE;
	}
	brcm_pcie_close_gic400(pcie);
}

static void brcm_msi_set_regs(struct pci_controller *pcie)
{
	KprintfH("[pcie] %s: setting MSI registers\n", __func__);
	u32 val = (u32)((1ULL << MSI_MAX_VECTORS) - 1ULL);

	mmio_write32(val, pcie->base + PCIE_MSI_INTR2_MASK_CLR);
	mmio_write32(val, pcie->base + PCIE_MSI_INTR2_CLR);

	/*
	 * The 0 bit of PCIE_MISC_MSI_BAR_CONFIG_LO is repurposed to MSI
	 * enable, which we set to 1.
	 */
	mmio_write32(u64_lo32(pcie->msi.target_addr) | 0x1,
				 pcie->base + PCIE_MISC_MSI_BAR_CONFIG_LO);
	mmio_write32(u64_hi32(pcie->msi.target_addr),
				 pcie->base + PCIE_MISC_MSI_BAR_CONFIG_HI);

	val = PCIE_MISC_MSI_DATA_CONFIG_VAL_32;
	mmio_write32(val, pcie->base + PCIE_MISC_MSI_DATA_CONFIG);
}

s32 brcm_pcie_enable_msi(struct pci_controller *pcie)
{
	Kprintf("[pcie] %s: enabling MSI\n", __func__);
	if (pcie->msi.enabled)
		return 0;
	if (brcm_pcie_open_gic400(pcie) < 0)
		return -ENODEV;
	pcie->msi.isr.is_Node.ln_Type = NT_INTERRUPT;
	pcie->msi.isr.is_Node.ln_Name = "xhci_msi_isr";
	pcie->msi.isr.is_Data = (APTR)pcie;
	pcie->msi.isr.is_Code = (APTR)brcm_pcie_msi_isr;

	s32 ret = AddIntServerEx((ULONG)pcie->msi.gic_irq, 0, FALSE, &pcie->msi.isr);
	if (ret < 0)
	{
		Kprintf("[pcie] %s: can't register IRQ %ld\n", __func__, pcie->msi.gic_irq);
		return -ENODEV;
	}

	brcm_msi_set_regs(pcie);
	pcie->msi.enabled = TRUE;

	return 0;
}
