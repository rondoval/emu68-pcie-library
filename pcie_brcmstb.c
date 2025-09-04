// SPDX-License-Identifier: GPL-2.0
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

#include <pcie_brcmstb.h>
#include <bcm2711.h>
#include <pci.h>
#include <debug.h>
#include <devtree.h>

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

/**
 * brcm_pcie_encode_ibar_size() - Encode the inbound "BAR" region size
 * @size: The inbound region size
 *
 * This function converts size of the inbound "BAR" region to the non-linear
 * values of the PCIE_MISC_RC_BAR[123]_CONFIG_LO register SIZE field.
 *
 * Return: The encoded inbound region size
 */
static int brcm_pcie_encode_ibar_size(u64 size)
{
	int log2_in = ilog2(size);

	if (log2_in >= 12 && log2_in <= 15)
		/* Covers 4KB to 32KB (inclusive) */
		return (log2_in - 12) + 0x1c;
	else if (log2_in >= 16 && log2_in <= 37)
		/* Covers 64KB to 32GB, (inclusive) */
		return log2_in - 15;

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
	ULONG val = readl(pcie->base + PCIE_MISC_PCIE_STATUS);
	Kprintf("[pcie]PCIe status: 0x%lx\n", val);

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
	ULONG val, dla, plu;

	val = readl(pcie->base + PCIE_MISC_PCIE_STATUS);
	dla = (val & STATUS_PCIE_DL_ACTIVE_MASK) >> STATUS_PCIE_DL_ACTIVE_SHIFT;
	plu = (val & STATUS_PCIE_PHYLINKUP_MASK) >> STATUS_PCIE_PHYLINKUP_SHIFT;
	Kprintf("[pcie] PCIe link %s\n", (dla && plu) ? "up" : "down");

	return dla && plu;
}

static int brcm_pcie_config_address(const struct pci_controller *pcie, pci_dev_t bdf,
									UWORD offset, void **paddress)
{

	unsigned int pci_bus = PCI_BUS(bdf);
	unsigned int pci_dev = PCI_DEV(bdf);
	unsigned int pci_func = PCI_FUNC(bdf);
	int idx;

	Kprintf("[pcie] %s: bus %ld dev %ld func %ld offset 0x%lx\n",
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

	writel(idx, pcie->base + PCIE_EXT_CFG_INDEX);
	*paddress = pcie->base + PCIE_EXT_CFG_DATA + offset;

	return 0;
}

int brcm_pcie_read_config(const struct pci_controller *ctrl, pci_dev_t bdf,
						  UWORD offset, ULONG *valuep,
						  enum pci_size_t size)
{
	if (offset < 0 || offset >= 4096)
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
		*valuep = readb(address);
		return 0;
	case PCI_SIZE_16:
		*valuep = readw(address);
		return 0;
	case PCI_SIZE_32:
		*valuep = readl(address);
		return 0;
	default:
		return -EINVAL;
	}
}

int brcm_pcie_write_config(struct pci_controller *ctrl, pci_dev_t bdf,
						   UWORD offset, ULONG value,
						   enum pci_size_t size)
{
	if (offset < 0 || offset >= 4096)
		return -EINVAL;

	void *address;

	if (brcm_pcie_config_address(ctrl, bdf, offset, &address) < 0)
		return 0;

	switch (size)
	{
	case PCI_SIZE_8:
		writeb(value, address);
		return 0;
	case PCI_SIZE_16:
		writew(value, address);
		return 0;
	case PCI_SIZE_32:
		writel(value, address);
		return 0;
	default:
		return -EINVAL;
	}
}

static const char *link_speed_to_str(unsigned int cls)
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

