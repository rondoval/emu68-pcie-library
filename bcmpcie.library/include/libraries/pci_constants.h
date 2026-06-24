/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * (C) Copyright 2001 Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Andreas Heppel <aheppel@sysgo.de>
 *
 * (C) Copyright 2002
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 * Copyright (c) 2021  Maciej W. Rozycki <macro@orcam.me.uk>
 */

#ifndef LIBRARIES_PCI_CONSTANTS_H
#define LIBRARIES_PCI_CONSTANTS_H

#include <types.h>
#include <bits.h>

#define PCI_DEVFN(slot, func) ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn) (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn) ((devfn) & 0x07)

#define PCI_CFG_SPACE_SIZE 256
#define PCI_CFG_SPACE_EXP_SIZE 4096

/*
 * Under PCI, each device has 256 bytes of configuration address space,
 * of which the first 64 bytes are standardized as follows:
 */
#define PCI_STD_HEADER_SIZEOF 64
#define PCI_VENDOR_ID 0x00				 /* 16 bits */
#define PCI_DEVICE_ID 0x02				 /* 16 bits */
#define PCI_COMMAND 0x04				 /* 16 bits */
#define PCI_COMMAND_IO BIT(0)			 /* Enable response in I/O space */
#define PCI_COMMAND_MEMORY BIT(1)		 /* Enable response in Memory space */
#define PCI_COMMAND_MASTER BIT(2)		 /* Enable bus mastering */
#define PCI_COMMAND_SPECIAL BIT(3)		 /* Enable response to special cycles */
#define PCI_COMMAND_INVALIDATE BIT(4)	 /* Use memory write and invalidate */
#define PCI_COMMAND_VGA_PALETTE BIT(5)	 /* Enable palette snooping */
#define PCI_COMMAND_PARITY BIT(6)		 /* Enable parity checking */
#define PCI_COMMAND_WAIT BIT(7)			 /* Enable address/data stepping */
#define PCI_COMMAND_SERR BIT(8)			 /* Enable SERR */
#define PCI_COMMAND_FAST_BACK BIT(9)	 /* Enable back-to-back writes */
#define PCI_COMMAND_INTX_DISABLE BIT(10) /* INTx Emulation Disable */

#define PCI_STATUS 0x06				 /* 16 bits */
#define PCI_STATUS_IMM_READY BIT(0)	 /* Immediate Readiness */
#define PCI_STATUS_INTERRUPT BIT(3)	 /* Interrupt status */
#define PCI_STATUS_CAP_LIST BIT(4)	 /* Support Capability List */
#define PCI_STATUS_66MHZ BIT(5)		 /* Support 66 Mhz PCI 2.1 bus */
#define PCI_STATUS_UDF BIT(6)		 /* Support User Definable Features [obsolete] */
#define PCI_STATUS_FAST_BACK BIT(7)	 /* Accept fast-back to back */
#define PCI_STATUS_PARITY BIT(8)	 /* Detected parity error */
#define PCI_STATUS_DEVSEL_MASK 0x600 /* DEVSEL timing */
#define PCI_STATUS_DEVSEL_FAST 0x000
#define PCI_STATUS_DEVSEL_MEDIUM 0x200
#define PCI_STATUS_DEVSEL_SLOW 0x400
#define PCI_STATUS_SIG_TARGET_ABORT BIT(11) /* Set on target abort */
#define PCI_STATUS_REC_TARGET_ABORT BIT(12) /* Master ack of " */
#define PCI_STATUS_REC_MASTER_ABORT BIT(13) /* Set on master abort */
#define PCI_STATUS_SIG_SYSTEM_ERROR BIT(14) /* Set when we drive SERR */
#define PCI_STATUS_DETECTED_PARITY BIT(15)	/* Set on parity error */

#define PCI_CLASS_REVISION 0x08 /* High 24 bits are class, low 8 \
				   revision */
#define PCI_REVISION_ID 0x08	/* Revision ID */
#define PCI_CLASS_PROG 0x09		/* Reg. Level Programming Interface */
#define PCI_CLASS_DEVICE 0x0a	/* Device class */
#define PCI_CLASS_CODE 0x0b		/* Device class code */
#define PCI_CLASS_SUB_CODE 0x0a /* Device sub-class code */

#define PCI_CACHE_LINE_SIZE 0x0c /* 8 bits */
#define PCI_LATENCY_TIMER 0x0d	 /* 8 bits */
#define PCI_HEADER_TYPE 0x0e	 /* 8 bits */
#define PCI_HEADER_TYPE_NORMAL 0
#define PCI_HEADER_TYPE_BRIDGE 1
#define PCI_HEADER_TYPE_CARDBUS 2

#define PCI_BIST 0x0f			/* 8 bits */
#define PCI_BIST_CODE_MASK 0x0f /* Return result */
#define PCI_BIST_START BIT(6)	/* 1 to start BIST, 2 secs or less */
#define PCI_BIST_CAPABLE BIT(7) /* 1 if BIST capable */

