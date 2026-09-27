/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Core PCI type definitions and data structures.
 *
 * This header is the shared foundation for the entire pcie library.  It
 * defines the build-time configuration macros (64-bit addresses, DRAM bank
 * count, MSI vector limit), the primitive typedefs (pci_addr_t, phys_addr_t,
 * pci_dev_t), and the main data structures:
 *
 *   struct pci_region     — one address-translation window (I/O, MEM, or prefetch)
 *   struct pci_device_id  — vendor/device/class match record for driver probe tables
 *   struct pci_controller — per-root-complex state (MMIO window, regions, MSI, buses)
 *   struct pci_bus        — one PCI bus segment within a controller
 *   struct pci_device     — one enumerated PCI function
 *
 * All other pcie library headers include this file; do not include it
 * redundantly.
 */

#ifndef __PCI_TYPES_H
#define __PCI_TYPES_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/interrupts.h>
#include <types.h>
#include <bits.h>
#include <libraries/pci_constants.h> /* PCI_IRQ_*: the values of pci_device.active.mode */

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

#define CONFIG_PCI_BRIDGE_MEM_ALIGNMENT 0x100000 /* minimum alignment for bridge memory windows (1 MiB) */
// #define CONFIG_SYS_PCI_64BIT             /* enable 64-bit PCI bus addresses (pci_addr_t = u64); disabled: u32 */
#define CONFIG_PHYS_64BIT /* CPU physical addresses are 64-bit (phys_addr_t = u64) */
// #define CONFIG_PCI_MAP_SYSTEM_MEMORY     /* define if Fast RAM has a 1:1 virt-to-phys mapping */
#define CONFIG_NR_DRAM_BANKS 4 /* number of Fast RAM regions exposed to the allocator (Pi4 has 3 + 1 reserved) */
#define MSI_MAX_VECTORS 32	   /* controller demux width = size of the generic slot/token pool; sized to the BCM2711 MSI register width */

typedef uint64_t u64;

#ifdef CONFIG_SYS_PCI_64BIT
typedef u64 pci_addr_t;
typedef u64 pci_size_t;
#else
typedef u32 pci_addr_t;
typedef u32 pci_size_t;
#endif

#ifdef CONFIG_PHYS_64BIT
typedef u64 phys_addr_t;
typedef u64 size_t; /* byte-count type for BAR sizes and DMA ranges; TODO verify every callsite handles 64-bit correctly */
#else
typedef u32 phys_addr_t;
typedef u32 size_t;
#endif

/* Device state flags stored in pci_device.flags */
#define DM_FLAG_BOUND BIT(0)	 /* device has been bound to a driver (pci_bind_bus_devices completed) */
#define DM_FLAG_ACTIVATED BIT(1) /* device resources have been activated (BARs assigned, bus mastering enabled) */

/* Access sizes for PCI reads and writes */
enum pci_size_t
{
	PCI_SIZE_8,
	PCI_SIZE_16,
	PCI_SIZE_32,
};

typedef u32 pci_dev_t;

/**
 * struct pci_region - One PCI address-translation window
 *
 * Describes a single contiguous range that maps a span of PCI bus addresses
 * to a corresponding span of CPU physical addresses.  A controller typically
 * has separate regions for I/O, non-prefetchable memory, and prefetchable
 * memory (flagged with PCI_REGION_IO / PCI_REGION_MEM / PCI_REGION_PREFETCH).
 *
 * @bus_start:  First bus address of the window
 * @phys_start: Corresponding CPU physical address
 * @size:       Size of the window in bytes
 * @flags:      PCI_REGION_* flags describing the region type
 * @bus_lower:  Rolling allocation pointer; auto-config advances this as BARs
 *              are assigned.  Reset to bus_start by pciauto_config_init().
 */
struct pci_region
{
	pci_addr_t bus_start;	/* first PCI bus address of this window */
	phys_addr_t phys_start; /* corresponding CPU physical address */
	pci_size_t size;		/* size of the window in bytes */
	u32 flags;				/* PCI_REGION_* type flags (e.g. PCI_REGION_MEM, PCI_REGION_IO, PCI_REGION_PREFETCH) */

	pci_addr_t bus_lower; /* rolling BAR-allocation pointer; advanced by pciauto_config_device(), reset by pciauto_config_init() */
};

/**
 * struct pci_device_id - PCI device match record
 *
 * Used to build probe tables for pci_find_device_id() and friends.  Fields
 * set to PCI_ANY_ID (~0) are treated as wildcards.
 *
 * @vendor:     PCI vendor ID (or PCI_ANY_ID)
 * @device:     PCI device ID (or PCI_ANY_ID)
 * @subvendor:  PCI subsystem vendor ID (or PCI_ANY_ID)
 * @subdevice:  PCI subsystem device ID (or PCI_ANY_ID)
 * @class:      Class/subclass/prog-if triplet
 * @class_mask: Bits of @class to compare (0xffffff to match all three bytes)
 */
