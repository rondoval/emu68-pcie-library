// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI autoconfiguration library
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright 2000 MontaVista Software Inc.
 * Copyright (c) 2021  Maciej W. Rozycki <macro@orcam.me.uk>
 *
 * Modifications for driver model:
 * Copyright 2015 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <pci.h>
#include <debug.h>
#include <timing.h>

/* the user can define CFG_SYS_PCI_CACHE_LINE_SIZE to avoid problems */
#ifndef CFG_SYS_PCI_CACHE_LINE_SIZE
#define CFG_SYS_PCI_CACHE_LINE_SIZE 8
#endif

static void pciauto_region_init(struct pci_region *res)
{
	/*
	 * Avoid allocating PCI resources from address 0 -- this is illegal
	 * according to PCI 2.1 and moreover, this is known to cause Linux IDE
	 * drivers to fail. Use a reasonable starting value of 0x1000 instead
	 * if the bus start address is below 0x1000.
	 */
	res->bus_lower = res->bus_start < 0x1000 ? 0x1000 : res->bus_start;
}

static void pciauto_region_align(struct pci_region *res, pci_size_t size)
{
	res->bus_lower = ((res->bus_lower - 1) | (size - 1)) + 1;
}

static s32 pciauto_region_allocate(struct pci_region *res, pci_size_t size, pci_addr_t *bar, BOOL supports_64bit)
{
	pci_addr_t addr;

	if (!res)
	{
		Kprintf("[pcie] %s: No resource\n", __func__);
		goto error;
	}

	addr = ((res->bus_lower - 1) | (size - 1)) + 1;

	if (addr - res->bus_start + size > res->size)
	{
		Kprintf("[pcie] %s: No room in resource, avail start=%lx%08lx / size=%lx%08lx, need=%lx%08lx\n", __func__,
				(ULONG)((u64)(res->bus_lower) >> 32), (ULONG)(res->bus_lower & 0xffffffff),
				(ULONG)((u64)(res->size) >> 32), (ULONG)(res->size & 0xffffffff),
				(ULONG)((u64)(size) >> 32), (ULONG)(size & 0xffffffff));
		goto error;
	}

	if (u64_hi32(addr) && !supports_64bit)
	{
		Kprintf("[pcie] %s: Cannot assign 64-bit address to 32-bit-only resource\n", __func__);
		goto error;
	}

	res->bus_lower = addr + size;

	Kprintf("[pcie] %s: address=0x%lx%08lx bus_lower=0x%lx%08lx\n", __func__, (ULONG)((u64)(addr) >> 32), (ULONG)(addr & 0xffffffff), (ULONG)((u64)(res->bus_lower) >> 32), (ULONG)(res->bus_lower & 0xffffffff));

	*bar = addr;
	return 0;

error:
	*bar = (pci_addr_t)-1;
	return -1;
}

static void pciauto_show_region(const char *name, struct pci_region *region)
{
	(void)name;
	pciauto_region_init(region);
	KprintfH("[pcie] %s: Bus %s region: [%lx%08lx-%lx%08lx], Physical Memory [%lx%08lx-%lx%08lx]\n",
			 __func__,
			 name,
			 (ULONG)((u64)(region->bus_start) >> 32),
			 (ULONG)(region->bus_start & 0xffffffff),
			 (ULONG)((u64)(region->bus_start + region->size - 1) >> 32),
			 (ULONG)((region->bus_start + region->size - 1) & 0xffffffff),
			 (ULONG)(region->phys_start >> 32),
			 (ULONG)(region->phys_start & 0xffffffff),
			 (ULONG)((region->phys_start + region->size - 1) >> 32),
			 (ULONG)((region->phys_start + region->size - 1) & 0xffffffff));
}

