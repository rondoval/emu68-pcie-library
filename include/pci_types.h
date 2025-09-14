#ifndef __PCI_TYPES_H
#define __PCI_TYPES_H

#include <exec/types.h>
#include <exec/lists.h>

#define CONFIG_PCI_BRIDGE_MEM_ALIGNMENT 0x100000
// #define CONFIG_SYS_PCI_64BIT
#define CONFIG_PHYS_64BIT 			 	// physical address is 64bit
// #define CONFIG_PCI_MAP_SYSTEM_MEMORY // there's a 1:1 mapping of virt to phys for Fast RAM I think
#define CONFIG_NR_DRAM_BANKS 4 			// There are 3 hunks of Fast RAM on Pi4

typedef uint64_t u64;

#ifdef CONFIG_SYS_PCI_64BIT
typedef u64 pci_addr_t;
typedef u64 pci_size_t;
#else
typedef ULONG pci_addr_t;
typedef ULONG pci_size_t;
#endif


#ifdef CONFIG_PHYS_64BIT
typedef u64 phys_addr_t;
typedef u64 size_t;		 // TODO check all occurences
#else
typedef ULONG phys_addr_t;
typedef ULONG size_t;
#endif

/* Device state flags */
#define DM_FLAG_BOUND 0x0001
#define DM_FLAG_ACTIVATED 0x0002

/* Access sizes for PCI reads and writes */
enum pci_size_t
{
	PCI_SIZE_8,
	PCI_SIZE_16,
	PCI_SIZE_32,
};

typedef int pci_dev_t;

struct pci_region
{
	pci_addr_t bus_start;	/* Start on the bus */
	phys_addr_t phys_start; /* Start in physical address space */
	pci_size_t size;		/* Size */
	ULONG flags;			/* Resource flags */

	pci_addr_t bus_lower;
};

struct pci_device_id
{
	unsigned int vendor, device;	   /* Vendor and device ID or PCI_ANY_ID */
	unsigned int subvendor, subdevice; /* Subsystem ID's or PCI_ANY_ID */
	unsigned int class, class_mask;	   /* (class,subclass,prog-if) triplet */
};

struct pci_controller
{
	APTR base;				 /* controller base address */
	int gen;				 /* PCI gen to use */
	BOOL ssc;				 /* should spread spectrum be enabled */
	CONST_STRPTR compatible; /* compatible string */

	phys_addr_t mmio_window_phys; /* physical address of MMIO window */
	ULONG mmio_window_virtual;	  /* virtual address of MMIO window */
	size_t mmio_window_size;	  /* size of MMIO window */

	ULONG region_count;
	struct pci_region *regions;

	struct pci_region *pci_io;
	struct pci_region *pci_mem;
	struct pci_region *pci_prefetch;

	CONST_STRPTR dt_node_name;

	int bus_number_base; /* root bus number. This is in case there are multiple controllers/root buses */
	int bus_number_last; /* last assigned bus number */
};

struct pci_bus
{
	struct MinNode node;
	struct pci_controller *controller; // controller of this bus
	struct pci_bus *parent;			   // null for root bus
	struct pci_device *pci_bridge;	   // bridge device that owns this bus

	UBYTE name[30];

	int bus_number;
	int bus_number_last_sub; /* last sub bus number assigned */

	struct MinList devices; // list of devices on this bus
};

struct pci_device
{
	struct MinNode node;
	struct pci_bus *bus; // ref to bus/bridge

	UBYTE name[30];
	UBYTE flags;

	pci_dev_t bdf;
	int devfn;

	unsigned short vendor;
	unsigned short device;
	unsigned int class;
};

#endif