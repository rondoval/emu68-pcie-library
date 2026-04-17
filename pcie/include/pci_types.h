/* SPDX-License-Identifier: GPL-2.0-only */

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

#define CONFIG_PCI_BRIDGE_MEM_ALIGNMENT 0x100000
// #define CONFIG_SYS_PCI_64BIT
#define CONFIG_PHYS_64BIT // physical address is 64bit
// #define CONFIG_PCI_MAP_SYSTEM_MEMORY // there's a 1:1 mapping of virt to phys for Fast RAM I think
#define CONFIG_NR_DRAM_BANKS 4 // There are 3 hunks of Fast RAM on Pi4
#define MSI_MAX_VECTORS 32

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
typedef u64 size_t; // TODO check all occurences
#else
typedef u32 phys_addr_t;
typedef u32 size_t;
#endif

/* Device state flags */
#define DM_FLAG_BOUND BIT(0)
#define DM_FLAG_ACTIVATED BIT(1)

/* Access sizes for PCI reads and writes */
enum pci_size_t
{
	PCI_SIZE_8,
	PCI_SIZE_16,
	PCI_SIZE_32,
};

typedef u32 pci_dev_t;

struct pci_region
{
	pci_addr_t bus_start;	/* Start on the bus */
	phys_addr_t phys_start; /* Start in physical address space */
	pci_size_t size;		/* Size */
	u32 flags;				/* Resource flags */

	pci_addr_t bus_lower;
};

struct pci_device_id
{
	u16 vendor, device;		  /* Vendor and device ID or PCI_ANY_ID */
	u16 subvendor, subdevice; /* Subsystem ID's or PCI_ANY_ID */
	u32 class, class_mask;	  /* (class,subclass,prog-if) triplet */
};

struct pci_controller
{
	APTR base;				 /* controller base address */
	u32 gen;				 /* PCI gen to use (1-5) */
	BOOL ssc;				 /* should spread spectrum be enabled */
	CONST_STRPTR compatible; /* compatible string */
	u32 hw_rev;				 /* hardware revision */

	phys_addr_t mmio_window_phys; /* physical address of MMIO window */
	u8 *mmio_window_virtual;	  /* CPU virtual address of MMIO window */
	size_t mmio_window_size;	  /* size of MMIO window */

	u32 region_count;
	struct pci_region *regions;

	struct pci_region *pci_io;
	struct pci_region *pci_mem;
	struct pci_region *pci_prefetch;

	CONST_STRPTR dt_node_name;

	u32 bus_number_base; /* root bus number. This is in case there are multiple controllers/root buses */
	u32 bus_number_last; /* last assigned bus number */

	struct pcie_msi
	{
		pci_addr_t target_addr;    /* MSI doorbell address written by the device */
		struct Interrupt *vectors[MSI_MAX_VECTORS]; /* per-vector Exec interrupt servers */
		s32 num_vectors;           /* number of allocated vector slots */
		s32 gic_irq;               /* GIC-400 SPI line for the MSI aggregation interrupt */
		struct Interrupt isr;      /* Exec Interrupt registered with gic400.library */
		BOOL enabled;
	} msi;

	struct MinList buses;
	struct Library *gic400Base;

	s32 INT_x_mapping[4]; /* mapping for INT A-D; -1 = not mapped */
};

struct pci_bus
{
	struct MinNode node;
	struct pci_controller *controller; // controller of this bus
	struct pci_bus *parent;			   // null for root bus
	struct pci_device *pci_bridge;	   // bridge device that owns this bus

	char name[30];

	u32 bus_number;
	u32 bus_number_last_sub; /* last sub bus number assigned */

	struct MinList devices; // list of devices on this bus
};

struct pci_device
{
	struct MinNode node;
	struct pci_bus *bus; // ref to bus/bridge

	char name[30];
	u32 flags;

	pci_dev_t bdf;
	u32 devfn;

	u16 vendor;
	u16 device;
	u32 class;

	struct flags_msi
	{
		u8 addr64;        /* device supports 64-bit MSI address (PCI_MSI_FLAGS_64BIT) */
		u8 maskable;      /* device has a per-vector mask register (PCI_MSI_FLAGS_MASKBIT) */
		u8 log2_max_vecs; /* log2 of max vectors advertised by device (PCI_MSI_FLAGS_QMASK) */
		u8 log2_num_vecs; /* log2 of vectors allocated/enabled (PCI_MSI_FLAGS_QSIZE) */
		u16 mask_offset;  /* config-space byte offset of the MSI mask register */
	} msi_flags;

	struct device_msi
	{
		u32 cap_offset; /* config-space offset of MSI capability, or 0 if none */
		BOOL enabled;
		s32 vector;     /* assigned MSI vector index (TODO: support multiple per device) */
		u32 mask;       /* cached value of the MSI mask register */
	} msi;

	BOOL prefer_msi; /* TRUE - use MSI when configuring this device */
	u8 irq_pin; /* INTA=1, INTB=2, INTC=3, INTD=4 */
	u8 irq_line; /* GIC-400 IRQ line */
};

#endif