void pciauto_config_init(struct pci_controller *ctrl)
{
	ctrl->pci_io = NULL;
	ctrl->pci_mem = NULL;
	ctrl->pci_prefetch = NULL;

	for (u32 i = 0; i < ctrl->region_count; i++)
	{
		switch (ctrl->regions[i].flags)
		{
		case PCI_REGION_IO:
			if (!ctrl->pci_io ||
				ctrl->pci_io->size < ctrl->regions[i].size)
				ctrl->pci_io = ctrl->regions + i;
			break;
		case PCI_REGION_MEM:
			if (!ctrl->pci_mem ||
				ctrl->pci_mem->size < ctrl->regions[i].size)
				ctrl->pci_mem = ctrl->regions + i;
			break;
		case (PCI_REGION_MEM | PCI_REGION_PREFETCH):
			if (!ctrl->pci_prefetch ||
				ctrl->pci_prefetch->size < ctrl->regions[i].size)
				ctrl->pci_prefetch = ctrl->regions + i;
			break;
		}
	}

	if (ctrl->pci_mem)
		pciauto_show_region("Memory", ctrl->pci_mem);
	if (ctrl->pci_prefetch)
		pciauto_show_region("Prefetchable Mem", ctrl->pci_prefetch);
	if (ctrl->pci_io)
		pciauto_show_region("I/O", ctrl->pci_io);
}

static void pciauto_setup_device(struct pci_device *dev,
								 struct pci_region *mem,
								 struct pci_region *prefetch,
								 struct pci_region *io)
{
	u32 bar_response;
	pci_size_t bar_size;
	u16 _cmdstat_raw = 0;
	u32 cmdstat = 0;
	u32 bar;
	u32 bar_nr = 0;
	u32 bars_num = 0;
	u8 header_type;
	u16 rom_addr;
	pci_addr_t bar_value;
	struct pci_region *bar_res = NULL;
	BOOL found_mem64 = FALSE;
	u16 class;

	pci_read_config16(dev, PCI_COMMAND, &_cmdstat_raw);
	cmdstat = _cmdstat_raw;
	cmdstat = (cmdstat & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) | PCI_COMMAND_MASTER;

	pci_read_config8(dev, PCI_HEADER_TYPE, &header_type);
	header_type &= 0x7f;

	switch (header_type)
	{
	case PCI_HEADER_TYPE_NORMAL:
		bars_num = 6;
		break;
	case PCI_HEADER_TYPE_BRIDGE:
		bars_num = 2;
		break;
	case PCI_HEADER_TYPE_CARDBUS:
		/* CardBus header does not have any BAR */
		bars_num = 0;
		break;
	default:
		/* Skip configuring BARs for unknown header types */
		bars_num = 0;
		break;
	}