static ULONG brcm_pcie_mdio_form_pkt(unsigned int port, unsigned int regad,
									 unsigned int cmd)
{
	ULONG pkt;

	pkt = (port << MDIO_PORT_SHIFT) & MDIO_PORT_MASK;
	pkt |= (regad << MDIO_REGAD_SHIFT) & MDIO_REGAD_MASK;
	pkt |= (cmd << MDIO_CMD_SHIFT) & MDIO_CMD_MASK;

	return pkt;
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
static int brcm_pcie_mdio_read(void *base, unsigned int port,
							   unsigned int regad, ULONG *val)
{
	ULONG data, addr;
	int ret;

	addr = brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_READ);
	writel(addr, base + PCIE_RC_DL_MDIO_ADDR);
	readl(base + PCIE_RC_DL_MDIO_ADDR);

	ret = readl_poll_timeout(base + PCIE_RC_DL_MDIO_RD_DATA, data,
							 (data & MDIO_DATA_DONE_MASK), 100);

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
static int brcm_pcie_mdio_write(void *base, unsigned int port,
								unsigned int regad, UWORD wrdata)
{
	ULONG data, addr;

	addr = brcm_pcie_mdio_form_pkt(port, regad, MDIO_CMD_WRITE);
	writel(addr, base + PCIE_RC_DL_MDIO_ADDR);
	readl(base + PCIE_RC_DL_MDIO_ADDR);
	writel(MDIO_DATA_DONE_MASK | wrdata, base + PCIE_RC_DL_MDIO_WR_DATA);

	return readl_poll_timeout(base + PCIE_RC_DL_MDIO_WR_DATA, data,
							  !(data & MDIO_DATA_DONE_MASK), 100);
}

/**
 * brcm_pcie_set_ssc() - Configure the controller for Spread Spectrum Clocking
 * @base: pointer to the PCIe controller IO registers
 *
 * Return: 0 on success, negative error value on failure.
 */
static int brcm_pcie_set_ssc(void *base)
{
	Kprintf("[pcie] %s\n", __func__);
	int pll, ssc;
	int ret;
	ULONG tmp;

	ret = brcm_pcie_mdio_write(base, MDIO_PORT0, SET_ADDR_OFFSET,
							   SSC_REGS_ADDR);
	if (ret < 0)
		return ret;

	ret = brcm_pcie_mdio_read(base, MDIO_PORT0, SSC_CNTL_OFFSET, &tmp);
	if (ret < 0)
		return ret;

	tmp |= (SSC_CNTL_OVRD_EN_MASK | SSC_CNTL_OVRD_VAL_MASK);

	ret = brcm_pcie_mdio_write(base, MDIO_PORT0, SSC_CNTL_OFFSET, tmp);
	if (ret < 0)
		return ret;

	delay_us(1000);
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
static void brcm_pcie_set_gen(struct pci_controller *pcie, unsigned int gen)
{
	Kprintf("[pcie] %s: gen %d\n", __func__, gen);
	void *cap_base = pcie->base + BRCM_PCIE_CAP_REGS;

	UWORD lnkctl2 = readw(cap_base + PCI_EXP_LNKCTL2);
	ULONG lnkcap = readl(cap_base + PCI_EXP_LNKCAP);

	lnkcap = (lnkcap & ~PCI_EXP_LNKCAP_SLS) | gen;
	writel(lnkcap, cap_base + PCI_EXP_LNKCAP);

	lnkctl2 = (lnkctl2 & ~0xf) | gen;
	writew(lnkctl2, cap_base + PCI_EXP_LNKCTL2);
}

static void brcm_pcie_set_outbound_win(struct pci_controller *pcie,
									   unsigned int win, u64 phys_addr,
									   u64 pcie_addr, u64 size)
{
	Kprintf("Setting outbound win %ld: phys 0x%lx <-> pcie 0x%lx, size 0x%lx\n",
		   win, phys_addr, pcie_addr, size);
	void *base = pcie->base;
	ULONG phys_addr_mb_high, limit_addr_mb_high;
	phys_addr_t phys_addr_mb, limit_addr_mb;
	int high_addr_shift;
	ULONG tmp;

	/* Set the base of the pcie_addr window */
	writel(lower_32_bits(pcie_addr), base + PCIE_MEM_WIN0_LO(win));
	writel(upper_32_bits(pcie_addr), base + PCIE_MEM_WIN0_HI(win));

	/* Write the addr base & limit lower bits (in MBs) */
	phys_addr_mb = phys_addr / SZ_1M;
	limit_addr_mb = (phys_addr + size - 1) / SZ_1M;

	tmp = readl(base + PCIE_MEM_WIN0_BASE_LIMIT(win));
	u32p_replace_bits(&tmp, phys_addr_mb,
					  MEM_WIN0_BASE_LIMIT_BASE_MASK);
	u32p_replace_bits(&tmp, limit_addr_mb,
					  MEM_WIN0_BASE_LIMIT_LIMIT_MASK);
	writel(tmp, base + PCIE_MEM_WIN0_BASE_LIMIT(win));

	/* Write the cpu & limit addr upper bits */
	high_addr_shift = MEM_WIN0_BASE_LIMIT_BASE_HI_SHIFT;
	phys_addr_mb_high = phys_addr_mb >> high_addr_shift;
	tmp = readl(base + PCIE_MEM_WIN0_BASE_HI(win));
	u32p_replace_bits(&tmp, phys_addr_mb_high,
					  MEM_WIN0_BASE_HI_BASE_MASK);
	writel(tmp, base + PCIE_MEM_WIN0_BASE_HI(win));

	limit_addr_mb_high = limit_addr_mb >> high_addr_shift;
	tmp = readl(base + PCIE_MEM_WIN0_LIMIT_HI(win));
	u32p_replace_bits(&tmp, limit_addr_mb_high,
					  PCIE_MEM_WIN0_LIMIT_HI_LIMIT_MASK);
	writel(tmp, base + PCIE_MEM_WIN0_LIMIT_HI(win));
}

int brcm_pcie_probe(struct pci_controller *ctlr)
{
	void *base = ctlr->base;
	struct pci_region region;
	BOOL ssc_good = FALSE;
	int num_out_wins = 0;
	u64 rc_bar2_offset, rc_bar2_size;
	unsigned int scb_size_val;
	int i, ret;
	UWORD nlw, cls, lnksta;
	ULONG tmp;

	/*
	 * Reset the bridge, assert the fundamental reset. Note for some SoCs,
	 * e.g. BCM7278, the fundamental reset should not be asserted here.
	 * This will need to be changed when support for other SoCs is added.
	 */
	setbits_le32(base + PCIE_RGR1_SW_INIT_1,
				 PCIE_RGR1_SW_INIT_1_INIT_MASK | PCIE_RGR1_SW_INIT_1_PERST_MASK);
	/* Small safety delay so the reset doesn't look like a glitch */
	delay_us(100);

	/* Take the bridge logic (but not PERST#) out of reset */
	clrbits_le32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT_MASK);

	clrbits_le32(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG,
				 PCIE_HARD_DEBUG_SERDES_IDDQ_MASK);

	/* Wait for SerDes to be stable */
	delay_us(100);

	/* Set SCB_MAX_BURST_SIZE, CFG_READ_UR_MODE, SCB_ACCESS_EN */
	clrsetbits_le32(base + PCIE_MISC_MISC_CTRL,
					MISC_CTRL_MAX_BURST_SIZE_MASK,
					MISC_CTRL_SCB_ACCESS_EN_MASK |
						MISC_CTRL_CFG_READ_UR_MODE_MASK |
						MISC_CTRL_MAX_BURST_SIZE_128);

	pci_get_dma_regions(ctlr, &region, 0);
	rc_bar2_offset = region.bus_start - region.phys_start;
	rc_bar2_size = 1ULL << fls64(region.size - 1);

	tmp = lower_32_bits(rc_bar2_offset);
	u32p_replace_bits(&tmp, brcm_pcie_encode_ibar_size(rc_bar2_size),
					  RC_BAR2_CONFIG_LO_SIZE_MASK);
	writel(tmp, base + PCIE_MISC_RC_BAR2_CONFIG_LO);
	writel(upper_32_bits(rc_bar2_offset),
		   base + PCIE_MISC_RC_BAR2_CONFIG_HI);

	scb_size_val = rc_bar2_size ? ilog2(rc_bar2_size) - 15 : 0xf; /* 0xf is 1GB */

	tmp = readl(base + PCIE_MISC_MISC_CTRL);
	u32p_replace_bits(&tmp, scb_size_val,
					  MISC_CTRL_SCB0_SIZE_MASK);
	writel(tmp, base + PCIE_MISC_MISC_CTRL);

	/* Disable the PCIe->GISB memory window (RC_BAR1) */
	clrbits_le32(base + PCIE_MISC_RC_BAR1_CONFIG_LO,
				 RC_BAR1_CONFIG_LO_SIZE_MASK);

	/* Disable the PCIe->SCB memory window (RC_BAR3) */
	clrbits_le32(base + PCIE_MISC_RC_BAR3_CONFIG_LO,
				 RC_BAR3_CONFIG_LO_SIZE_MASK);

	/* Mask all interrupts since we are not handling any yet */
	writel(0xffffffff, base + PCIE_MSI_INTR2_MASK_SET);

	/* Clear any interrupts we find on boot */
	writel(0xffffffff, base + PCIE_MSI_INTR2_CLR);

	if (ctlr->gen)
		brcm_pcie_set_gen(ctlr, ctlr->gen);

	/* Unassert the fundamental reset */
	clrbits_le32(ctlr->base + PCIE_RGR1_SW_INIT_1,
				 PCIE_RGR1_SW_INIT_1_PERST_MASK);
	/* 100ms after PERST# deassertion per PCIe CEM */
	delay_us(100 * 1000);

	/* Give the RC/EP time to wake up, before trying to configure RC.
	 * Intermittently check status for link-up, up to a total of 100ms.
	 */
	for (i = 0; i < 100 && !brcm_pcie_link_up(ctlr); i += 5)
		delay_us(5 * 1000);

	if (!brcm_pcie_link_up(ctlr))
	{
		Kprintf("PCIe BRCM: link down\n");
		return -EINVAL;
	}

	if (!brcm_pcie_rc_mode(ctlr))
	{
		Kprintf("PCIe misconfigured; is in EP mode\n");
		return -EINVAL;
	}

	for (i = 0; i < ctlr->region_count; i++)
	{
		struct pci_region *reg = &ctlr->regions[i];

		if (reg->flags != PCI_REGION_MEM)
			continue;

		if (num_out_wins >= BRCM_NUM_PCIE_OUT_WINS)
			return -EINVAL;

		brcm_pcie_set_outbound_win(ctlr, num_out_wins, reg->phys_start,
								   reg->bus_start, reg->size);

		num_out_wins++;
	}

	/*
	 * For config space accesses on the RC, show the right class for
	 * a PCIe-PCIe bridge (the default setting is to be EP mode).
	 */
	clrsetbits_le32(base + PCIE_RC_CFG_PRIV1_ID_VAL3,
					PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK, 0x060400);

	if (ctlr->ssc)
	{
		ret = brcm_pcie_set_ssc(ctlr->base);
		if (!ret)
			ssc_good = TRUE;
		else
			Kprintf("PCIe BRCM: failed attempt to enter SSC mode\n");
	}

	lnksta = readw(base + BRCM_PCIE_CAP_REGS + PCI_EXP_LNKSTA);
	cls = lnksta & PCI_EXP_LNKSTA_CLS;
	nlw = (lnksta & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;

	Kprintf("[pcie] PCIe BRCM: link up, %s Gbps x%lu %s\n", link_speed_to_str(cls),
			nlw, ssc_good ? "(SSC)" : "(!SSC)");

	/* PCIe->SCB endian mode for BAR */
	// TODO change to big endian?
	clrsetbits_le32(base + PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1,
					PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK,
					VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN);

	/*
	 * We used to enable the CLKREQ# input here, but a few PCIe cards don't
	 * attach anything to the CLKREQ# line, so we shouldn't assume that
	 * it's connected and working. The controller does allow detecting
	 * whether the port on the other side of our link is/was driving this
	 * signal, so we could check before we assume. But because this signal
	 * is for power management, which doesn't make sense in a bootloader,
	 * let's instead just unadvertise ASPM support.
	 */
	clrbits_le32(base + PCIE_RC_CFG_PRIV1_LINK_CAPABILITY,
				 LINK_CAPABILITY_ASPM_SUPPORT_MASK);

	return 0;
}

int brcm_pcie_remove(struct pci_controller *pcie)
{
	void *base = pcie->base;

	/* Assert fundamental reset */
	setbits_le32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_PERST_MASK);

	/* Turn off SerDes */
	setbits_le32(base + PCIE_MISC_HARD_PCIE_HARD_DEBUG,
				 PCIE_HARD_DEBUG_SERDES_IDDQ_MASK);

	/* Shutdown bridge */
	setbits_le32(base + PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT_MASK);

	return 0;
}

int DevTreeParse(struct pci_controller *ctrl)
{
	DT_Init();

	ctrl->dt_node_name = DT_GetAlias("pcie0");
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

	ctrl->base = DT_GetBaseAddress(ctrl->dt_node_name);
	if (ctrl->base == NULL)
	{
		Kprintf("[pcie] %s: Failed to get PCIe base address\n", __func__);
		DT_CloseKey(key);
		return -1;
	}

	Kprintf("[pcie] %s: Device tree info\n", __func__);
	Kprintf("[pcie] %s: compatible: %s\n", __func__, ctrl->compatible);
	Kprintf("[pcie] %s: register base: %08lx\n", __func__, ctrl->base);


	ULONG offset = DT_GetAddressTranslationOffset(ctrl->base);
	ctrl->base = (APTR)((ULONG)ctrl->base + offset);
	Kprintf("[pcie] %s: Found PCIe in CPU space, base address in CPU space: %08lx\n", __func__, ctrl->base);

	//they're not in our dev tree...
	ctrl->gen = 0;
	ctrl->ssc = FALSE;

	// We're done with the device tree
	DT_CloseKey(key);
	return 0;
}

void *map_physmem(phys_addr_t phys_addr, size_t len, int map_flags)
{
	void *virt_addr;

	//TODO emu68 needs to map our BAR window to physical memory
	virt_addr = (void *)(phys_addr + 0);

	return virt_addr;
}