/*
 * Base addresses specify locations in memory or I/O space.
 * Decoded size can be determined by writing a value of
 * 0xffffffff to the register, and reading it back.  Only
 * 1 bits are decoded.
 */
#define PCI_BASE_ADDRESS_0 0x10		  /* 32 bits */
#define PCI_BASE_ADDRESS_1 0x14		  /* 32 bits [htype 0,1 only] */
#define PCI_BASE_ADDRESS_2 0x18		  /* 32 bits [htype 0 only] */
#define PCI_BASE_ADDRESS_3 0x1c		  /* 32 bits */
#define PCI_BASE_ADDRESS_4 0x20		  /* 32 bits */
#define PCI_BASE_ADDRESS_5 0x24		  /* 32 bits */
#define PCI_BASE_ADDRESS_SPACE BIT(0) /* 0 = memory, 1 = I/O */
#define PCI_BASE_ADDRESS_SPACE_IO BIT(0)
#define PCI_BASE_ADDRESS_SPACE_MEMORY 0x00
#define PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x06
#define PCI_BASE_ADDRESS_MEM_TYPE_32 0x00	 /* 32 bit address */
#define PCI_BASE_ADDRESS_MEM_TYPE_1M 0x02	 /* Below 1M [obsolete] */
#define PCI_BASE_ADDRESS_MEM_TYPE_64 0x04	 /* 64 bit address */
#define PCI_BASE_ADDRESS_MEM_PREFETCH BIT(3) /* prefetchable? */
#define PCI_BASE_ADDRESS_MEM_MASK (~0x0fUL)
#define PCI_BASE_ADDRESS_IO_MASK (~0x03UL)
/* bit 1 is reserved if address_space = 1 */

/* Convert a regsister address (e.g. PCI_BASE_ADDRESS_1) to a bar # (e.g. 1) */
#define pci_offset_to_barnum(offset) \
	(((offset) - PCI_BASE_ADDRESS_0) / sizeof(ULONG))

/* Header type 0 (normal devices) */
#define PCI_CARDBUS_CIS 0x28
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c
#define PCI_SUBSYSTEM_ID 0x2e
#define PCI_ROM_ADDRESS 0x30 /* Bits 31..11 are address, 10..1 reserved */
#define PCI_ROM_ADDRESS_ENABLE BIT(0)
#define PCI_ROM_ADDRESS_MASK (~0x7ffULL)

#define PCI_CAPABILITY_LIST 0x34 /* Offset of first capability list entry */

/* 0x35-0x3b are reserved */
#define PCI_INTERRUPT_LINE 0x3c /* 8 bits */
#define PCI_INTERRUPT_PIN 0x3d	/* 8 bits */
#define PCI_MIN_GNT 0x3e		/* 8 bits */
#define PCI_MAX_LAT 0x3f		/* 8 bits */

#define PCI_INTERRUPT_LINE_DISABLE 0xff

/* Header type 1 (PCI-to-PCI bridges) */
#define PCI_PRIMARY_BUS 0x18	   /* Primary bus number */
#define PCI_SECONDARY_BUS 0x19	   /* Secondary bus number */
#define PCI_SUBORDINATE_BUS 0x1a   /* Highest bus number behind the bridge */
#define PCI_SEC_LATENCY_TIMER 0x1b /* Latency timer for secondary interface */
#define PCI_IO_BASE 0x1c		   /* I/O range behind the bridge */
#define PCI_IO_LIMIT 0x1d
#define PCI_IO_RANGE_TYPE_MASK 0x0f /* I/O bridging type */
#define PCI_IO_RANGE_TYPE_16 0x00
#define PCI_IO_RANGE_TYPE_32 0x01
#define PCI_IO_RANGE_MASK ~0x0fU
#define PCI_SEC_STATUS 0x1e	 /* Secondary status register, only bit 14 used */
#define PCI_MEMORY_BASE 0x20 /* Memory range behind */
#define PCI_MEMORY_LIMIT 0x22
#define PCI_MEMORY_RANGE_TYPE_MASK 0x0f
#define PCI_MEMORY_RANGE_MASK ~0x0fU
#define PCI_PREF_MEMORY_BASE 0x24 /* Prefetchable memory range behind */
#define PCI_PREF_MEMORY_LIMIT 0x26
#define PCI_PREF_RANGE_TYPE_MASK 0x0f
#define PCI_PREF_RANGE_TYPE_32 0x00
#define PCI_PREF_RANGE_TYPE_64 0x01
#define PCI_PREF_RANGE_MASK ~0x0fU
#define PCI_PREF_BASE_UPPER32 0x28 /* Upper half of prefetchable memory range */
#define PCI_PREF_LIMIT_UPPER32 0x2c
#define PCI_IO_BASE_UPPER16 0x30 /* Upper half of I/O addresses */
#define PCI_IO_LIMIT_UPPER16 0x32
/* 0x34 same as for htype 0 */
/* 0x35-0x3b is reserved */
#define PCI_ROM_ADDRESS1 0x38 /* Same as PCI_ROM_ADDRESS, but for htype 1 */
/* 0x3c-0x3d are same as for htype 0 */
#define PCI_BRIDGE_CONTROL 0x3e
#define PCI_BRIDGE_CTL_PARITY BIT(0)	   /* Enable parity detection on secondary interface */
#define PCI_BRIDGE_CTL_SERR BIT(1)		   /* The same for SERR forwarding */
#define PCI_BRIDGE_CTL_NO_ISA BIT(2)	   /* Disable bridging of ISA ports */
#define PCI_BRIDGE_CTL_VGA BIT(3)		   /* Forward VGA addresses */
#define PCI_BRIDGE_CTL_MASTER_ABORT BIT(5) /* Report master aborts */
#define PCI_BRIDGE_CTL_BUS_RESET BIT(6)	   /* Secondary bus reset */
#define PCI_BRIDGE_CTL_FAST_BACK BIT(7)	   /* Fast Back2Back enabled on secondary interface */