	for (bar = PCI_BASE_ADDRESS_0; bar < PCI_BASE_ADDRESS_0 + (bars_num * 4); bar += 4)
	{
		s32 ret = 0;

		/* Tickle the BAR and get the response */
		pci_write_config32(dev, bar, 0xffffffff);
		pci_read_config32(dev, bar, &bar_response);

		/* If BAR is not implemented (or invalid) go to the next BAR */
		if (!bar_response || bar_response == 0xffffffff)
			continue;

		found_mem64 = FALSE;

		/* Check the BAR type and set our address mask */
		if (bar_response & PCI_BASE_ADDRESS_SPACE)
		{
			bar_size = (u32)(~(bar_response & PCI_BASE_ADDRESS_IO_MASK) + 1u);

			bar_res = io;

			Kprintf("[pcie] %s: BAR %ld, I/O, size=0x%lx, ", __func__, bar_nr, bar_size);
		}
		else
		{
			if ((bar_response & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64)
			{
				u32 bar_response_upper;
				u64 bar64;

				pci_write_config32(dev, bar + 4, 0xffffffff);
				pci_read_config32(dev, bar + 4, &bar_response_upper);

				bar64 = ((u64)bar_response_upper << 32) | bar_response;

				bar_size = (pci_size_t)(~(bar64 & PCI_BASE_ADDRESS_MEM_MASK) + 1);
				found_mem64 = TRUE;
			}
			else
			{
				bar_size = (u32)(~(bar_response & PCI_BASE_ADDRESS_MEM_MASK) + 1u);
			}

			if (prefetch && (bar_response & PCI_BASE_ADDRESS_MEM_PREFETCH)
#ifdef CONFIG_SYS_PCI_64BIT
				&& (found_mem64 || prefetch->bus_lower < 0x100000000ULL)
#endif
			)
				bar_res = prefetch;
			else
				bar_res = mem;

			Kprintf("[pcie] %s: BAR %ld, %s%s, size=0x%lx%08lx\n", __func__, bar_nr, bar_res == prefetch ? "Prf" : "Mem", found_mem64 ? "64" : "", (ULONG)((u64)(bar_size) >> 32), (ULONG)(bar_size & 0xffffffff));
		}

		ret = pciauto_region_allocate(bar_res, bar_size, &bar_value, found_mem64);
		if (ret)
			Kprintf("[pcie] %s: Failed autoconfig bar %ld\n", __func__, bar_nr);

		if (!ret)
		{
			/* Write it out and update our limit */
			pci_write_config32(dev, bar, (u32)bar_value);

			if (found_mem64)
			{
				bar += 4;
#ifdef CONFIG_SYS_PCI_64BIT
				pci_write_config32(dev, bar,
								   (ULONG)(bar_value >> 32));
#else
				/*
				 * If we are a 64-bit decoder then increment to
				 * the upper 32 bits of the bar and force it to
				 * locate in the lower 4GB of memory.
				 */
				pci_write_config32(dev, bar, 0x00000000);
#endif
			}
		}

		cmdstat |= (bar_response & PCI_BASE_ADDRESS_SPACE) ? PCI_COMMAND_IO : PCI_COMMAND_MEMORY;

		bar_nr++;
	}

	/* Configure the expansion ROM address */
	if (header_type == PCI_HEADER_TYPE_NORMAL ||
		header_type == PCI_HEADER_TYPE_BRIDGE)
	{
		rom_addr = (header_type == PCI_HEADER_TYPE_NORMAL) ? PCI_ROM_ADDRESS : PCI_ROM_ADDRESS1;
		pci_write_config32(dev, rom_addr, 0xfffffffe);
		pci_read_config32(dev, rom_addr, &bar_response);
		if (bar_response)
		{
			bar_size = -(bar_response & ~1U);
			Kprintf("[pcie] %s: ROM, size=%#x, ", __func__, bar_size);
			if (pciauto_region_allocate(mem, bar_size, &bar_value,
										FALSE) == 0)
			{
				pci_write_config32(dev, rom_addr, (u32)bar_value);
			}
			cmdstat |= PCI_COMMAND_MEMORY;
			Kprintf("\n");
		}
	}

	/* PCI_COMMAND_IO must be set for VGA device */
	pci_read_config16(dev, PCI_CLASS_DEVICE, &class);
	if (class == PCI_CLASS_DISPLAY_VGA)
		cmdstat |= PCI_COMMAND_IO;

	pci_write_config16(dev, PCI_COMMAND, cmdstat);
	pci_write_config8(dev, PCI_CACHE_LINE_SIZE,
					  CFG_SYS_PCI_CACHE_LINE_SIZE);
	pci_write_config8(dev, PCI_LATENCY_TIMER, 0x80);

	pci_assign_irq(dev);

	/* Enusre MSI is disabled and preconfigure MSI message */
	pci_msi_init(dev);
}

/*
 * Check if the link of a downstream PCIe port operates correctly.
 *
 * For that check if the optional Data Link Layer Link Active status gets
 * on within a 200ms period or failing that wait until the completion of
 * that period and check if link training has shown the completed status
 * continuously throughout the second half of that period.
 *
 * Observation with the ASMedia ASM2824 Gen 3 switch indicates it takes
 * 11-44ms to indicate the Data Link Layer Link Active status at 2.5GT/s,
 * though it may take a couple of link training iterations.
 */
static BOOL pciauto_exp_link_stable(struct pci_device *dev, u32 pcie_off)
{
	u32 loops = 0, trcount = 0, ntrcount = 0, flips = 0;
	BOOL dllla, lnktr;
	u16 exp_lnksta;
	u32 end;

	pci_read_config16(dev, pcie_off + PCI_EXP_LNKSTA, &exp_lnksta);
	BOOL plnktr = !!(exp_lnksta & PCI_EXP_LNKSTA_LT);

	end = get_time() + 200000;
	do
	{
		pci_read_config16(dev, pcie_off + PCI_EXP_LNKSTA,
						  &exp_lnksta);
		dllla = !!(exp_lnksta & PCI_EXP_LNKSTA_DLLLA);
		lnktr = !!(exp_lnksta & PCI_EXP_LNKSTA_LT);

		flips += (plnktr != lnktr ? 1u : 0u);
		if (lnktr)
		{
			ntrcount = 0;
			trcount++;
		}
		else
		{
			ntrcount++;
		}
		loops++;

		plnktr = lnktr;
	} while (!dllla && get_time() < end);

	pci_dev_t bdf = pci_get_bdf(dev);
	Kprintf("[pcie] %s: %02x.%02x.%02x: Fixup link: DL active: %lu; "
			"%3lu flips, %6lu loops of which %6lu while training, "
			"final %6lu stable\n",
			__func__, PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf),
			(ULONG)dllla,
			flips, loops,
			trcount, ntrcount);

