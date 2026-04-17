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

#if defined(__INTELLISENSE__)
#define asm(x)
#define __attribute__(x)
#endif

#define CONFIG_PCI_BRIDGE_MEM_ALIGNMENT 0x100000 /* minimum alignment for bridge memory windows (1 MiB) */
// #define CONFIG_SYS_PCI_64BIT             /* enable 64-bit PCI bus addresses (pci_addr_t = u64); disabled: u32 */
#define CONFIG_PHYS_64BIT                    /* CPU physical addresses are 64-bit (phys_addr_t = u64) */
// #define CONFIG_PCI_MAP_SYSTEM_MEMORY     /* define if Fast RAM has a 1:1 virt-to-phys mapping */
#define CONFIG_NR_DRAM_BANKS 4               /* number of Fast RAM regions exposed to the allocator (Pi4 has 3 + 1 reserved) */
#define MSI_MAX_VECTORS 32                   /* maximum MSI vectors per controller; sized to the BCM2711 MSI register width */

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
#define DM_FLAG_BOUND     BIT(0) /* device has been bound to a driver (pci_bind_bus_devices completed) */
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

	pci_addr_t bus_lower;   /* rolling BAR-allocation pointer; advanced by pciauto_config_device(), reset by pciauto_config_init() */
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
	APTR base;				 /* virtual base address of the PCIe RC register block */
	u32 gen;				 /* PCIe generation to negotiate (1–3 on BCM2711) */
	BOOL ssc;				 /* TRUE to enable spread-spectrum clocking on the RC */
	CONST_STRPTR compatible; /* device-tree compatible string used to identify this controller */
	u32 hw_rev;				 /* hardware revision read from the RC revision register */

	phys_addr_t mmio_window_phys; /* CPU physical address of the outbound MMIO aperture */
	u8 *mmio_window_virtual;	  /* CPU virtual address of the outbound MMIO aperture */
	size_t mmio_window_size;	  /* size of the outbound MMIO aperture in bytes */

	u32 region_count;           /* number of entries in the regions array */
	struct pci_region *regions; /* array of all outbound address-translation windows */

	struct pci_region *pci_io;       /* pointer into regions[] for the I/O window (or NULL) */
	struct pci_region *pci_mem;      /* pointer into regions[] for the non-prefetchable memory window (or NULL) */
	struct pci_region *pci_prefetch; /* pointer into regions[] for the prefetchable memory window (or NULL) */

	CONST_STRPTR dt_node_name; /* device-tree node name, used for diagnostic messages */

	u32 bus_number_base; /* bus number of the root bus; non-zero when multiple controllers share the bus-number space */
	u32 bus_number_last; /* highest bus number assigned so far during enumeration */

	struct pcie_msi
	{
		pci_addr_t target_addr;                     /* MSI doorbell PCI address programmed into device MSI capability registers */
		struct Interrupt *vectors[MSI_MAX_VECTORS]; /* Exec Interrupt server for each allocated MSI vector slot */
		s32 num_vectors;                            /* number of vector slots currently allocated */
		s32 gic_irq;                               /* GIC-400 SPI interrupt line for the BCM2711 MSI aggregation interrupt */
		struct Interrupt isr;                       /* Exec Interrupt structure registered with gic400.library for MSI dispatch */
		BOOL enabled;                              /* TRUE once brcm_pcie_enable_msi() has succeeded */
	} msi;

	struct MinList buses;       /* doubly-linked list of all pci_bus structs reachable from this controller */
	struct Library *gic400Base; /* open gic400.library base pointer used for MSI interrupt registration */

	s32 INT_x_mapping[4]; /* GIC-400 IRQ line for INTx pins A–D (index 0–3); -1 if not connected */
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
	struct MinNode node;                /* linkage in pci_controller.buses */
	struct pci_controller *controller; /* controller this bus belongs to */
	struct pci_bus *parent;			   /* parent bus, or NULL for the root bus */
	struct pci_device *pci_bridge;	   /* bridge device on the parent bus that created this bus, or NULL for root */

	char name[30];                   /* human-readable name for diagnostic output (e.g. "PCI Bus 0") */

	u32 bus_number;          /* PCI bus number assigned to this segment */
	u32 bus_number_last_sub; /* highest bus number in the subtree rooted at this bus */

	struct MinList devices; /* doubly-linked list of pci_device structs directly on this bus */
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
	struct MinNode node;     /* linkage in pci_bus.devices */
	struct pci_bus *bus;     /* bus this device lives on */

	char name[30];           /* human-readable name for diagnostic output (e.g. "00:00.0") */
	u32 flags;               /* DM_FLAG_* state bits */

	pci_dev_t bdf;           /* packed bus/device/function — use PCI_BUS/PCI_DEV/PCI_FUNC to unpack */
	u32 devfn;               /* raw devfn encoding: bits [7:3] = device, bits [2:0] = function */

	u16 vendor;              /* PCI vendor ID read from config space (offset 0x00) */
	u16 device;              /* PCI device ID read from config space (offset 0x02) */
	u32 class;               /* 24-bit class/subclass/prog-if read from config space (offset 0x09) */

	/* MSI capability flags decoded from PCI_MSI_FLAGS at msi_capability_init() time */
	struct flags_msi
	{
		u8 addr64;        /* 1 if device supports 64-bit MSI address (PCI_MSI_FLAGS_64BIT) */
		u8 maskable;      /* 1 if device has a per-vector mask register (PCI_MSI_FLAGS_MASKBIT) */
		u8 log2_max_vecs; /* log2 of the maximum number of vectors the device can use (PCI_MSI_FLAGS_QMASK) */
		u8 log2_num_vecs; /* log2 of the number of vectors actually allocated (PCI_MSI_FLAGS_QSIZE) */
		u16 mask_offset;  /* byte offset of PCI_MSI_MASK_32/64 within config space, or 0 if not maskable */
	} msi_flags;

	/* per-device MSI runtime state */
	struct device_msi
	{
		u32 cap_offset; /* byte offset of the MSI capability structure in config space, or 0 if no MSI */
		BOOL enabled;   /* TRUE once msi_capability_init() has enabled MSI on this device */
		s32 vector;     /* controller MSI vector slot assigned to this device; TODO: extend for multi-vector */
		u32 mask;       /* shadow copy of the MSI mask register (kept in sync with hardware) */
	} msi;

	BOOL prefer_msi; /* TRUE if the driver requests MSI rather than INTx when both are available */
	u8 irq_pin;      /* INTx pin reported in PCI_INTERRUPT_PIN: 1=INTA, 2=INTB, 3=INTC, 4=INTD, 0=none */
	u8 irq_line;     /* GIC-400 SPI line assigned by pci_assign_irq() via INT_x_mapping[] */
};

#endif