/* Header type 2 (CardBus bridges) */
#define PCI_CB_CAPABILITY_LIST 0x14
/* 0x15 reserved */
#define PCI_CB_SEC_STATUS 0x16		/* Secondary status */
#define PCI_CB_PRIMARY_BUS 0x18		/* PCI bus number */
#define PCI_CB_CARD_BUS 0x19		/* CardBus bus number */
#define PCI_CB_SUBORDINATE_BUS 0x1a /* Subordinate bus number */
#define PCI_CB_LATENCY_TIMER 0x1b	/* CardBus latency timer */
#define PCI_CB_MEMORY_BASE_0 0x1c
#define PCI_CB_MEMORY_LIMIT_0 0x20
#define PCI_CB_MEMORY_BASE_1 0x24
#define PCI_CB_MEMORY_LIMIT_1 0x28
#define PCI_CB_IO_BASE_0 0x2c
#define PCI_CB_IO_BASE_0_HI 0x2e
#define PCI_CB_IO_LIMIT_0 0x30
#define PCI_CB_IO_LIMIT_0_HI 0x32
#define PCI_CB_IO_BASE_1 0x34
#define PCI_CB_IO_BASE_1_HI 0x36
#define PCI_CB_IO_LIMIT_1 0x38
#define PCI_CB_IO_LIMIT_1_HI 0x3a
#define PCI_CB_IO_RANGE_MASK ~0x03
/* 0x3c-0x3d are same as for htype 0 */
#define PCI_CB_BRIDGE_CONTROL 0x3e
#define PCI_CB_BRIDGE_CTL_PARITY BIT(0) /* Similar to standard bridge control register */
#define PCI_CB_BRIDGE_CTL_SERR BIT(1)
#define PCI_CB_BRIDGE_CTL_ISA BIT(2)
#define PCI_CB_BRIDGE_CTL_VGA BIT(3)
#define PCI_CB_BRIDGE_CTL_MASTER_ABORT BIT(5)
#define PCI_CB_BRIDGE_CTL_CB_RESET BIT(6)	   /* CardBus reset */
#define PCI_CB_BRIDGE_CTL_16BIT_INT BIT(7)	   /* Enable interrupt for 16-bit cards */
#define PCI_CB_BRIDGE_CTL_PREFETCH_MEM0 BIT(8) /* Prefetch enable for both memory regions */
#define PCI_CB_BRIDGE_CTL_PREFETCH_MEM1 BIT(9)
#define PCI_CB_BRIDGE_CTL_POST_WRITES BIT(10)
#define PCI_CB_SUBSYSTEM_VENDOR_ID 0x40
#define PCI_CB_SUBSYSTEM_ID 0x42
#define PCI_CB_LEGACY_MODE_BASE 0x44 /* 16-bit PC Card legacy mode base address (ExCa) */
/* 0x48-0x7f reserved */

// TODO reg space

/* Capability lists */

#define PCI_CAP_LIST_ID 0	   /* Capability ID */
#define PCI_CAP_ID_PM 0x01	   /* Power Management */
#define PCI_CAP_ID_AGP 0x02	   /* Accelerated Graphics Port */
#define PCI_CAP_ID_VPD 0x03	   /* Vital Product Data */
#define PCI_CAP_ID_SLOTID 0x04 /* Slot Identification */
#define PCI_CAP_ID_MSI 0x05	   /* Message Signalled Interrupts */
#define PCI_CAP_ID_CHSWP 0x06  /* CompactPCI HotSwap */
#define PCI_CAP_ID_PCIX 0x07   /* PCI-X */
#define PCI_CAP_ID_HT 0x08	   /* HyperTransport */
#define PCI_CAP_ID_VNDR 0x09   /* Vendor-Specific */
#define PCI_CAP_ID_DBG 0x0A	   /* Debug port */
#define PCI_CAP_ID_CCRC 0x0B   /* CompactPCI Central Resource Control */
#define PCI_CAP_ID_SHPC 0x0C   /* PCI Standard Hot-Plug Controller */
#define PCI_CAP_ID_SSVID 0x0D  /* Bridge subsystem vendor/device ID */
#define PCI_CAP_ID_AGP3 0x0E   /* AGP Target PCI-PCI bridge */
#define PCI_CAP_ID_SECDEV 0x0F /* Secure Device */
#define PCI_CAP_ID_EXP 0x10	   /* PCI Express */
#define PCI_CAP_ID_MSIX 0x11   /* MSI-X */
#define PCI_CAP_ID_SATA 0x12   /* SATA Data/Index Conf. */
#define PCI_CAP_ID_AF 0x13	   /* PCI Advanced Features */
#define PCI_CAP_ID_EA 0x14	   /* PCI Enhanced Allocation */
#define PCI_CAP_ID_MAX PCI_CAP_ID_EA
#define PCI_CAP_LIST_NEXT 1 /* Next capability in the list */
#define PCI_CAP_FLAGS 2		/* Capability defined flags (16 bits) */
#define PCI_CAP_SIZEOF 4

