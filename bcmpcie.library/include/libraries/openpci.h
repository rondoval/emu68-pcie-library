#ifndef LIBRARIES_OPENPCI_H
#define LIBRARIES_OPENPCI_H
/*
**	$VER: openpci.h 3.0 (30.08.2024)
**	Includes Release 3.0
**
**	openpci.library interface structures and definitions.
**
*/

/*****************************************************************************/


#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

/* pci_bus() result flags
** Multiple flags may be set if multiple boards are
** found.
*/
#define BCM2711PCIeBus 0x80

/* pci_allocdma_mem flags */
#define MEM_PCI			0x1  /* on the PCI bus, always implied */
#define MEM_NONCACHEABLE	0x2  /* target memory must not be cachable for 68K */
#define MEM_24BITDMA		0x4  /* hint: 24-bit DMA address required; no-op on BCM2711 */

#define MIN_OPENPCI_VERSION 3 /* Version 3 or more */

/*****************************************************************************/

#if defined(__GNUC__)
# pragma pack(2)
#endif

struct pci_dev {
  void             *bus;         /* this is not filled nor used */
  struct  pci_dev  *next;        /* Next pci_dev, or NULL. NOT an exec style list     */
  struct  pci_dev  *pred;        /* Previous pci_dev, or NULL. NOT an exec style list */
  
  UBYTE             devfn;       /* encoded device & function index, (device << 3) | function */
  UBYTE             kludgefill;  /* not used, stricly for WORD alignment */
  UWORD             vendor;      /* PCI vendor ID */
  UWORD             device;      /* PCI device ID */
  ULONG             devclass;    /* 3 bytes: (base<<16,sub<<8,prog-if<<0) */
  ULONG             hdr_type;    /* PCI header type */
  UWORD             master;      /* actually, a collection of flags, see below */
  /*
   * In theory, the irq level can be read from configuration
   * space and all would be fine.  However, old PCI chips don't
   * support these registers and return 0 instead.  For example,
   * the Vision864-P rev 0 chip can uses INTA, but returns 0 in
   * the interrupt line and pin registers.  pci_init()
   * initializes this field with the value at PCI_INTERRUPT_LINE
   * and it is the job of pcibios_fixup() to change it if
   * necessary.  The field must not be 0 unless the device
   * cannot generate interrupts at all.
   */
  ULONG             irq;         /* this is actually the IRQ line as found in the config area */

  /* Base registers for this device */
  ULONG             base_address[6];  /* these should actually be APTRs, they are 68K addresses */
  ULONG	            base_size[6];     /* one's complement of the size, actually, and some flags */
  ULONG             rom_address;      /* 68K address of where the ROM is mapped, or NULL */
  ULONG             rom_size;         /* one's complemenet of the size, plus flags */
  void             *reserved;         /* never ever used */
  /* the following is new in version 3 */
  UBYTE            *legacy_io;        /* 68K pointer to legacy IO space */
};

/*
** the pci_bus structure is not anymore supported, the library
** flattens the PCI hierarchy (if any)
*/

#if defined(__GNUC__)
# pragma pack()
#endif

#endif /* LIBRARIES_OPENPCI_H */
