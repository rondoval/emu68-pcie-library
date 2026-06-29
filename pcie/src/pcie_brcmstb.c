// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom STB PCIe controller driver
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd.
 *
 * Based on upstream Linux kernel driver:
 * drivers/pci/controller/pcie-brcmstb.c
 * Copyright (C) 2009 - 2017 Broadcom
 *
 * Based driver by Nicolas Saenz Julienne
 * Copyright (C) 2020 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <pcie_brcmstb.h>
#include <bcm2711.h>
#include <pci.h>
#include <debug.h>
#include <pci_util.h>
#include <devtree.h>
#include <errors.h>
#include <iomem.h>
#include <timing.h>

/* PCIe parameters */
#define BRCM_NUM_PCIE_OUT_WINS 4

/* MDIO registers */
#define MDIO_PORT0 0x0
#define MDIO_DATA_MASK 0x7fffffff
#define MDIO_DATA_SHIFT 0
#define MDIO_PORT_MASK 0xf0000
#define MDIO_PORT_SHIFT 16
#define MDIO_REGAD_MASK 0xffff
#define MDIO_REGAD_SHIFT 0
#define MDIO_CMD_MASK 0xfff00000
#define MDIO_CMD_SHIFT 20
#define MDIO_CMD_READ 0x1
#define MDIO_CMD_WRITE 0x0
#define MDIO_DATA_DONE_MASK 0x80000000
#define SSC_REGS_ADDR 0x1100
#define SET_ADDR_OFFSET 0x1f
#define SSC_CNTL_OFFSET 0x2
#define SSC_CNTL_OVRD_EN_MASK 0x8000
#define SSC_CNTL_OVRD_VAL_MASK 0x4000
#define SSC_STATUS_OFFSET 0x1
#define SSC_STATUS_SSC_MASK 0x400
#define SSC_STATUS_SSC_SHIFT 10
#define SSC_STATUS_PLL_LOCK_MASK 0x800
#define SSC_STATUS_PLL_LOCK_SHIFT 11

/* MSI target addresses */
#define BRCM_MSI_TARGET_ADDR_LT_4GB 0x0fffffffcULL
#define BRCM_MSI_TARGET_ADDR_GT_4GB 0xffffffffcULL

/**
 * brcm_pcie_encode_ibar_size() - Encode the inbound "BAR" region size
 * @size: The inbound region size
 *
 * This function converts size of the inbound "BAR" region to the non-linear
 * values of the PCIE_MISC_RC_BAR[123]_CONFIG_LO register SIZE field.
 *
 * Return: The encoded inbound region size
 */
static u32 brcm_pcie_encode_ibar_size(u64 size)
{
	u32 log2_in = log2_floor_u64(size);

	if (log2_in >= 12 && log2_in <= 15)
		/* Covers 4KB to 32KB (inclusive) */
		return (u32)((log2_in - 12U) + 0x1cU);
	else if (log2_in >= 16 && log2_in <= 37)
		/* Covers 64KB to 32GB, (inclusive) */
		return (u32)(log2_in - 15U);

	/* Something is awry so disable */
	return 0;
}

/**
 * brcm_pcie_rc_mode() - Check if PCIe controller is in RC mode
 * @pcie: Pointer to the PCIe controller state
 *
 * The controller is capable of serving in both RC and EP roles.
 *
 * Return: true for RC mode, false for EP mode.
 */
static BOOL brcm_pcie_rc_mode(struct pci_controller *pcie)
{
	u32 val = mmio_read32(pcie->base + PCIE_MISC_PCIE_STATUS);
	KprintfH("[pcie] %s: PCIe status: 0x%lx\n", __func__, val);

	return (val & STATUS_PCIE_PORT_MASK) >> STATUS_PCIE_PORT_SHIFT;
}

/**
 * brcm_pcie_link_up() - Check whether the PCIe link is up
 * @pcie: Pointer to the PCIe controller state
 *
 * Return: true if the link is up, false otherwise.
 */
static BOOL brcm_pcie_link_up(const struct pci_controller *pcie)
{
	u32 val, dla, plu;

	val = mmio_read32(pcie->base + PCIE_MISC_PCIE_STATUS);
	dla = (val & STATUS_PCIE_DL_ACTIVE_MASK) >> STATUS_PCIE_DL_ACTIVE_SHIFT;
	plu = (val & STATUS_PCIE_PHYLINKUP_MASK) >> STATUS_PCIE_PHYLINKUP_SHIFT;
	KprintfH("[pcie] %s: PCIe link %s\n", __func__, (dla && plu) ? "up" : "down");

	return dla && plu;
}

static s32 brcm_pcie_config_address(const struct pci_controller *pcie, pci_dev_t bdf, u32 offset, void **paddress)
{

	u32 pci_bus = PCI_BUS(bdf), pci_dev = PCI_DEV(bdf), pci_func = PCI_FUNC(bdf), idx;

	KprintfH("[pcie] %s: bus %ld dev %ld func %ld offset 0x%lx\n",
			 __func__, pci_bus, pci_dev, pci_func, offset);
	/*
	 * Busses 0 (host PCIe bridge) and 1 (its immediate child)
	 * are limited to a single device each
	 */
	if (pci_bus < 2 && pci_dev > 0)
		return -EINVAL;

	/* Accesses to the RC go right to the RC registers */
	if (pci_bus == 0)
	{
		*paddress = pcie->base + offset;
		return 0;
	}

	/* An access to our HW w/o link-up will cause a CPU Abort */
	if (!brcm_pcie_link_up(pcie))
		return -EINVAL;

	/* For devices, write to the config space index register */
	idx = PCIE_ECAM_OFFSET(pci_bus, pci_dev, pci_func, 0);

	mmio_write32(idx, pcie->base + PCIE_EXT_CFG_INDEX);
	*paddress = pcie->base + PCIE_EXT_CFG_DATA + offset;

	return 0;
}