/* Power Management Registers */

#define PCI_PM_CAP_VER_MASK 0x0007		   /* Version */
#define PCI_PM_CAP_PME_CLOCK BIT(3)		   /* PME clock required */
#define PCI_PM_CAP_AUX_POWER BIT(4)		   /* Auxilliary power support */
#define PCI_PM_CAP_DSI BIT(5)			   /* Device specific initialization */
#define PCI_PM_CAP_D1 BIT(9)			   /* D1 power state support */
#define PCI_PM_CAP_D2 BIT(10)			   /* D2 power state support */
#define PCI_PM_CAP_PME BIT(11)			   /* PME pin supported */
#define PCI_PM_CTRL 4					   /* PM control and status register */
#define PCI_PM_CTRL_STATE_MASK 0x0003	   /* Current power state (D0 to D3) */
#define PCI_PM_CTRL_PME_ENABLE BIT(8)	   /* PME pin enable */
#define PCI_PM_CTRL_DATA_SEL_MASK 0x1e00   /* Data select (??) */
#define PCI_PM_CTRL_DATA_SCALE_MASK 0x6000 /* Data scale (??) */
#define PCI_PM_CTRL_PME_STATUS BIT(15)	   /* PME pin status */
#define PCI_PM_PPB_EXTENSIONS 6			   /* PPB support extensions (??) */
#define PCI_PM_PPB_B2_B3 BIT(6)			   /* Stop clock when in D3hot (??) */
#define PCI_PM_BPCC_ENABLE BIT(7)		   /* Bus power/clock control enable (??) */
#define PCI_PM_DATA_REGISTER 7			   /* (??) */
#define PCI_PM_SIZEOF 8

/* AGP registers */

#define PCI_AGP_VERSION 2				   /* BCD version number */
#define PCI_AGP_RFU 3					   /* Rest of capability flags */
#define PCI_AGP_STATUS 4				   /* Status register */
#define PCI_AGP_STATUS_RQ_MASK 0xff000000  /* Maximum number of requests - 1 */
#define PCI_AGP_STATUS_SBA BIT(9)		   /* Sideband addressing supported */
#define PCI_AGP_STATUS_64BIT BIT(5)		   /* 64-bit addressing supported */
#define PCI_AGP_STATUS_FW BIT(4)		   /* FW transfers supported */
#define PCI_AGP_STATUS_RATE4 BIT(2)		   /* 4x transfer rate supported */
#define PCI_AGP_STATUS_RATE2 BIT(1)		   /* 2x transfer rate supported */
#define PCI_AGP_STATUS_RATE1 BIT(0)		   /* 1x transfer rate supported */
#define PCI_AGP_COMMAND 8				   /* Control register */
#define PCI_AGP_COMMAND_RQ_MASK 0xff000000 /* Master: Maximum number of requests */
#define PCI_AGP_COMMAND_SBA BIT(9)		   /* Sideband addressing enabled */
#define PCI_AGP_COMMAND_AGP BIT(8)		   /* Allow processing of AGP transactions */
#define PCI_AGP_COMMAND_64BIT BIT(5)	   /* Allow processing of 64-bit addresses */
#define PCI_AGP_COMMAND_FW BIT(4)		   /* Force FW transfers */
#define PCI_AGP_COMMAND_RATE4 BIT(2)	   /* Use 4x rate */
#define PCI_AGP_COMMAND_RATE2 BIT(1)	   /* Use 2x rate */
#define PCI_AGP_COMMAND_RATE1 BIT(0)	   /* Use 1x rate */
#define PCI_AGP_SIZEOF 12

/* PCI-X registers */

#define PCI_X_CMD_DPERR_E BIT(0)			   /* Data Parity Error Recovery Enable */
#define PCI_X_CMD_ERO BIT(1)				   /* Enable Relaxed Ordering */
#define PCI_X_CMD_MAX_READ 0x0000			   /* Max Memory Read Byte Count */
#define PCI_X_CMD_MAX_SPLIT 0x0030			   /* Max Outstanding Split Transactions */
#define PCI_X_CMD_VERSION(x) (((x) >> 12) & 3) /* Version */

/* Slot Identification */