struct pci_device_id
{
	u16 vendor, device;		  /* Vendor and device ID or PCI_ANY_ID */
	u16 subvendor, subdevice; /* Subsystem ID's or PCI_ANY_ID */
	u32 class, class_mask;	  /* (class,subclass,prog-if) triplet */
};

/**
 * struct pci_controller - BCM2711 PCIe root complex state
 *
 * One instance is allocated per physical PCIe controller.  Holds the MMIO
 * window mapping, the outbound address-translation regions, the MSI
 * aggregation state, the list of all buses reachable from this controller,
 * and the INTx routing table.
 */
struct pci_controller
{
	struct ExecBase *sysBase; /* the library's, set before probe */
	APTR base;				 /* virtual base address of the PCIe RC register block */
	u32 gen;				 /* PCIe generation to negotiate (1–3 on BCM2711) */
	BOOL ssc;				 /* TRUE to enable spread-spectrum clocking on the RC */
	CONST_STRPTR compatible; /* device-tree compatible string used to identify this controller */
	u32 hw_rev;				 /* hardware revision read from the RC revision register */

	phys_addr_t mmio_window_phys; /* CPU physical address of the outbound MMIO aperture */
	u8 *mmio_window_virtual;	  /* CPU virtual address of the outbound MMIO aperture */
	size_t mmio_window_size;	  /* size of the outbound MMIO aperture in bytes */

	u32 region_count;			/* number of entries in the regions array */
	struct pci_region *regions; /* array of all outbound address-translation windows */

	struct pci_region *pci_io;		 /* pointer into regions[] for the I/O window (or NULL) */
	struct pci_region *pci_mem;		 /* pointer into regions[] for the non-prefetchable memory window (or NULL) */
	struct pci_region *pci_prefetch; /* pointer into regions[] for the prefetchable memory window (or NULL) */

	CONST_STRPTR dt_node_name; /* device-tree node name, used for diagnostic messages */

	u32 bus_number_base; /* bus number of the root bus; non-zero when multiple controllers share the bus-number space */
	u32 bus_number_last; /* highest bus number assigned so far during enumeration */

	struct pcie_msi
	{
		pci_addr_t target_addr;						/* MSI doorbell PCI address programmed into device MSI capability registers */
		struct Interrupt *servers[MSI_MAX_VECTORS]; /* Exec Interrupt server bound to each demux slot, NULL if none; a slot is unmasked only while bound */
		u32 used;									/* bitmap of reserved demux slots (bit i = slot i allocated) */
		s32 gic_irq;								/* GIC-400 SPI interrupt line for the BCM2711 MSI aggregation interrupt */
		struct Interrupt isr;						/* Exec Interrupt structure registered with gic400.library for MSI dispatch */
	} msi;

	struct MinList buses;		/* doubly-linked list of all pci_bus structs reachable from this controller */
	struct Library *gic400Base; /* gic400.library, open from probe to remove: the MSI demux interrupt and per-device INTx */

	s32 INT_x_mapping[4]; /* absolute GIC-400 IRQ (SPI + 32, as DT_GetInterrupt) for INTx pins A–D (index 0–3); 0 if not connected */
};

/**
 * struct pci_bus - One PCI bus segment
 *
 * Represents a single bus number within a controller — either the root bus
 * (parent == NULL, pci_bridge == NULL) or a secondary bus behind a PCI-to-PCI
 * bridge.  All devices directly on this bus are listed in @devices.
 */
struct pci_bus
{
	struct MinNode node;			   /* linkage in pci_controller.buses */
	struct pci_controller *controller; /* controller this bus belongs to */
	struct ExecBase *sysBase;			   /* controller->sysBase, copied at creation */
	struct pci_bus *parent;			   /* parent bus, or NULL for the root bus */
	struct pci_device *pci_bridge;	   /* bridge device on the parent bus that created this bus, or NULL for root */

	char name[30]; /* human-readable name for diagnostic output (e.g. "PCI Bus 0") */

	u32 bus_number;			 /* PCI bus number assigned to this segment */
	u32 bus_number_last_sub; /* highest bus number in the subtree rooted at this bus */

	struct MinList devices; /* doubly-linked list of pci_device structs directly on this bus */
};

/**
 * struct pci_bar_info - Cached BAR assignment from auto-configuration
 *
 * Populated by pciauto_setup_device() for every BAR that is successfully
 * assigned a bus address.  Slots that are absent, failed allocation, or are
 * the upper 32-bit half of a 64-bit pair are left zeroed (present == FALSE).
 *
 * Using pci_addr_t / pci_size_t ensures the cache remains correct when
 * CONFIG_SYS_PCI_64BIT is enabled without any further changes here.
 */
