// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2009 - 2019 Broadcom */
#ifdef __INTELLISENSE__
#include <clib/gic400_protos.h>
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME SysBase /* a local in every function, from its context's sysBase */
#define GIC400_BASE_NAME pcie->gic400Base
#include <proto/gic400.h>
#include <proto/exec.h>
#endif

#include <exec/interrupts.h>

#include <debug.h>

#include <bits.h>
#include <errors.h>
#include <iomem.h>
#include <intserver.h>
#include <pci.h>
#include <bcm2711.h>
#include <pcie_brcmstb.h>

/* msi_call_server: Invoke an interrupt server with the Exec server ABI
 * (A1 = is_Data, A5 = is_Code, A6 = SysBase; D0/D1/A0/A1/A5/A6 scratch) plus
 * D0 = demux slot.  A5 is saved inside the asm: the compiler may be using it
 * as the frame pointer, which a clobber cannot protect.
 */
static inline void msi_call_server(const struct Interrupt *server, ULONG slot,
								   struct ExecBase *sysBase)
{
	register ULONG d0 asm("d0") = slot;
	register APTR a1 asm("a1") = server->is_Data;
	register struct ExecBase *a6 asm("a6") = sysBase;

	__asm__ __volatile__(
		"move.l %%a5,-(%%sp)\n\t"
		"move.l %[code],%%a5\n\t"
		"jsr (%%a5)\n\t"
		"move.l (%%sp)+,%%a5"
		: "+d"(d0), "+a"(a1), "+a"(a6)
		: [code] "a"(server->is_Code)
		: "d1", "a0", "cc", "memory");
}

void brcm_msi_compose_msg(struct pci_controller *pcie, s32 slot,
						  u32 *addr_lo, u32 *addr_hi, u16 *data)
{
	*addr_lo = u64_lo32(pcie->msi.target_addr);
	*addr_hi = u64_hi32(pcie->msi.target_addr);
	/* The low 16 bits of the doorbell value carry the demux slot the device
	 * must select; the BCM2711 MSI demux uses it as the INTR2 status bit. */
	*data = (u16)((PCIE_MISC_MSI_DATA_CONFIG_VAL_32 & 0xffffu) | (u32)slot);
}

/*
 * A slot is masked from probe and open only while a server is bound to it.
 * Binding first drops whatever the slot still holds: a message from the
 * previous owner that was already on its way when that owner closed its
 * device gate lands after the unbind and stays latched.
 */
void brcm_msi_slot_bind(struct pci_controller *pcie, s32 slot, struct Interrupt *isr)
{
	pcie->msi.servers[slot] = isr;
	mmio_write32(1u << (u32)slot, pcie->base + PCIE_MSI_INTR2_CLR);
	mmio_write32(1u << (u32)slot, pcie->base + PCIE_MSI_INTR2_MASK_CLR);
}

void brcm_msi_slot_unbind(struct pci_controller *pcie, s32 slot)
{
	mmio_write32(1u << (u32)slot, pcie->base + PCIE_MSI_INTR2_MASK_SET);
	pcie->msi.servers[slot] = NULL;
}

/*
 * The runtime mask, relaxed (no barrier of its own): a mask written from an
 * interrupt server completes before the GIC samples the line again, because
 * gic400 issues a dsb before its EOI; nothing waits on an unmask.
 */
void brcm_msi_slot_mask(struct pci_controller *pcie, s32 slot)
{
	mmio_write32_relaxed(1u << (u32)slot, pcie->base + PCIE_MSI_INTR2_MASK_SET);
}

void brcm_msi_slot_unmask(struct pci_controller *pcie, s32 slot)
{
	mmio_write32_relaxed(1u << (u32)slot, pcie->base + PCIE_MSI_INTR2_MASK_CLR);
}

/*
 * gic400 registration, shared by INTx and the MSI demux.
 *
 * Priority and trigger are properties of the LINE, not of the server: once
 * gic400 chains servers they are set by whoever registers first.  Everything
 * in this stack registers with the same pair, so a shared line cannot end up
 * configured against anyone's wishes.
 *
 * gic400 takes one server per IRQ today, so the second device swizzled onto an
 * INTx line is refused with ALREADY_REGISTERED, which is -EBUSY to our callers.
 * That is gic400's limit, not ours: when it chains, this call starts succeeding
 * and each server is called for every interrupt on the line.  See
 * interrupt-chaining.md in the gic400 component for what that requires of a
 * server.
 */
#define PCIE_GIC_PRIORITY 0	 /* GIC priority for every line bcmpcie registers */
#define PCIE_GIC_EDGE FALSE	 /* level-triggered: PCI INTx is level by spec, and the
							  * BCM2711 MSI aggregation interrupt is level too */