#define PCI_SID_ESR 2			/* Expansion Slot Register */
#define PCI_SID_ESR_NSLOTS 0x1f /* Number of expansion slots available */
#define PCI_SID_ESR_FIC BIT(5)	/* First In Chassis Flag */
#define PCI_SID_CHASSIS_NR 3	/* Chassis Number */

/* Message Signalled Interrupts registers */

#define PCI_MSI_FLAGS 0x02			 /* Message Control */
#define PCI_MSI_FLAGS_ENABLE BIT(0)	 /* MSI feature enabled */
#define PCI_MSI_FLAGS_QMASK 0x000e	 /* Maximum queue size available */
#define PCI_MSI_FLAGS_QSIZE 0x0070	 /* Message queue size configured */
#define PCI_MSI_FLAGS_64BIT BIT(7)	 /* 64-bit addresses allowed */
#define PCI_MSI_FLAGS_MASKBIT BIT(8) /* Per-vector masking capable */
#define PCI_MSI_RFU 3				 /* Rest of capability flags */
#define PCI_MSI_ADDRESS_LO 0x04		 /* Lower 32 bits */
#define PCI_MSI_ADDRESS_HI 0x08		 /* Upper 32 bits (if PCI_MSI_FLAGS_64BIT set) */
#define PCI_MSI_DATA_32 0x08		 /* 16 bits of data for 32-bit devices */
#define PCI_MSI_MASK_32 0x0c		 /* Mask bits register for 32-bit devices */
#define PCI_MSI_PENDING_32 0x10		 /* Pending intrs for 32-bit devices */
#define PCI_MSI_DATA_64 0x0c		 /* 16 bits of data for 64-bit devices */
#define PCI_MSI_MASK_64 0x10		 /* Mask bits register for 64-bit devices */
#define PCI_MSI_PENDING_64 0x14		 /* Pending intrs for 64-bit devices */

/* MSI-X registers (in the capability structure, config space) */
#define PCI_MSIX_FLAGS 0x02				 /* Message Control */
#define PCI_MSIX_FLAGS_QSIZE 0x07ff		 /* Table size - 1 */
#define PCI_MSIX_FLAGS_MASKALL BIT(14)	 /* Mask all vectors for this function */
#define PCI_MSIX_FLAGS_ENABLE BIT(15)	 /* MSI-X enable */
#define PCI_MSIX_TABLE 0x04				 /* Table offset / BAR indicator (BIR) */
#define PCI_MSIX_TABLE_BIR 0x00000007	 /* BAR index holding the table */
#define PCI_MSIX_TABLE_OFFSET 0xfffffff8 /* Offset into the BAR (qword-aligned) */
#define PCI_MSIX_PBA 0x08				 /* Pending bit array offset / BIR */
#define PCI_MSIX_PBA_BIR 0x00000007		 /* BAR index holding the PBA */
#define PCI_MSIX_PBA_OFFSET 0xfffffff8	 /* Offset into the BAR */

/*
 * Interrupt-type selector flags for AllocIntVectors().  A type is attempted
 * only when its bit is set, in priority order MSI-X -> MSI -> INTx.  Omit a bit
 * to forbid that type (e.g. drop PCI_IRQ_MSIX to disable MSI-X).
 */
#define PCI_IRQ_INTX BIT(0) /* legacy INTx pin */
#define PCI_IRQ_MSI BIT(1)	/* message-signalled interrupts */
#define PCI_IRQ_MSIX BIT(2) /* extended message-signalled interrupts */
#define PCI_IRQ_ALL_TYPES (PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX)

/* MSI-X table entry layout (16 bytes per vector, in device memory / a BAR) */
#define PCI_MSIX_ENTRY_SIZE 16
#define PCI_MSIX_ENTRY_LOWER_ADDR 0x00	   /* Message Address low 32 bits */
#define PCI_MSIX_ENTRY_UPPER_ADDR 0x04	   /* Message Address high 32 bits */
#define PCI_MSIX_ENTRY_DATA 0x08		   /* Message Data */
#define PCI_MSIX_ENTRY_VECTOR_CTRL 0x0c	   /* Vector Control */
#define PCI_MSIX_ENTRY_CTRL_MASKBIT BIT(0) /* Per-vector mask bit */

#define PCI_MAX_PCI_DEVICES 32
#define PCI_MAX_PCI_FUNCTIONS 8

#define PCI_FIND_CAP_TTL 0x48
#define CAP_START_POS 0x40

/* Extended Capabilities (PCI-X 2.0 and Express) */
#define PCI_EXT_CAP_ID(header) (header & 0x0000ffff)
#define PCI_EXT_CAP_VER(header) ((header >> 16) & 0xf)
#define PCI_EXT_CAP_NEXT(header) ((header >> 20) & 0xffc)

