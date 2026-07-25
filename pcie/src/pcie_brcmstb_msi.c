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

void brcm_pcie_compose_msi_msg(struct pci_controller *pcie, s32 slot,
							   u32 *addr_lo, u32 *addr_hi, u16 *data)
{
	*addr_lo = u64_lo32(pcie->msi.target_addr);
	*addr_hi = u64_hi32(pcie->msi.target_addr);
	/* The low 16 bits of the doorbell value carry the demux slot the device
	 * must select; the BCM2711 MSI demux uses it as the INTR2 status bit. */
	*data = (u16)((PCIE_MISC_MSI_DATA_CONFIG_VAL_32 & 0xffffu) | (u32)slot);
}

void brcm_msi_bind(struct pci_controller *pcie, s32 slot, struct Interrupt *isr)
{
	if (slot >= 0 && slot < MSI_MAX_VECTORS)
		pcie->msi.vectors[slot] = isr;
}

void brcm_msi_unbind(struct pci_controller *pcie, s32 slot)
{
	if (slot >= 0 && slot < MSI_MAX_VECTORS)
		pcie->msi.vectors[slot] = NULL;
}

/* GIC SPI base: added to a device's INT_x_mapping line to form the absolute
 * GIC-400 interrupt number for INTx delivery. */
#define GIC_SPI_BASE 32

s32 brcm_intx_bind(struct pci_controller *pcie, struct pci_device *dev, struct Interrupt *isr)
{
	if (!pcie->gic400Base)
		return -ENODEV;
	if (AddIntServerEx((ULONG)dev->intx.gic_line + GIC_SPI_BASE, 0, FALSE, isr) != 0)
	{
		Kprintf("[pcie] %s: AddIntServerEx(irq=%ld) failed\n", __func__,
				(LONG)dev->intx.gic_line + GIC_SPI_BASE);
		return -EIO;
	}
	return 0;
}

void brcm_intx_unbind(struct pci_controller *pcie, struct pci_device *dev, struct Interrupt *isr)
{
	if (!pcie->gic400Base)
		return;
	RemIntServerEx((ULONG)dev->intx.gic_line + GIC_SPI_BASE, isr);
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

s32 brcm_pcie_open_gic400(struct pci_controller *pcie)
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

void brcm_pcie_close_gic400(struct pci_controller *pcie)
{
	if (pcie->gic400Base == NULL)
		return;

	CloseLibrary(pcie->gic400Base);
	pcie->gic400Base = NULL;
}

void brcm_pcie_disable_msi(struct pci_controller *pcie)
{
	Kprintf("[pcie] %s: disabling MSI\n", __func__);
	if (pcie->msi.enabled)
	{
		RemIntServerEx((ULONG)pcie->msi.gic_irq, &pcie->msi.isr);
		pcie->msi.enabled = FALSE;
	}
}

static void brcm_msi_set_regs(struct pci_controller *pcie)
{
	KprintfT("[pcie] %s: setting MSI registers\n", __func__);
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
	if (!pcie->gic400Base) /* opened by brcm_pcie_probe before we are called */
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