static s32 brcm_gic_add_server(struct pci_controller *pcie, u32 gic_irq, struct Interrupt *isr)
{
	LONG r = AddIntServerEx(gic_irq, PCIE_GIC_PRIORITY, PCIE_GIC_EDGE, isr);
	if (r == 0)
		return 0;
	Kprintf("[pcie] %s: AddIntServerEx(irq=%ld) failed: %ld\n", __func__, (LONG)gic_irq, r);
	return r == GIC400_ERR_ALREADY_REGISTERED ? -EBUSY : -EIO;
}

static void brcm_gic_rem_server(struct pci_controller *pcie, u32 gic_irq, struct Interrupt *isr)
{
	RemIntServerEx(gic_irq, isr);
}

s32 brcm_intx_add_server(struct pci_controller *pcie, struct pci_device *dev, struct Interrupt *isr)
{
	return brcm_gic_add_server(pcie, dev->intx.gic_irq, isr);
}

void brcm_intx_rem_server(struct pci_controller *pcie, struct pci_device *dev, struct Interrupt *isr)
{
	brcm_gic_rem_server(pcie, dev->intx.gic_irq, isr);
}

/*
 * brcm_msi_demux_isr - the demux server gic400 calls (A6 = SysBase, which is
 * passed on to the vector servers: reading $4 is an Amiga-bus cycle).
 *
 * STATUS and CLR use the relaxed accessors.  STATUS is consumed at once; CLR
 * stays ordered ahead of any later access to the controller (Device memory),
 * and gic400 issues a dsb before its EOI.  A CLR landing late could only wipe
 * an MSI that arrived after the device's data write (PCIe ordering puts the
 * MSI behind it), which the woken task's drain sees anyway.
 *
 * STATUS is not filtered by the slot masks: a masked slot that has latched a
 * message is dispatched here when another slot fires.  For a slot masked by
 * its driver that is harmless - in the mask-in-ISR / unmask-after-drain
 * pattern the server re-masks and re-signals a task that drains anyway - and
 * an unbound slot has no server to call.  It keeps the demux free of shared
 * mask state.
 *
 * Returns handled (non-zero, Z clear) only when STATUS had something in it,
 * not-ours (0, Z set) otherwise: gic400 takes one server per IRQ today, but
 * when it chains them a server that always claims the interrupt would cut the
 * walk short and starve whatever else sits on the line.  See
 * interrupt-chaining.md in the gic400 component.
 */
static EMU68_INTSERVER(brcm_msi_demux_isr)
ULONG brcm_msi_demux_isr(struct ExecBase *SysBase asm("a6"),
                         struct pci_controller *pcie asm("a1"), ULONG gic_irq asm("d0"))
{
	(void)gic_irq;

	u32 status = mmio_read32_relaxed(pcie->base + PCIE_MSI_INTR2_STATUS);
	if (!status)
		return 0;

	do
	{
		u32 slot = (u32)__builtin_ctz(status);
		status &= status - 1u;

		/* Clear before calling so a re-assertion during the handler is
		 * not lost — the hardware will re-set the status bit. */
		mmio_write32_relaxed(1u << slot, pcie->base + PCIE_MSI_INTR2_CLR);

		const struct Interrupt *server = pcie->msi.servers[slot];
		if (server != NULL)
			msi_call_server(server, slot, SysBase);
	} while (status);

	return 1;
}

s32 brcm_pcie_open_gic400(struct pci_controller *pcie)
{
	struct ExecBase *SysBase = pcie->sysBase;

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
	struct ExecBase *SysBase = pcie->sysBase;

	CloseLibrary(pcie->gic400Base);
	pcie->gic400Base = NULL;
}

static void brcm_msi_set_regs(struct pci_controller *pcie)
{
	KprintfT("[pcie] %s: setting MSI registers\n", __func__);
	u32 val = (u32)((1ULL << MSI_MAX_VECTORS) - 1ULL);

	/* Every slot stays masked, as probe left it, until a server is bound */
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

s32 brcm_msi_demux_enable(struct pci_controller *pcie)
{
	Kprintf("[pcie] %s: enabling MSI\n", __func__);
	pcie->msi.isr.is_Node.ln_Type = NT_INTERRUPT;
	pcie->msi.isr.is_Node.ln_Name = "bcmpcie MSI demux";
	pcie->msi.isr.is_Data = (APTR)pcie;
	pcie->msi.isr.is_Code = (APTR)brcm_msi_demux_isr;

	s32 ret = brcm_gic_add_server(pcie, (u32)pcie->msi.gic_irq, &pcie->msi.isr);
	if (ret < 0)
		return ret;

	brcm_msi_set_regs(pcie);
	return 0;
}

void brcm_msi_demux_disable(struct pci_controller *pcie)
{
	Kprintf("[pcie] %s: disabling MSI\n", __func__);
	brcm_gic_rem_server(pcie, (u32)pcie->msi.gic_irq, &pcie->msi.isr);
}
