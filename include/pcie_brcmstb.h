#ifndef _PCI_BRCMSTB_H
#define _PCI_BRCMSTB_H

#include <exec/types.h>
#include <compat.h>

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

/* Access sizes for PCI reads and writes */
enum pci_size_t {
	PCI_SIZE_8,
	PCI_SIZE_16,
	PCI_SIZE_32,
};

typedef int pci_dev_t;

struct pci_region {
	pci_addr_t bus_start;	/* Start on the bus */
	phys_addr_t phys_start;	/* Start in physical address space */
	pci_size_t size;	/* Size */
	ULONG flags;	/* Resource flags */

	pci_addr_t bus_lower;
};

struct pci_device_id {
	unsigned int vendor, device;	/* Vendor and device ID or PCI_ANY_ID */
	unsigned int subvendor, subdevice; /* Subsystem ID's or PCI_ANY_ID */
	unsigned int class, class_mask;	/* (class,subclass,prog-if) triplet */
};

struct pci_controller
{
    APTR base;
    int gen;
    BOOL ssc;
    CONST_STRPTR compatible;

    ULONG region_count;
    struct pci_region *regions;

    struct pci_region *pci_io;
    struct pci_region *pci_mem;
    struct pci_region *pci_prefetch;

	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;

	int indirect_type;

	void (*fixup_irq)(struct pci_controller *, pci_dev_t);    

	CONST_STRPTR dt_node_name;
};

struct pci_bus
{
	struct pci_bus *next; // linked list

	struct pci_controller *controller; // owning controller
	struct pci_bus *parent; // null for root bus

	int bus_number;
	struct pci_device *devices; // list of devices on this bus

	CONST_STRPTR name;
	int first_busno;
	int last_busno;


	struct pci_device *pci_bridge; // unused for root bus. I think.
};

struct pci_device
{
	struct pci_bus *bus; // ref to bus/bridge
	struct pci_device *next;

    pci_dev_t bdf;

	int devfn;
	unsigned short vendor;
	unsigned short device;
	unsigned int class;

	CONST_STRPTR name;
};

int DevTreeParse(struct pci_controller *ctrl);
int brcm_pcie_probe(struct pci_controller *pcie);
int brcm_pcie_remove(struct pci_controller *pcie);
int brcm_pcie_read_config(const struct pci_controller *bus, pci_dev_t bdf,
				 UWORD offset, ULONG *valuep,
				 enum pci_size_t size);
int brcm_pcie_write_config(struct pci_controller *bus, pci_dev_t bdf,
				  UWORD offset, ULONG value,
				  enum pci_size_t size);

void *map_physmem(phys_addr_t phys_addr, size_t len, int map_flags);
#endif // PCI_BRCMSTB_H