s32 brcm_pcie_read_config(const struct pci_controller *ctrl, pci_dev_t bdf, u32 offset, u32 *valuep, enum pci_size_t size)
{
	if (offset >= 4096)
	{
		*valuep = pci_conv_32_to_size(0, offset, size);
		return -EINVAL;
	}

	void *address;

	if (brcm_pcie_config_address(ctrl, bdf, offset, &address) < 0)
	{
		*valuep = pci_get_ff(size);
		return 0;
	}

	switch (size)
	{
	case PCI_SIZE_8:
		*valuep = (u32)mmio_read8(address);
		return 0;
	case PCI_SIZE_16:
		*valuep = (u32)mmio_read16(address);
		return 0;
	case PCI_SIZE_32:
		*valuep = (u32)mmio_read32(address);
		return 0;
	default:
		return -EINVAL;
	}
}

s32 brcm_pcie_write_config(struct pci_controller *ctrl, pci_dev_t bdf, u32 offset, u32 value, enum pci_size_t size)
{
	if (offset >= 4096)
		return -EINVAL;

	void *address;

	if (brcm_pcie_config_address(ctrl, bdf, offset, &address) < 0)
		return 0;

	switch (size)
	{
	case PCI_SIZE_8:
		mmio_write8((u8)value, address);
		return 0;
	case PCI_SIZE_16:
		mmio_write16((u16)value, address);
		return 0;
	case PCI_SIZE_32:
		mmio_write32((u32)value, address);
		return 0;
	default:
		return -EINVAL;
	}
}

#ifdef DEBUG
static const char *link_speed_to_str(u32 cls)
{
	switch (cls)
	{
	case PCI_EXP_LNKSTA_CLS_2_5GB:
		return "2.5";
	case PCI_EXP_LNKSTA_CLS_5_0GB:
		return "5.0";
	case PCI_EXP_LNKSTA_CLS_8_0GB:
		return "8.0";
	default:
		break;
	}

	return "??";
}
#endif /* DEBUG (link_speed_to_str) */

static u32 brcm_pcie_mdio_form_pkt(u32 port, u32 regad, u32 cmd)
{
	u32 pkt;

	pkt = (port << MDIO_PORT_SHIFT) & MDIO_PORT_MASK;
	pkt |= (regad << MDIO_REGAD_SHIFT) & MDIO_REGAD_MASK;
	pkt |= (cmd << MDIO_CMD_SHIFT) & MDIO_CMD_MASK;

	return pkt;
}

static s32 brcm_pcie_wait_mdio_value(void *addr, u32 mask, u32 expected, u32 timeout_us, u32 *value)
{
	u32 current = 0, deadline = get_time() + timeout_us;

	for (;;)
	{
		current = mmio_read32(addr);
		if ((current & mask) == expected)
			break;
		if (timeout_us && time_deadline_passed(get_time(), deadline))
			break;
	}

	*value = current;
	return ((current & mask) == expected) ? 0 : -ETIMEDOUT;
}

/**
 * brcm_pcie_mdio_read() - Perform a register read on the internal MDIO bus
 * @base: Pointer to the PCIe controller IO registers
 * @port: The MDIO port number
 * @regad: The register address
 * @val: A pointer at which to store the read value
 *
 * Return: 0 on success and register value in @val, negative error value
 *         on failure.
 */
static s32 brcm_pcie_mdio_read(void *base, u32 port, u32 regad, u32 *val)
{
	u32 data, addr;

	addr = brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_READ);
	mmio_write32(addr, base + PCIE_RC_DL_MDIO_ADDR);
	mmio_read32(base + PCIE_RC_DL_MDIO_ADDR);

	s32 ret = brcm_pcie_wait_mdio_value(base + PCIE_RC_DL_MDIO_RD_DATA,
										MDIO_DATA_DONE_MASK, MDIO_DATA_DONE_MASK, 100, &data);

	*val = data & MDIO_DATA_MASK;

	return ret;
}

/**
 * brcm_pcie_mdio_write() - Perform a register write on the internal MDIO bus
 * @base: Pointer to the PCIe controller IO registers
 * @port: The MDIO port number
 * @regad: Address of the register
 * @wrdata: The value to write
 *
 * Return: 0 on success, negative error value on failure.
 */
static s32 brcm_pcie_mdio_write(void *base, u32 port, u32 regad, u16 wrdata)
{
	u32 data, addr;

	addr = brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_WRITE);
	mmio_write32(addr, base + PCIE_RC_DL_MDIO_ADDR);
	mmio_read32(base + PCIE_RC_DL_MDIO_ADDR);
	mmio_write32(MDIO_DATA_DONE_MASK | wrdata, base + PCIE_RC_DL_MDIO_WR_DATA);

	return brcm_pcie_wait_mdio_value(base + PCIE_RC_DL_MDIO_WR_DATA,
									 MDIO_DATA_DONE_MASK, 0, 100, &data);
}

/**
 * brcm_pcie_set_ssc() - Configure the controller for Spread Spectrum Clocking
 * @base: pointer to the PCIe controller IO registers
 *
 * Return: 0 on success, negative error value on failure.
 */
