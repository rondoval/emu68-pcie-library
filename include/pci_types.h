#ifndef __PCI_TYPES_H
#define __PCI_TYPES_H

#include <exec/types.h>
#include <exec/lists.h>

#define CONFIG_PCI_BRIDGE_MEM_ALIGNMENT 0x100000
// #define CONFIG_SYS_PCI_64BIT
// #define CONFIG_PCI_MAP_SYSTEM_MEMORY
#define CONFIG_NR_DRAM_BANKS 4 // likely 1 needed?

#ifdef CONFIG_SYS_PCI_64BIT
typedef u64 pci_addr_t;
typedef u64 pci_size_t;
#else
typedef ULONG pci_addr_t;
typedef ULONG pci_size_t;
#endif

typedef uint64_t u64;
typedef ULONG phys_addr_t;
typedef ULONG size_t;

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
	APTR base; /* controller base address */
	int gen;   /* PCI gen to use */
	BOOL ssc;  /* should spread spectrum be enabled */
	CONST_STRPTR compatible;

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
	struct pci_device *pci_bridge; // bridge device that owns this bus

	STRPTR name;

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