struct pci_bar_info
{
	BOOL present;			 /* TRUE if this slot holds a successfully assigned BAR */
	u8 type;				 /* PCI_REGION_MEM (0) or PCI_REGION_IO (1) */
	BOOL is64;				 /* TRUE for the low slot of a 64-bit MEM BAR;
								slot+1 is consumed and stays not-present */
	pci_addr_t bus_addr;	 /* assigned PCIe bus address (u32 today, u64 with CONFIG_SYS_PCI_64BIT) */
	pci_size_t size;		 /* BAR size in bytes */
	pci_size_t bar_response; /* raw BAR sizing response (all-1s write-back) including flag bits;
								for 64-bit MEM BARs this is the full bar64 value; */
	/* NOTE: size and bar_response widen to u64 when CONFIG_SYS_PCI_64BIT is enabled */
	phys_addr_t phys_addr; /* ARM physical address; 0 for I/O BARs (BCM2711 does not map PCI I/O) */
	void *virt_addr;	   /* emu68 virtual address; NULL for I/O BARs */
};

/**
 * struct pci_device - One enumerated PCI function
 *
 * Created during bus scanning for every function that responds to a config
 * read.  Holds the identity fields (vendor/device/class), the MSI capability
 * state, and the INTx routing result.  Linked into pci_bus.devices.
 */
struct pci_device
{
	struct MinNode node; /* linkage in pci_bus.devices */
	struct pci_bus *bus; /* bus this device lives on */

	char name[30]; /* human-readable name for diagnostic output (e.g. "00:00.0") */
	u32 flags;	   /* DM_FLAG_* state bits */

	pci_dev_t bdf; /* packed bus/device/function — use PCI_BUS/PCI_DEV/PCI_FUNC to unpack */
	u32 devfn;	   /* raw devfn encoding: bits [7:3] = device, bits [2:0] = function */

	u16 vendor;		   /* PCI vendor ID read from config space (offset 0x00) */
	u16 device;		   /* PCI device ID read from config space (offset 0x02) */
	u32 class;		   /* 24-bit class/subclass/prog-if read from config space (offset 0x09) */
	u8 revision;	   /* revision ID read from config space (offset 0x08); cached at probe time */
	u16 subsys_vendor; /* subsystem vendor ID (offset 0x2C); cached at probe time */
	u16 subsys_id;	   /* subsystem device ID (offset 0x2E); cached at probe time */

	/* MSI capability, located and decoded at probe (pci_msi_init) */
	struct device_msi
	{
		u32 cap_offset;	  /* byte offset of the MSI capability in config space, or 0 if no MSI */
		u32 mask;		  /* shadow copy of the MSI mask register (kept in sync with hardware) */
		BOOL addr64;	  /* device supports a 64-bit MSI address (PCI_MSI_FLAGS_64BIT) */
		BOOL maskable;	  /* device has a per-vector mask register (PCI_MSI_FLAGS_MASKBIT) */
		u8 log2_max_vecs; /* log2 of the max vectors the device can use (PCI_MSI_FLAGS_QMASK) */
		u16 mask_offset;  /* config offset of PCI_MSI_MASK_32/64 (meaningful only if maskable) */
	} msi;

	/* MSI-X capability, located and its table resolved at probe (pci_msix_init) */
	struct device_msix
	{
		u32 cap_offset;	  /* byte offset of the MSI-X capability in config space, or 0 if none */
		u16 table_size;	  /* number of table entries (Table Size field + 1) */
		void *table_virt; /* CPU-virtual base of the table; NULL = table unreachable, MSI-X unusable */
	} msix;

	/* INTx routing, filled by pci_assign_irq() */
	struct device_intx
	{
		u8 pin;			 /* raw PCI_INTERRUPT_PIN (1=INTA..4=INTD, 0 = no INTx) */
		u8 pin_routed;	 /* pin after bridge swizzle; written back to PCI_INTERRUPT_LINE */
		u32 gic_irq;	 /* absolute GIC-400 IRQ from INT_x_mapping[]; 0 = not routed (no INTx) */
		BOOL prefer_msi; /* legacy EnableMSI() hint: prefer MSI over INTx (never MSI-X) */
	} intx;

	/* Active interrupt allocation — the single source of truth for what is live. */
	struct irq_alloc
	{
		u32 mode;					/* PCI_IRQ_INTX, PCI_IRQ_MSI or PCI_IRQ_MSIX; 0 when idle */
		u16 nvec;					/* number of vectors allocated */
		s32 slots[MSI_MAX_VECTORS]; /* controller demux slot per vector (MSI and MSI-X only) */
		struct Interrupt *intx_server; /* INTx: the server registered with gic400, NULL if none (MSI/MSI-X: pcie->msi.servers[slot]) */
	} active;

	u8 header_type;				 /* PCI header type [6:0] (multifunction bit cleared):
									PCI_HEADER_TYPE_NORMAL / BRIDGE / CARDBUS */
	u8 bars_num;				 /* number of BAR slots for this header type (6 / 2 / 0) */
	struct pci_bar_info bars[6]; /* BAR assignments cached by pciauto_setup_device();
									indexed by physical slot 0–5 */
};

#endif