static s32 brcm_pcie_set_ssc(void *base)
{
	KprintfH("[pcie] %s\n", __func__);
	u32 pll, ssc;
	s32 ret;
	u32 tmp;

	ret = brcm_pcie_mdio_write(base, MDIO_PORT0, SET_ADDR_OFFSET,
							   SSC_REGS_ADDR);
	if (ret < 0)
		return ret;

	ret = brcm_pcie_mdio_read(base, MDIO_PORT0, SSC_CNTL_OFFSET, &tmp);
	if (ret < 0)
		return ret;

	tmp |= (SSC_CNTL_OVRD_EN_MASK | SSC_CNTL_OVRD_VAL_MASK);

	ret = brcm_pcie_mdio_write(base, MDIO_PORT0, SSC_CNTL_OFFSET, (u16)tmp);
	if (ret < 0)
		return ret;

	delay_ms(1);
	ret = brcm_pcie_mdio_read(base, MDIO_PORT0, SSC_STATUS_OFFSET, &tmp);
	if (ret < 0)
		return ret;

	ssc = (tmp & SSC_STATUS_SSC_MASK) >> SSC_STATUS_SSC_SHIFT;
	pll = (tmp & SSC_STATUS_PLL_LOCK_MASK) >> SSC_STATUS_PLL_LOCK_SHIFT;

	return ssc && pll ? 0 : -EIO;
}

/**
 * brcm_pcie_set_gen() - Limits operation to a specific generation (1, 2 or 3)
 * @pcie: pointer to the PCIe controller state
 * @gen: PCIe generation to limit the controller's operation to
 */
static void brcm_pcie_set_gen(struct pci_controller *pcie, u32 gen)
{
	Kprintf("[pcie] %s: gen %ld\n", __func__, gen);
	void *cap_base = pcie->base + BRCM_PCIE_CAP_REGS;

	u32 lnkctl2 = mmio_read16(cap_base + PCI_EXP_LNKCTL2);
	u32 lnkcap = mmio_read32(cap_base + PCI_EXP_LNKCAP);

	lnkcap = (lnkcap & ~(u32)PCI_EXP_LNKCAP_SLS) | gen;
	mmio_write32(lnkcap, cap_base + PCI_EXP_LNKCAP);

	lnkctl2 = (lnkctl2 & ~0x0fU) | gen;
	mmio_write16((u16)lnkctl2, cap_base + PCI_EXP_LNKCTL2);
}