	return dllla || ntrcount >= loops / 2;
}

/*
 * Retrain the link of a downstream PCIe port by hand if necessary.
 *
 * This is needed at least where a downstream port of the ASMedia ASM2824
 * Gen 3 switch is wired to the upstream port of the Pericom PI7C9X2G304
 * Gen 2 switch, and observed with the Delock Riser Card PCI Express x1 >
 * 2 x PCIe x1 device, P/N 41433, plugged into the SiFive HiFive Unmatched
 * board.
 *
 * In such a configuration the switches are supposed to negotiate the link
 * speed of preferably 5.0GT/s, falling back to 2.5GT/s.  However the link
 * continues switching between the two speeds indefinitely and the data
 * link layer never reaches the active state, with link training reported
 * repeatedly active ~84% of the time.  Forcing the target link speed to
 * 2.5GT/s with the upstream ASM2824 device makes the two switches talk to
 * each other correctly however.  And more interestingly retraining with a
 * higher target link speed afterwards lets the two successfully negotiate
 * 5.0GT/s.
 *
 * As this can potentially happen with any device and is cheap in the case
 * of correctly operating hardware, let's do it for all downstream ports,
 * for root complexes, PCIe switches and PCI/PCI-X to PCIe bridges.
 *
 * First check if automatic link training may have failed to complete, as
 * indicated by the optional Data Link Layer Link Active status being off
 * and the Link Bandwidth Management Status indicating that hardware has
 * changed the link speed or width in an attempt to correct unreliable
 * link operation.  If this is the case, then check if the link operates
 * correctly by seeing whether it is being trained excessively.  If it is,
 * then conclude the link is broken.
 *
 * In that case restrict the speed to 2.5GT/s, observing that the Target
 * Link Speed field is sticky and therefore the link will stay restricted
 * even after a device reset is later made by an OS that is unaware of the
 * problem.  With the speed restricted request that the link be retrained
 * and check again if the link operates correctly.  If not, then set the
 * Target Link Speed back to the original value.
 *
 * This requires the presence of the Link Control 2 register, so make sure
 * the PCI Express Capability Version is at least 2.  Also don't try, for
 * obvious reasons, to limit the speed if 2.5GT/s is the only link speed
 * supported.
 */