#define PCI_EXT_CAP_ID_ERR 0x01		/* Advanced Error Reporting */
#define PCI_EXT_CAP_ID_VC 0x02		/* Virtual Channel Capability */
#define PCI_EXT_CAP_ID_DSN 0x03		/* Device Serial Number */
#define PCI_EXT_CAP_ID_PWR 0x04		/* Power Budgeting */
#define PCI_EXT_CAP_ID_RCLD 0x05	/* Root Complex Link Declaration */
#define PCI_EXT_CAP_ID_RCILC 0x06	/* Root Complex Internal Link Control */
#define PCI_EXT_CAP_ID_RCEC 0x07	/* Root Complex Event Collector */
#define PCI_EXT_CAP_ID_MFVC 0x08	/* Multi-Function VC Capability */
#define PCI_EXT_CAP_ID_VC9 0x09		/* same as _VC */
#define PCI_EXT_CAP_ID_RCRB 0x0A	/* Root Complex RB? */
#define PCI_EXT_CAP_ID_VNDR 0x0B	/* Vendor-Specific */
#define PCI_EXT_CAP_ID_CAC 0x0C		/* Config Access - obsolete */
#define PCI_EXT_CAP_ID_ACS 0x0D		/* Access Control Services */
#define PCI_EXT_CAP_ID_ARI 0x0E		/* Alternate Routing ID */
#define PCI_EXT_CAP_ID_ATS 0x0F		/* Address Translation Services */
#define PCI_EXT_CAP_ID_SRIOV 0x10	/* Single Root I/O Virtualization */
#define PCI_EXT_CAP_ID_MRIOV 0x11	/* Multi Root I/O Virtualization */
#define PCI_EXT_CAP_ID_MCAST 0x12	/* Multicast */
#define PCI_EXT_CAP_ID_PRI 0x13		/* Page Request Interface */
#define PCI_EXT_CAP_ID_AMD_XXX 0x14 /* Reserved for AMD */
#define PCI_EXT_CAP_ID_REBAR 0x15	/* Resizable BAR */
#define PCI_EXT_CAP_ID_DPA 0x16		/* Dynamic Power Allocation */
#define PCI_EXT_CAP_ID_TPH 0x17		/* TPH Requester */
#define PCI_EXT_CAP_ID_LTR 0x18		/* Latency Tolerance Reporting */
#define PCI_EXT_CAP_ID_SECPCI 0x19	/* Secondary PCIe Capability */
#define PCI_EXT_CAP_ID_PMUX 0x1A	/* Protocol Multiplexing */
#define PCI_EXT_CAP_ID_PASID 0x1B	/* Process Address Space ID */
#define PCI_EXT_CAP_ID_DPC 0x1D		/* Downstream Port Containment */
#define PCI_EXT_CAP_ID_L1SS 0x1E	/* L1 PM Substates */
#define PCI_EXT_CAP_ID_PTM 0x1F		/* Precision Time Measurement */
#define PCI_EXT_CAP_ID_MAX PCI_EXT_CAP_ID_PTM

/* Enhanced Allocation Registers */
#define PCI_EA_NUM_ENT 2		  /* Number of Capability Entries */
#define PCI_EA_NUM_ENT_MASK 0x3f  /* Num Entries Mask */
#define PCI_EA_FIRST_ENT 4		  /* First EA Entry in List */
#define PCI_EA_FIRST_ENT_BRIDGE 8 /* First EA Entry for Bridges */
#define PCI_EA_ES 0x00000007	  /* Entry Size */
#define PCI_EA_BEI 0x000000f0	  /* BAR Equivalent Indicator */

#define PCI_EA_SEC_BUS_MASK 0xff
#define PCI_EA_SUB_BUS_MASK 0xff00
#define PCI_EA_SUB_BUS_SHIFT 8

#define PCI_EA_BEI_BAR0 0
#define PCI_EA_BEI_BAR5 5
#define PCI_EA_BEI_BRIDGE 6
#define PCI_EA_BEI_ENI 7
#define PCI_EA_BEI_ROM 8
/* 9-14 map to VF BARs 0-5 respectively */
#define PCI_EA_BEI_VF_BAR0 9
#define PCI_EA_BEI_VF_BAR5 14
#define PCI_EA_BEI_RESERVED 15

#define PCI_EA_PP 0x0000ff00	/* Primary Properties */
#define PCI_EA_SP 0x00ff0000	/* Secondary Properties */
#define PCI_EA_WRITABLE BIT(30) /* Writable flag */
#define PCI_EA_ENABLE BIT(31)	/* Enable flag */
/* Base, MaxOffset registers */
/* bit 0 is reserved */
#define PCI_EA_IS_64 BIT(1)			 /* 64-bit field flag */
#define PCI_EA_FIELD_MASK 0xfffffffc /* For Base & Max Offset */

#define PCI_EA_P_MEM 0x00
#define PCI_EA_P_MEM_PREFETCH 0x01
#define PCI_EA_P_IO 0x02
#define PCI_EA_P_VF_MEM_PREFETCH 0x03
#define PCI_EA_P_VF_MEM 0x04
#define PCI_EA_P_BRIDGE_MEM 0x05
#define PCI_EA_P_BRIDGE_MEM_PREFETCH 0x06
#define PCI_EA_P_BRIDGE_IO 0x07
#define PCI_EA_P_MEM_RESERVED 0xfd
#define PCI_EA_P_IO_RESERVED 0xfe
#define PCI_EA_P_UNAVAILABLE 0xff