static void brcm_pcie_set_outbound_win(struct pci_controller *pcie, u32 win, u64 phys_addr, u64 pcie_addr, u64 size)
{
	KprintfH("[pcie] %s: Setting outbound win %ld: phys 0x%lx%08lx <-> pcie 0x%lx, size 0x%lx\n", __func__, win, (ULONG)(phys_addr >> 32), (ULONG)(phys_addr & 0xffffffff), (ULONG)pcie_addr, (ULONG)size);
	void *base = pcie->base;

	/* Set the base of the pcie_addr window */
	mmio_write32(u64_lo32(pcie_addr), base + PCIE_MEM_WIN0_LO(win));
	mmio_write32(u64_hi32(pcie_addr), base + PCIE_MEM_WIN0_HI(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_LO(%ld) = 0x%lx\n", __func__, win, mmio_read32(base + PCIE_MEM_WIN0_LO(win)));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_HI(%ld) = 0x%lx\n", __func__, win, mmio_read32(base + PCIE_MEM_WIN0_HI(win)));

	/* Write the addr base & limit lower bits (in MBs) */
	u32 phys_addr_mb = (u32)(phys_addr / SZ_1M);
	u32 limit_addr_mb = (u32)((phys_addr + size - 1) / SZ_1M);
	KprintfH("[pcie] %s: phys_addr_mb = 0x%lx, limit_addr_mb = 0x%lx\n", __func__, (ULONG)phys_addr_mb, (ULONG)limit_addr_mb);

	u32 tmp = mmio_read32(base + PCIE_MEM_WIN0_BASE_LIMIT(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_BASE_LIMIT(%ld) before = 0x%lx\n", __func__, win, tmp);
	u32_update_mask(&tmp, phys_addr_mb, MEM_WIN0_BASE_LIMIT_BASE_MASK);
	u32_update_mask(&tmp, limit_addr_mb, MEM_WIN0_BASE_LIMIT_LIMIT_MASK);
	mmio_write32(tmp, base + PCIE_MEM_WIN0_BASE_LIMIT(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_BASE_LIMIT(%ld) after = 0x%lx\n", __func__, win, mmio_read32(base + PCIE_MEM_WIN0_BASE_LIMIT(win)));

	/* Write the cpu & limit addr upper bits */
	u32 high_addr_shift = MEM_WIN0_BASE_LIMIT_BASE_HI_SHIFT;
	u32 phys_addr_mb_high = (u32)(phys_addr_mb >> high_addr_shift);
	KprintfH("[pcie] %s: phys_addr_mb_high = 0x%lx\n", __func__, (ULONG)phys_addr_mb_high);
	tmp = mmio_read32(base + PCIE_MEM_WIN0_BASE_HI(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_BASE_HI(%ld) before = 0x%lx\n", __func__, win, tmp);
	u32_update_mask(&tmp, phys_addr_mb_high, MEM_WIN0_BASE_HI_BASE_MASK);
	mmio_write32(tmp, base + PCIE_MEM_WIN0_BASE_HI(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_BASE_HI(%ld) after = 0x%lx\n", __func__, win, mmio_read32(base + PCIE_MEM_WIN0_BASE_HI(win)));

	u32 limit_addr_mb_high = (u32)(limit_addr_mb >> high_addr_shift);
	KprintfH("[pcie] %s: limit_addr_mb_high = 0x%lx\n", __func__, (ULONG)limit_addr_mb_high);
	tmp = mmio_read32(base + PCIE_MEM_WIN0_LIMIT_HI(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_LIMIT_HI(%ld) before = 0x%lx\n", __func__, win, tmp);
	u32_update_mask(&tmp, limit_addr_mb_high, PCIE_MEM_WIN0_LIMIT_HI_LIMIT_MASK);
	mmio_write32(tmp, base + PCIE_MEM_WIN0_LIMIT_HI(win));
	KprintfH("[pcie] %s: PCIE_MEM_WIN0_LIMIT_HI(%ld) after = 0x%lx\n", __func__, win, mmio_read32(base + PCIE_MEM_WIN0_LIMIT_HI(win)));
}

static s32 brcm_devtree_parse(struct pci_controller *ctrl)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");

	ctrl->dt_node_name = DT_GetAlias((CONST_STRPTR) "pcie0");
	if (ctrl->dt_node_name == NULL)
	{
		Kprintf("[pcie] %s: Failed to get aliases from device tree\n", __func__);
		return -1;
	}

	APTR key = DT_OpenKey(ctrl->dt_node_name);
	if (key == NULL)
	{
		Kprintf("[pcie] %s: Failed to open key %s\n", __func__, ctrl->dt_node_name);
		return -1;
	}

	ctrl->compatible = DT_GetPropValue(DT_FindProperty(key, (CONST_STRPTR) "compatible"));

	ctrl->base = DT_GetBaseAddressVirtual(ctrl->dt_node_name);
	if (ctrl->base == NULL)
	{
		Kprintf("[pcie] %s: Failed to get PCIe base address\n", __func__);
		DT_CloseKey(key);
		return -1;
	}

	KprintfH("[pcie] %s: compatible: %s\n", __func__, ctrl->compatible);
	KprintfH("[pcie] %s: register base: 0x%08lx\n", __func__, ctrl->base);

	// they're not in our dev tree...
	if (DT_FindProperty(key, (CONST_STRPTR) "brcm,enable-ssc"))
	{
		ctrl->ssc = TRUE;
		KprintfH("[pcie] %s: Found brcm,enable-ssc property\n", __func__);
	}
	ctrl->gen = 0;

	APTR mmio_window_phys_prop = DT_FindProperty(key, (CONST_STRPTR) "emu68,pci-mmio-phys");
	if (!mmio_window_phys_prop)
	{
		Kprintf("[pcie] %s: Device '%s': No property emu68,pci-mmio-phys\n", __func__, ctrl->dt_node_name);
		DT_CloseKey(key);
		return -EINVAL;
	}
	u32 mmio_window_phys_cells = DT_GetPropLen(mmio_window_phys_prop) / sizeof(u32);
	ctrl->mmio_window_phys = DT_GetNumber(DT_GetPropValue(mmio_window_phys_prop), mmio_window_phys_cells);
	ctrl->mmio_window_virtual = (u8 *)DT_GetPropertyValueULONG(key, "emu68,pci-mmio-virt", 0, FALSE);
	ctrl->mmio_window_size = DT_GetPropertyValueULONG(key, "emu68,pci-mmio-size", SZ_64M, FALSE);
	KprintfH("[pcie] %s: emu68,pci-mmio-phys = 0x%lx%08lx\n", __func__, (ULONG)(ctrl->mmio_window_phys >> 32), (ULONG)(ctrl->mmio_window_phys & 0xffffffff));
	KprintfH("[pcie] %s: emu68,pci-mmio-virt = 0x%lx\n", __func__, (ULONG)ctrl->mmio_window_virtual);
	KprintfH("[pcie] %s: emu68,pci-mmio-size = 0x%lx\n", __func__, (ULONG)(ctrl->mmio_window_size));

	APTR int_map_prop = DT_FindProperty(key, (CONST_STRPTR) "interrupt-map");
	if (int_map_prop)
	{
		// TODO cleanup
		APTR root = DT_OpenKey((CONST_STRPTR) "/");
		// ULONG interrupt_parent_phandle = DT_GetPropertyValueULONG(root, "interrupt-parent", 0, TRUE);
		DT_CloseKey(root);

		const u32 interrupt_cells = DT_GetPropertyValueULONG(key, "#interrupt-cells", 1, FALSE);
		const u32 addr_cells = DT_GetPropertyValueULONG(key, "#address-cells", 2, FALSE);

		const u32 *int_map = (const u32 *)DT_GetPropValue(int_map_prop);
		u32 len = DT_GetPropLen(int_map_prop);

		// child_addr + child_interrupt + phandle + int_type + parent_interrupt + interrupt flags
		u32 entry_size = addr_cells + 1 + 1 + 1 + interrupt_cells + 1;
		u32 entries = len / (sizeof(u32) * entry_size);

		KprintfH("[pcie] %s: Found interrupt-map with %ld entries\n", __func__, entries);

		for (u32 i = 0; i < entries; i++)
		{
			// child addr cells + child interrupt cells + parent phandle + parent interrupt cells + interrupt flags
			u32 child_interrupt = (u32)DT_GetNumber(int_map + i * entry_size + addr_cells, 1);
			u32 parent_interrupt = (u32)DT_GetNumber(int_map + i * entry_size + addr_cells + 3, interrupt_cells);
			// TODO check phandle matches GIC-400 phandle
#ifdef DEBUG_HIGH
			u32 flags = (u32)DT_GetNumber(int_map + i * entry_size + addr_cells + 3 + interrupt_cells, 1);
			KprintfH("[pcie] %s: interrupt-map entry %ld: child=%ld parent=%ld flags=0x%lx\n", __func__, i, child_interrupt, parent_interrupt, flags);
#endif

			if (child_interrupt >= 1 && child_interrupt <= 4)
			{
				ctrl->INT_x_mapping[child_interrupt - 1] = (s32)parent_interrupt;
			}
		}
	}

	ctrl->msi.gic_irq = DT_GetInterrupt(key, 1); // first interrupt is for the host controller; second interrupt is MSI
	KprintfH("[pcie] %s: MSI IRQ = %ld\n", __func__, ctrl->msi.gic_irq);

	// We're done with the device tree
	DT_CloseKey(key);
	return 0;
}

static s32 pci_get_devtree_dma_regions(struct pci_controller *ctlr, struct pci_region *memp, u32 index)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	u32 cells_per_record, i = 0;

	APTR key = DT_OpenKey(ctlr->dt_node_name);
	APTR prop = DT_FindProperty(key, (CONST_STRPTR) "dma-ranges");
	if (!prop)
	{
		Kprintf("[pcie] %s: Device '%s': Cannot decode dma-ranges\n", __func__, ctlr->dt_node_name);
		DT_CloseKey(key);
		return -EINVAL;
	}

	const u32 *dma_ranges = (const u32 *)DT_GetPropValue(prop);
	u32 len = DT_GetPropLen(prop);

	u32 pci_addr_cells = DT_GetPropertyValueULONG(key, "#address-cells", 2, FALSE);
	u32 addr_cells = DT_GetPropertyValueULONG(DT_GetParent(key), "#address-cells", 2, FALSE);
	u32 size_cells = DT_GetPropertyValueULONG(key, "#size-cells", 1, FALSE);

	/* PCI addresses are always 3-cells */
	len /= sizeof(u32);
	cells_per_record = pci_addr_cells + addr_cells + size_cells;
	KprintfH("[pcie] %s: len=%ld, cells_per_record=%ld\n", __func__, len, cells_per_record);

	while (len >= cells_per_record)
	{
		if (i == index)
		{
			memp->bus_start = (pci_addr_t)DT_GetNumber(dma_ranges + 1, 2);
			memp->phys_start = DT_GetNumber(dma_ranges + pci_addr_cells, addr_cells);
			memp->size = (pci_size_t)DT_GetNumber(dma_ranges + pci_addr_cells + addr_cells, size_cells);
			KprintfH("[pcie] %s: dma-range %ld, bus_start=0x%lx%08lx, phys_start=0x%lx%08lx, size=0x%lx%08lx\n",
					 __func__, i, (ULONG)((u64)(memp->bus_start) >> 32), (ULONG)(memp->bus_start & 0xffffffff), (ULONG)(memp->phys_start >> 32), (ULONG)(memp->phys_start & 0xffffffff), (ULONG)((u64)(memp->size) >> 32), (ULONG)(memp->size & 0xffffffff));
			return 0;
		}
		dma_ranges += cells_per_record;
		len -= cells_per_record;
		i++;
	}

	return -EINVAL;
}

static s32 pci_get_devtree_regions(struct pci_controller *hose)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");

	APTR key = DT_OpenKey(hose->dt_node_name);
	APTR prop = DT_FindProperty(key, (CONST_STRPTR) "ranges");
	if (!prop)
	{
		Kprintf("[pcie] %s: Cannot find 'ranges' property in device tree\n", __func__);
		DT_CloseKey(key);
		return -EINVAL;
	}

	u32 *ranges = (u32 *)DT_GetPropValue(prop);
	u32 len = DT_GetPropLen(prop);

	u32 pci_addr_cells = DT_GetPropertyValueULONG(key, "#address-cells", 2, FALSE);
	u32 addr_cells = DT_GetPropertyValueULONG(DT_GetParent(key), "#address-cells", 2, FALSE);
	u32 size_cells = DT_GetPropertyValueULONG(key, "#size-cells", 1, FALSE);

	/* PCI addresses are always 3-cells */
	len /= sizeof(u32);
	const u32 cells_per_record = pci_addr_cells + addr_cells + size_cells;
	hose->region_count = 0;
	KprintfH("[pcie] %s: len=%ld, cells_per_record=%ld\n", __func__, len, cells_per_record);

	/* Dynamically allocate the regions array */
	u32 max_regions = len / cells_per_record + CONFIG_NR_DRAM_BANKS;
	hose->regions = (struct pci_region *)AllocVec(max_regions * sizeof(struct pci_region), MEMF_CLEAR);
	if (!hose->regions)
		return -ENOMEM;

	for (u32 i = 0; i < max_regions; i++, len -= cells_per_record)
	{
		u64 pci_addr, addr, size;
		u32 space_code;
		u32 flags;
		u32 type;
		u32 pos;

		if (len < cells_per_record)
			break;
		flags = ranges[0];
		space_code = (flags >> 24) & 3;
		pci_addr = DT_GetNumber(ranges + 1, 2);
		ranges += pci_addr_cells;
		addr = DT_GetNumber(ranges, addr_cells);
		ranges += addr_cells;
		size = DT_GetNumber(ranges, size_cells);
		ranges += size_cells;
		KprintfH("[pcie] %s: region %ld, pci_addr=0x%lx%08lx, addr=0x%lx%08lx, size=0x%lx%08lx, space_code=%ld\n",
				 __func__, hose->region_count, (ULONG)(pci_addr >> 32), (ULONG)(pci_addr & 0xffffffff), (ULONG)(addr >> 32), (ULONG)(addr & 0xffffffff), (ULONG)(size >> 32), (ULONG)(size & 0xffffffff), space_code);
		if (space_code & 2)
		{
			type = flags & (1U << 30) ? PCI_REGION_PREFETCH : PCI_REGION_MEM;
		}
		else if (space_code & 1)
		{
			type = PCI_REGION_IO;
		}
		else
		{
			continue;
		}

#ifndef CONFIG_SYS_PCI_64BIT
		if (type == PCI_REGION_MEM && u64_hi32(pci_addr))
		{
			Kprintf("[pcie] %s: - pci_addr beyond the 32-bit boundary, ignoring\n", __func__);
			continue;
		}
#endif

#ifndef CONFIG_PHYS_64BIT
		if (u64_hi32(addr))
		{
			Kprintf("[pcie] %s: - addr beyond the 32-bit boundary, ignoring\n", __func__);
			continue;
		}
#endif

		if (~((pci_addr_t)0) - pci_addr < size)
		{
			Kprintf("[pcie] %s: - PCI range exceeds max address, ignoring\n", __func__);
			continue;
		}

		if (~((phys_addr_t)0) - addr < size)
		{
			Kprintf("[pcie] %s: - phys range exceeds max address, ignoring\n", __func__);
			continue;
		}

		pos = hose->region_count++;
		KprintfH("[pcie] %s: - type=%ld, pos=%ld\n", __func__, type, pos);
		pci_set_region(hose->regions + pos, (pci_addr_t)pci_addr, (phys_addr_t)addr, (pci_size_t)size, type);
	}

	/* Add a region for our local memory — but only the Emu68 (Pi-DRAM) RAM that the
	 * PCIe inbound window (dma-ranges) actually decodes.  Registering Zorro/accelerator
	 * Fast RAM here would let pci_phys_to_bus translate addresses the engine cannot
	 * reach.  If dma-ranges is unavailable, fall back to registering all Fast RAM. */
	KprintfH("[pcie] %s: Adding system memory regions\n", __func__);
	struct pci_region dma_win;
	BOOL have_dma_win = (pci_get_devtree_dma_regions(hose, &dma_win, 0) == 0);
	/* The inbound window translates PCI bus <-> CPU phys by this offset (the value
	 * programmed into RC_BAR2, below).  The SYS_MEMORY region must encode the same
	 * offset so pci_phys_to_bus() yields a bus address the window maps back to the
	 * buffer; normally offset is 0. */
	pci_addr_t dma_offset = have_dma_win
								? (pci_addr_t)(dma_win.bus_start - dma_win.phys_start)
								: 0;

	struct ExecBase *sysBase = EXEC_BASE_NAME;
	Forbid();
	struct MemHeader *mh = (struct MemHeader *)sysBase->MemList.lh_Head;
	for (u32 added = 0;
		 mh->mh_Node.ln_Succ != NULL && added < CONFIG_NR_DRAM_BANKS;
		 mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
	{
		if ((mh->mh_Attributes & MEMF_FAST) == 0)
			continue;

		u32 start = (u32)mh->mh_Lower;
		u32 end = (u32)mh->mh_Upper; /* exclusive */
		if (end <= start)
			continue;
		u32 size = end - start;

		if (have_dma_win &&
			((phys_addr_t)start < dma_win.phys_start ||
			 (phys_addr_t)end > dma_win.phys_start + dma_win.size))
		{
			KprintfH("[pcie] %s: - skipping non-DMA Fast RAM 0x%lx..0x%lx\n", __func__, start, end);
			continue;
		}

		u32 pos = hose->region_count++;
		KprintfH("[pcie] %s: - DRAM region %ld: start=0x%lx, size=0x%lx\n", __func__, pos, start, size);
#ifdef CONFIG_PCI_MAP_SYSTEM_MEMORY
		start = virt_to_phys((void *)(uintptr_t)bd->bi_dram[added].start);
#endif
		pci_set_region(hose->regions + pos, (pci_addr_t)start + dma_offset, start, size,
					   PCI_REGION_MEM | PCI_REGION_SYS_MEMORY);
		added++;
	}
	Permit();

	return 0;
}

s32 brcm_pcie_probe(struct pci_controller *ctlr, u32 bus_number_base)
{
	s32 res = brcm_devtree_parse(ctlr);
	if (res < 0)
	{
		return res;
	}

	void *base = ctlr->base;
	ctlr->bus_number_base = bus_number_base;
	ctlr->bus_number_last = bus_number_base;

	/*
	 * Reset the bridge, assert the fundamental reset. Note for some SoCs,
	 * e.g. BCM7278, the fundamental reset should not be asserted here.
	 * This will need to be changed when support for other SoCs is added.
	 */
	mmio_set32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT_MASK | PCIE_RGR1_SW_INIT_1_PERST_MASK);
	/* Small safety delay so the reset doesn't look like a glitch */
	delay_us(100);

	/* Take the bridge logic (but not PERST#) out of reset */
	mmio_clear32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT_MASK);

	mmio_clear32(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG, PCIE_HARD_DEBUG_SERDES_IDDQ_MASK);

	/* Wait for SerDes to be stable */
	delay_us(100);

	/* Set SCB_MAX_BURST_SIZE, CFG_READ_UR_MODE, SCB_ACCESS_EN */
	mmio_update32(base + PCIE_MISC_MISC_CTRL,
				  MISC_CTRL_MAX_BURST_SIZE_MASK,
				  MISC_CTRL_SCB_ACCESS_EN_MASK |
					  MISC_CTRL_CFG_READ_UR_MODE_MASK |
					  MISC_CTRL_MAX_BURST_SIZE_128 |
					  MISC_CTRL_PCIE_RCB_MPS_MODE_MASK |
					  MISC_CTRL_PCIE_RCB_64B_MODE_MASK);

	s32 ret = pci_get_devtree_regions(ctlr);
	if (ret)
	{
		Kprintf("[pcie] %s: failed to get ranges\n", __func__);
		return ret;
	}

	struct pci_region region;
	/* This takes only first region */
	ret = pci_get_devtree_dma_regions(ctlr, &region, 0);
	if (ret)
	{
		Kprintf("[pcie] %s: failed to get dma-ranges\n", __func__);
		return ret;
	}

	u64 rc_bar2_offset = region.bus_start - region.phys_start;
	u64 rc_bar2_size = round_up_pow2_u64(region.size);
	KprintfH("[pcie] %s: DMA region: bus_start=0x%lx%08lx, phys_start=0x%lx%08lx, size=0x%lx%08lx\n", __func__, (ULONG)((u64)(region.bus_start) >> 32), (ULONG)(region.bus_start & 0xffffffff), (ULONG)(region.phys_start >> 32), (ULONG)(region.phys_start & 0xffffffff), (ULONG)((u64)(region.size) >> 32), (ULONG)(region.size & 0xffffffff));
	KprintfH("[pcie] %s: RC BAR2: offset=0x%lx%08lx, size=0x%lx%08lx\n", __func__, (ULONG)(rc_bar2_offset >> 32), (ULONG)(rc_bar2_offset & 0xffffffff), (ULONG)(rc_bar2_size >> 32), (ULONG)(rc_bar2_size & 0xffffffff));
	KprintfH("[pcie] %s: RC BAR2 size encoded = 0x%lx\n", __func__, (ULONG)brcm_pcie_encode_ibar_size(rc_bar2_size));

	u32 tmp = u64_lo32(rc_bar2_offset);
	u32_update_mask(&tmp, (u32)brcm_pcie_encode_ibar_size(rc_bar2_size),
					RC_BAR2_CONFIG_LO_SIZE_MASK);
	mmio_write32(tmp, base + PCIE_MISC_RC_BAR2_CONFIG_LO);
	mmio_write32(u64_hi32(rc_bar2_offset),
				 base + PCIE_MISC_RC_BAR2_CONFIG_HI);

#ifdef CONFIG_SYS_PCI_64BIT
	if (rc_bar2_offset >= SZ_4G || rc_bar2_size + rc_bar2_offset < SZ_4G)
	{
		ctlr->msi.target_addr = BRCM_MSI_TARGET_ADDR_LT_4GB;
	}
	else
	{
		ctlr->msi.target_addr = BRCM_MSI_TARGET_ADDR_GT_4GB;
	}
#else
	ctlr->msi.target_addr = BRCM_MSI_TARGET_ADDR_LT_4GB;
#endif
	KprintfH("[pcie] %s: MSI target address set to 0x%lx\n", __func__, ctlr->msi.target_addr);

	u32 scb_size_val = rc_bar2_size ? (u32)(log2_floor_u64(rc_bar2_size) - 15) : 0xf; /* 0xf is 1GB */

	tmp = mmio_read32(base + PCIE_MISC_MISC_CTRL);
	u32_update_mask(&tmp, scb_size_val,
					MISC_CTRL_SCB0_SIZE_MASK);
	mmio_write32(tmp, base + PCIE_MISC_MISC_CTRL);
	KprintfH("[pcie] %s: RC BAR2 size=0x%lx, offset=0x%lx%08lx\n", __func__, (ULONG)rc_bar2_size, (ULONG)(rc_bar2_offset >> 32), (ULONG)(rc_bar2_offset & 0xffffffff));
	KprintfH("[pcie] %s: SCB0_SIZE=%ld (0x%lx)\n", __func__, scb_size_val, mmio_read32(base + PCIE_MISC_MISC_CTRL) & MISC_CTRL_SCB0_SIZE_MASK);

	/* Disable the PCIe->GISB memory window (RC_BAR1) */
	mmio_clear32(base + PCIE_MISC_RC_BAR1_CONFIG_LO,
				 RC_BAR1_CONFIG_LO_SIZE_MASK);

	/* Disable the PCIe->SCB memory window (RC_BAR3) */
	mmio_clear32(base + PCIE_MISC_RC_BAR3_CONFIG_LO,
				 RC_BAR3_CONFIG_LO_SIZE_MASK);

	/* Mask all interrupts since we are not handling any yet */
	mmio_write32(0xffffffff, base + PCIE_MSI_INTR2_MASK_SET);

	/* Clear any interrupts we find on boot */
	mmio_write32(0xffffffff, base + PCIE_MSI_INTR2_CLR);

	if (ctlr->gen)
		brcm_pcie_set_gen(ctlr, ctlr->gen);

	/* Unassert the fundamental reset */
	mmio_clear32(ctlr->base + PCIE_RGR1_SW_INIT_1,
				 PCIE_RGR1_SW_INIT_1_PERST_MASK);
	/* 100ms after PERST# deassertion per PCIe CEM */
	delay_ms(100);

	/* Give the RC/EP time to wake up, before trying to configure RC.
	 * Intermittently check status for link-up, up to a total of 100ms.
	 */
	for (u32 i = 0; i < 100 && !brcm_pcie_link_up(ctlr); i += 5)
		delay_ms(5);

	if (!brcm_pcie_link_up(ctlr))
	{
		Kprintf("[pcie] %s: link down\n", __func__);
		return -EINVAL;
	}

	if (!brcm_pcie_rc_mode(ctlr))
	{
		Kprintf("[pcie] %s: PCIe misconfigured; is in EP mode\n", __func__);
		return -EINVAL;
	}

	u32 num_out_wins = 0;
	for (u32 i = 0; i < ctlr->region_count; i++)
	{
		struct pci_region *reg = &ctlr->regions[i];

		if (reg->flags != PCI_REGION_MEM)
			continue;

		if (num_out_wins >= BRCM_NUM_PCIE_OUT_WINS)
			return -EINVAL;

		brcm_pcie_set_outbound_win(ctlr, num_out_wins, reg->phys_start, reg->bus_start, reg->size);

		num_out_wins++;
	}

	/*
	 * For config space accesses on the RC, show the right class for
	 * a PCIe-PCIe bridge (the default setting is to be EP mode).
	 */
	mmio_update32(base + PCIE_RC_CFG_PRIV1_ID_VAL3,
				  PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK, 0x060400);

#ifdef DEBUG
	BOOL ssc_good = FALSE;
#endif
	if (ctlr->ssc)
	{
		ret = brcm_pcie_set_ssc(ctlr->base);
		if (ret)
			Kprintf("[pcie] %s: failed attempt to enter SSC mode\n", __func__);
#ifdef DEBUG
		else
			ssc_good = TRUE;
#endif
	}

#ifdef DEBUG
	u16 lnksta = mmio_read16(base + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKSTA);
	u16 cls = lnksta & PCI_EXP_LNKSTA_CLS, nlw = (lnksta & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;

	Kprintf("[pcie] %s: link up, %s Gbps x%lu %s\n", __func__, link_speed_to_str(cls), nlw, ssc_good ? "(SSC)" : "(!SSC)");
#endif

	/* PCIe->SCB endian mode for BAR */
	mmio_update32(base + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1,
				  PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK,
				  VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN);

	/*
	 * RootCtl bits are reset by perst_n, which undoes pci_enable_crs()
	 * called prior to pci_add_new_bus() during probe. Re-enable here.
	 */
	u16 capreg = mmio_read16(base + BRCM_PCIE_CAP_REGS + PCI_EXP_RTCAP);
	if (capreg & PCI_EXP_RTCAP_CRSVIS)
	{
		KprintfH("[pcie] %s: enabling CRS\n", __func__);
		mmio_set16(base + BRCM_PCIE_CAP_REGS + PCI_EXP_RTCTL,
				   PCI_EXP_RTCTL_CRSSVE);
	}

	/*
	 * We used to enable the CLKREQ# input here, but a few PCIe cards don't
	 * attach anything to the CLKREQ# line, so we shouldn't assume that
	 * it's connected and working. The controller does allow detecting
	 * whether the port on the other side of our link is/was driving this
	 * signal, so we could check before we assume. But because this signal
	 * is for power management, which doesn't make sense in a bootloader,
	 * let's instead just unadvertise ASPM support.
	 */
	mmio_clear32(base + PCIE_RC_CFG_PRIV1_LINK_CAPABILITY,
				 LINK_CAPABILITY_ASPM_SUPPORT_MASK);

	ctlr->hw_rev = mmio_read32(base + PCIE_MISC_REVISION);
	Kprintf("[pcie] %s: controller hw_rev=0x%lx\n", __func__, ctlr->hw_rev);
	if (ctlr->hw_rev < BRCM_PCIE_HW_REV_33)
	{
		Kprintf("[pcie] %s: controller is pre-3.3 revision, unsupported\n", __func__);
		return -ENODEV;
	}

	/* Open gic400 (used by the MSI demux ISR and per-device INTx) before any
	 * interrupt registration; the controller owns this handle until remove. */
	if (brcm_pcie_open_gic400(ctlr) < 0)
		return -ENODEV;

	// Configure MSI
	ret = brcm_pcie_enable_msi(ctlr);
	if (ret)
	{
		Kprintf("[pcie] %s: failed to enable MSI\n", __func__);
		brcm_pcie_disable_msi(ctlr);
		brcm_pcie_close_gic400(ctlr);
		return ret;
	}

	return 0;
}

s32 brcm_pcie_remove(struct pci_controller *pcie)
{
	brcm_pcie_disable_msi(pcie);
	brcm_pcie_close_gic400(pcie);

	void *base = pcie->base;

	/* Assert fundamental reset */
	mmio_set32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_PERST_MASK);

	/* Turn off SerDes */
	mmio_set32(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG,
			   PCIE_HARD_DEBUG_SERDES_IDDQ_MASK);

	/* Shutdown bridge */
	mmio_set32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT_MASK);

	if (pcie->regions)
	{
		FreeVec(pcie->regions);
	}
	return 0;
}