static void pciauto_exp_fixup_link(struct pci_device *dev, u32 pcie_off)
{
	u16 exp_lnksta, exp_lnkctl, exp_lnkctl2;
	u16 exp_flags, exp_type, exp_version;
	u32 exp_lnkcap;
	pci_dev_t bdf;

	pci_read_config16(dev, pcie_off + PCI_EXP_FLAGS, &exp_flags);
	exp_version = exp_flags & PCI_EXP_FLAGS_VERS;
	if (exp_version < 2)
		return;

	exp_type = (exp_flags & PCI_EXP_FLAGS_TYPE) >> 4;
	switch (exp_type)
	{
	case PCI_EXP_TYPE_ROOT_PORT:
	case PCI_EXP_TYPE_DOWNSTREAM:
	case PCI_EXP_TYPE_PCIE_BRIDGE:
		break;
	default:
		return;
	}

	pci_read_config32(dev, pcie_off + PCI_EXP_LNKCAP, &exp_lnkcap);
	if ((exp_lnkcap & PCI_EXP_LNKCAP_SLS) <= PCI_EXP_LNKCAP_SLS_2_5GB)
		return;

	pci_read_config16(dev, pcie_off + PCI_EXP_LNKSTA, &exp_lnksta);
	if ((exp_lnksta & (PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_DLLLA)) != PCI_EXP_LNKSTA_LBMS)
		return;

	if (pciauto_exp_link_stable(dev, pcie_off))
		return;

	bdf = pci_get_bdf(dev);
	Kprintf("[pcie] %s: %02lx.%02lx.%02lx: Downstream link non-functional\n", __func__, PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));
	Kprintf("[pcie] %s: %02lx.%02lx.%02lx: Retrying with speed restricted to 2.5GT/s...\n", __func__, PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));

	pci_read_config16(dev, pcie_off + PCI_EXP_LNKCTL, &exp_lnkctl);
	pci_read_config16(dev, pcie_off + PCI_EXP_LNKCTL2, &exp_lnkctl2);

	pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL2,
					   (u32)((exp_lnkctl2 & ~PCI_EXP_LNKCTL2_TLS) |
							 PCI_EXP_LNKCTL2_TLS_2_5GT));
	pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL,
					   exp_lnkctl | PCI_EXP_LNKCTL_RL);

	if (pciauto_exp_link_stable(dev, pcie_off))
	{
		Kprintf("[pcie] %s: %02lx.%02lx.%02lx: Succeeded!\n",
				__func__, PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));
	}
	else
	{
		Kprintf("[pcie] %s: %02lx.%02lx.%02lx: Failed!\n",
				__func__, PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));

		pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL2,
						   exp_lnkctl2);
		pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL,
						   exp_lnkctl | PCI_EXP_LNKCTL_RL);
	}
}