/* PCI Express capabilities */
#define PCI_EXP_FLAGS 2						/* Capabilities register */
#define PCI_EXP_FLAGS_VERS 0x000f			/* Capability Version */
#define PCI_EXP_FLAGS_TYPE 0x00f0			/* Device/Port type */
#define PCI_EXP_TYPE_ROOT_PORT 0x4			/* Root Port */
#define PCI_EXP_TYPE_DOWNSTREAM 0x6			/* Downstream Port */
#define PCI_EXP_TYPE_PCIE_BRIDGE 0x8		/* PCI/PCI-X to PCIe Bridge */
#define PCI_EXP_DEVCAP 4					/* Device capabilities */
#define PCI_EXP_DEVCAP_FLR BIT(28)			/* Function Level Reset */
#define PCI_EXP_DEVCAP_PAYLOAD 0x0007		/* Max payload size supported */
#define PCI_EXP_DEVCAP_PAYLOAD_128B 0x0000	/* 128 Bytes */
#define PCI_EXP_DEVCAP_PAYLOAD_256B 0x0001	/* 256 Bytes */
#define PCI_EXP_DEVCAP_PAYLOAD_512B 0x0002	/* 512 Bytes */
#define PCI_EXP_DEVCAP_PAYLOAD_1024B 0x0003 /* 1024 Bytes */
#define PCI_EXP_DEVCAP_PAYLOAD_2048B 0x0004 /* 2048 Bytes */
#define PCI_EXP_DEVCAP_PAYLOAD_4096B 0x0005 /* 4096 Bytes */
#define PCI_EXP_DEVCTL 8					/* Device Control */
#define PCI_EXP_DEVCTL_PAYLOAD 0x00e0		/* Max_Payload_Size */
#define PCI_EXP_DEVCTL_PAYLOAD_128B 0x0000	/* 128 Bytes */
#define PCI_EXP_DEVCTL_PAYLOAD_256B 0x0020	/* 256 Bytes */
#define PCI_EXP_DEVCTL_PAYLOAD_512B 0x0040	/* 512 Bytes */
#define PCI_EXP_DEVCTL_PAYLOAD_1024B 0x0060 /* 1024 Bytes */
#define PCI_EXP_DEVCTL_PAYLOAD_2048B 0x0080 /* 2048 Bytes */
#define PCI_EXP_DEVCTL_PAYLOAD_4096B 0x00a0 /* 4096 Bytes */
#define PCI_EXP_DEVCTL_RELAX_EN BIT(4)		/* Enable relaxed ordering */
#define PCI_EXP_DEVCTL_NOSNOOP_EN BIT(11)	/* Enable No Snoop */
#define PCI_EXP_DEVCTL_READRQ 0x7000		/* Max_Read_Request_Size */
#define PCI_EXP_DEVCTL_READRQ_128B 0x0000	/* 128 Bytes */
#define PCI_EXP_DEVCTL_READRQ_256B 0x1000	/* 256 Bytes */
#define PCI_EXP_DEVCTL_READRQ_512B 0x2000	/* 512 Bytes */
#define PCI_EXP_DEVCTL_READRQ_1024B 0x3000	/* 1024 Bytes */
#define PCI_EXP_DEVCTL_READRQ_2048B 0x4000	/* 2048 Bytes */
#define PCI_EXP_DEVCTL_READRQ_4096B 0x5000	/* 4096 Bytes */
#define PCI_EXP_DEVCTL_BCR_FLR BIT(15)		/* Bridge Configuration Retry / FLR */
#define PCI_EXP_LNKCAP 12					/* Link Capabilities */
#define PCI_EXP_LNKCAP_SLS 0x0000000f		/* Supported Link Speeds */
#define PCI_EXP_LNKCAP_SLS_2_5GB 0x00000001 /* LNKCAP2 SLS Vector bit 0 */
#define PCI_EXP_LNKCAP_SLS_5_0GB 0x00000002 /* LNKCAP2 SLS Vector bit 1 */
#define PCI_EXP_LNKCAP_SLS_8_0GB 0x00000003 /* LNKCAP2 SLS Vector bit 2 */
#define PCI_EXP_LNKCAP_MLW 0x000003f0		/* Maximum Link Width */
#define PCI_EXP_LNKCAP_ASPMS 0x00000c00		/* ASPM Support */
#define PCI_EXP_LNKCAP_ASPM_L0S BIT(10)		/* ASPM L0s Support */
#define PCI_EXP_LNKCAP_ASPM_L1 BIT(11)		/* ASPM L1 Support */
#define PCI_EXP_LNKCAP_DLLLARC BIT(20)		/* Data Link Layer Link Active Reporting Capable */
#define PCI_EXP_LNKCTL 16					/* Link Control */
#define PCI_EXP_LNKCTL_RL BIT(5)			/* Retrain Link */
#define PCI_EXP_LNKSTA 18					/* Link Status */
#define PCI_EXP_LNKSTA_CLS 0x000f			/* Current Link Speed */
#define PCI_EXP_LNKSTA_CLS_2_5GB 0x0001		/* Current Link Speed 2.5GT/s */
#define PCI_EXP_LNKSTA_CLS_5_0GB 0x0002		/* Current Link Speed 5.0GT/s */
#define PCI_EXP_LNKSTA_CLS_8_0GB 0x0003		/* Current Link Speed 8.0GT/s */
#define PCI_EXP_LNKSTA_NLW 0x03f0			/* Negotiated Link Width */
#define PCI_EXP_LNKSTA_NLW_SHIFT 4			/* start of NLW mask in link status */
#define PCI_EXP_LNKSTA_LT BIT(11)			/* Link Training */
#define PCI_EXP_LNKSTA_DLLLA BIT(13)		/* Data Link Layer Link Active */
#define PCI_EXP_LNKSTA_LBMS BIT(14)			/* Link Bandwidth Management Status */
#define PCI_EXP_SLTCAP 20					/* Slot Capabilities */
#define PCI_EXP_SLTCAP_HPC BIT(6)			/* Hot-Plug Capable */
#define PCI_EXP_SLTCAP_PSN 0xfff80000		/* Physical Slot Number */
#define PCI_EXP_RTCTL 28					/* Root Control */
#define PCI_EXP_RTCTL_CRSSVE BIT(4)			/* CRS Software Visibility Enable */
#define PCI_EXP_RTCAP 30					/* Root Capabilities */
#define PCI_EXP_RTCAP_CRSVIS BIT(0)			/* CRS Software Visibility capability */
#define PCI_EXP_DEVCAP2 36					/* Device Capabilities 2 */
#define PCI_EXP_DEVCAP2_ARI BIT(5)			/* ARI Forwarding Supported */
#define PCI_EXP_DEVCTL2 40					/* Device Control 2 */
#define PCI_EXP_DEVCTL2_ARI BIT(5)			/* Alternative Routing-ID */
#define PCI_EXP_LNKCAP2 44					/* Link Capability 2 */
#define PCI_EXP_LNKCAP2_SLS 0x000000fe		/* Supported Link Speeds Vector */
#define PCI_EXP_LNKCTL2 48					/* Link Control 2 */
#define PCI_EXP_LNKCTL2_TLS 0x000f			/* Target Link Speed */
#define PCI_EXP_LNKCTL2_TLS_2_5GT 0x0001	/* Target Link Speed 2.5GT/s */
#define PCI_EXP_LNKCTL2_TLS_5_0GT 0x0002	/* Target Link Speed 5.0GT/s */
#define PCI_EXP_LNKCTL2_TLS_8_0GT 0x0003	/* Target Link Speed 8.0GT/s */