void pciauto_prescan_setup_bridge(struct pci_bus *brd)
{
	u16 _cmdstat_raw;
	u32 cmdstat;
	u16 prefechable_64;
	u8 io_32;
	u32 pcie_off;

	struct pci_controller *ctlr = pci_get_controller(brd);
	struct pci_device *dev = brd->pci_bridge;

	struct pci_region *pci_mem = ctlr->pci_mem;
	struct pci_region *pci_prefetch = ctlr->pci_prefetch;
	struct pci_region *pci_io = ctlr->pci_io;

	pci_read_config16(dev, PCI_COMMAND, &_cmdstat_raw);
	cmdstat = _cmdstat_raw;
	pci_read_config16(dev, PCI_PREF_MEMORY_BASE, &prefechable_64);
	prefechable_64 = (u16)(prefechable_64 & PCI_PREF_RANGE_TYPE_MASK);
	pci_read_config8(dev, PCI_IO_BASE, &io_32);
	io_32 &= PCI_IO_RANGE_TYPE_MASK;

	/* Configure bus number registers */
	pci_write_config8(dev, PCI_PRIMARY_BUS, PCI_BUS(pci_get_bdf(dev)) - ctlr->bus_number_base);
	pci_write_config8(dev, PCI_SECONDARY_BUS, brd->bus_number - ctlr->bus_number_base);
	pci_write_config8(dev, PCI_SUBORDINATE_BUS, 0xff);
	KprintfH("[pcie] %s: Bus %ld primary bus set to %ld, secondary bus set to %ld\n", __func__, brd->bus_number, PCI_BUS(pci_get_bdf(dev)) - ctlr->bus_number_base, brd->bus_number - ctlr->bus_number_base);

	if (pci_mem)
	{
		/* Round memory allocator */
		pciauto_region_align(pci_mem, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		/*
		 * Set up memory and I/O filter limits, assume 32-bit
		 * I/O space
		 */
		pci_write_config16(dev, PCI_MEMORY_BASE, ((pci_mem->bus_lower & 0xfff00000) >> 16) & PCI_MEMORY_RANGE_MASK);
		KprintfH("[pcie] %s: Bus %ld memory base set to 0x%lx%08lx\n", __func__, brd->bus_number, (ULONG)((u64)(pci_mem->bus_lower) >> 32), (ULONG)(pci_mem->bus_lower & 0xffffffff));

		cmdstat |= PCI_COMMAND_MEMORY;
	}

	if (pci_prefetch)
	{
		/* Round memory allocator */
		pciauto_region_align(pci_prefetch, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		/*
		 * Set up memory and I/O filter limits, assume 32-bit
		 * I/O space
		 */
		pci_write_config16(dev, PCI_PREF_MEMORY_BASE, (u32)(((pci_prefetch->bus_lower & 0xfff00000) >> 16) & PCI_PREF_RANGE_MASK) | prefechable_64);
		KprintfH("[pcie] %s: Bus %ld prefetchable memory base set to 0x%lx%08lx\n", __func__, brd->bus_number, (ULONG)((u64)(pci_prefetch->bus_lower) >> 32), (ULONG)(pci_prefetch->bus_lower & 0xffffffff));
		if (prefechable_64 == PCI_PREF_RANGE_TYPE_64)
#ifdef CONFIG_SYS_PCI_64BIT
			pci_write_config32(dev, PCI_PREF_BASE_UPPER32,
							   (u32)(pci_prefetch->bus_lower >> 32));
#else
			pci_write_config32(dev, PCI_PREF_BASE_UPPER32, 0x0);
#endif

		cmdstat |= PCI_COMMAND_MEMORY;
	}
	else
	{
		/* We don't support prefetchable memory for now, so disable */
		pci_write_config16(dev, PCI_PREF_MEMORY_BASE, 0xfff0 | prefechable_64);
		pci_write_config16(dev, PCI_PREF_MEMORY_LIMIT, 0x0 | prefechable_64);
		if (prefechable_64 == PCI_PREF_RANGE_TYPE_64)
		{
			pci_write_config16(dev, PCI_PREF_BASE_UPPER32, 0x0);
			pci_write_config16(dev, PCI_PREF_LIMIT_UPPER32, 0x0);
		}
	}

	if (pci_io)
	{
		/* Round I/O allocator to 4KB boundary */
		pciauto_region_align(pci_io, 0x1000);

		pci_write_config8(dev, PCI_IO_BASE, (u32)(((pci_io->bus_lower & 0x0000f000) >> 8) & PCI_IO_RANGE_MASK) | io_32);
		if (io_32 == PCI_IO_RANGE_TYPE_32)
			pci_write_config16(dev, PCI_IO_BASE_UPPER16, (pci_io->bus_lower & 0xffff0000) >> 16);

		cmdstat |= PCI_COMMAND_IO;
	}
	else
	{
		/* Disable I/O if unsupported */
		pci_write_config8(dev, PCI_IO_BASE, 0xf0 | io_32);
		pci_write_config8(dev, PCI_IO_LIMIT, 0x0 | io_32);
		if (io_32 == PCI_IO_RANGE_TYPE_32)
		{
			pci_write_config16(dev, PCI_IO_BASE_UPPER16, 0x0);
			pci_write_config16(dev, PCI_IO_LIMIT_UPPER16, 0x0);
		}
	}

	/* For PCIe devices see if we need to retrain the link by hand */
	pcie_off = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (pcie_off)
		pciauto_exp_fixup_link(dev, pcie_off);

	/* Enable memory and I/O accesses, enable bus master */
	pci_write_config16(dev, PCI_COMMAND, cmdstat | PCI_COMMAND_MASTER);
}

void pciauto_postscan_setup_bridge(struct pci_bus *bus)
{
	struct pci_controller *ctlr_hose = pci_get_controller(bus);

	struct pci_region *pci_mem = ctlr_hose->pci_mem;
	struct pci_region *pci_prefetch = ctlr_hose->pci_prefetch;
	struct pci_region *pci_io = ctlr_hose->pci_io;

	struct pci_device *dev = bus->pci_bridge;

	/* Configure bus number registers */
	pci_write_config8(dev, PCI_SUBORDINATE_BUS, bus->bus_number_last_sub - ctlr_hose->bus_number_base);
	KprintfH("[pcie] %s: Bus %ld subordinate bus set to %ld\n", __func__, bus->bus_number, bus->bus_number_last_sub);

	if (pci_mem)
	{
		/* Round memory allocator */
		pciauto_region_align(pci_mem, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);
		pci_write_config16(dev, PCI_MEMORY_LIMIT, ((pci_mem->bus_lower - 1) >> 16) & PCI_MEMORY_RANGE_MASK);
		KprintfH("[pcie] %s: Bus %ld memory limit set to 0x%lx%08lx\n", __func__, bus->bus_number, (ULONG)((u64)(pci_mem->bus_lower - 1) >> 32), (ULONG)((pci_mem->bus_lower - 1) & 0xffffffff));
	}

	if (pci_prefetch)
	{
		u16 prefechable_64;

		pci_read_config16(dev, PCI_PREF_MEMORY_LIMIT, &prefechable_64);
		prefechable_64 &= PCI_PREF_RANGE_TYPE_MASK;

		/* Round memory allocator */
		pciauto_region_align(pci_prefetch, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		pci_write_config16(dev, PCI_PREF_MEMORY_LIMIT, (u32)(((pci_prefetch->bus_lower - 1) >> 16) & PCI_PREF_RANGE_MASK) | prefechable_64);
		KprintfH("[pcie] %s: Bus %ld prefetchable memory limit set to 0x%lx%08lx\n", __func__, bus->bus_number, (ULONG)((u64)(pci_prefetch->bus_lower - 1) >> 32), (ULONG)((pci_prefetch->bus_lower - 1) & 0xffffffff));
		if (prefechable_64 == PCI_PREF_RANGE_TYPE_64)
#ifdef CONFIG_SYS_PCI_64BIT
			pci_write_config32(dev, PCI_PREF_LIMIT_UPPER32,
							   (u32)((pci_prefetch->bus_lower - 1) >> 32));
#else
			pci_write_config32(dev, PCI_PREF_LIMIT_UPPER32, 0x0);
#endif
	}

	if (pci_io)
	{
		u8 io_32;

		pci_read_config8(dev, PCI_IO_LIMIT, &io_32);
		io_32 &= PCI_IO_RANGE_TYPE_MASK;

		/* Round I/O allocator to 4KB boundary */
		pciauto_region_align(pci_io, 0x1000);

		pci_write_config8(dev, PCI_IO_LIMIT, ((u32)(((pci_io->bus_lower - 1) & 0x0000f000) >> 8) & PCI_IO_RANGE_MASK) | io_32);
		if (io_32 == PCI_IO_RANGE_TYPE_32)
			pci_write_config16(dev, PCI_IO_LIMIT_UPPER16, ((pci_io->bus_lower - 1) & 0xffff0000) >> 16);
	}
}

/*
 * HJF: Changed this to return int. I think this is required
 * to get the correct result when scanning bridges
 */
s32 pciauto_config_device(struct pci_device *dev)
{
	u32 sub_bus = dev->bus->bus_number;
	struct pci_controller *ctlr = pci_get_controller(dev->bus);

	struct pci_region *pci_mem = ctlr->pci_mem;
	struct pci_region *pci_prefetch = ctlr->pci_prefetch;
	struct pci_region *pci_io = ctlr->pci_io;

	u16 class;
	pci_read_config16(dev, PCI_CLASS_DEVICE, &class);

	switch (class)
	{
	case PCI_CLASS_BRIDGE_PCI:
		Kprintf("[pcie] %s: Found P2P bridge, device %ld\n", __func__, PCI_DEV(pci_get_bdf(dev)));

		pciauto_setup_device(dev, pci_mem, pci_prefetch, pci_io);

		struct pci_bus *bus;
		pci_create_bus(&bus, dev->bus, dev, ctlr);

		s32 err = pci_probe_bus(bus);
		if (err < 0)
		{
			Kprintf("[pcie] %s: Failed to probe bus %ld\n", __func__, sub_bus);
			return err;
		}
		sub_bus = bus->bus_number_last_sub;
		break;

	case PCI_CLASS_BRIDGE_CARDBUS:
		Kprintf("[pcie] %s: Found P2CardBus bridge, device %ld\n", __func__, PCI_DEV(pci_get_bdf(dev)));
		/*
		 * just do a minimal setup of the bridge,
		 * let the OS take care of the rest
		 */
		pciauto_setup_device(dev, pci_mem, pci_prefetch, pci_io);
		break;

#if defined(CONFIG_PCIAUTO_SKIP_HOST_BRIDGE)
	case PCI_CLASS_BRIDGE_OTHER:
		Kprintf("[pcie] %s: Skipping bridge device %ld\n", __func__, PCI_DEV(pci_get_bdf(dev)));
		break;
#endif

	default:
		pciauto_setup_device(dev, pci_mem, pci_prefetch, pci_io);
		break;
	}

	return (s32)sub_bus;
}