/* Advanced Error Reporting */
#define PCI_ERR_CAP 24				  /* Advanced Error Capabilities */
#define PCI_ERR_CAP_FEP(x) ((x) & 31) /* First Error Pointer */
#define PCI_ERR_CAP_ECRC_GENC BIT(5)  /* ECRC Generation Capable */
#define PCI_ERR_CAP_ECRC_GENE BIT(6)  /* ECRC Generation Enable */
#define PCI_ERR_CAP_ECRC_CHKC BIT(7)  /* ECRC Check Capable */
#define PCI_ERR_CAP_ECRC_CHKE BIT(8)  /* ECRC Check Enable */

/* Single Root I/O Virtualization Registers */
#define PCI_SRIOV_CAP 0x04		  /* SR-IOV Capabilities */
#define PCI_SRIOV_CTRL 0x08		  /* SR-IOV Control */
#define PCI_SRIOV_CTRL_VFE BIT(0) /* VF Enable */
#define PCI_SRIOV_CTRL_MSE BIT(3) /* VF Memory Space Enable */
#define PCI_SRIOV_CTRL_ARI BIT(4) /* ARI Capable Hierarchy */
#define PCI_SRIOV_INITIAL_VF 0x0c /* Initial VFs */
#define PCI_SRIOV_TOTAL_VF 0x0e	  /* Total VFs */
#define PCI_SRIOV_NUM_VF 0x10	  /* Number of VFs */
#define PCI_SRIOV_VF_OFFSET 0x14  /* First VF Offset */
#define PCI_SRIOV_VF_STRIDE 0x16  /* Following VF Stride */
#define PCI_SRIOV_VF_DID 0x1a	  /* VF Device ID */

/* PCI region flags (for PCIe_MapBAR) */
#define PCI_REGION_MEM 0x00000000 /* PCI memory space */
#define PCI_REGION_IO BIT(0)	  /* PCI IO space */
#define PCI_REGION_TYPE 0x00000001
#define PCI_REGION_PREFETCH BIT(3) /* prefetchable PCI memory */

#define PCI_REGION_SYS_MEMORY BIT(8) /* System memory */
#define PCI_REGION_RO BIT(9)		 /* Read-only memory */

#endif /* LIBRARIES_PCI_CONSTANTS_H */
