#ifndef _PCI_BRCMSTB_H
#define _PCI_BRCMSTB_H

#include <exec/types.h>
#include <pci_types.h>

int brcm_pcie_probe(struct pci_controller *pcie);
int brcm_pcie_remove(struct pci_controller *pcie);
int brcm_pcie_read_config(const struct pci_controller *bus, pci_dev_t bdf,
						  UWORD offset, ULONG *valuep,
						  enum pci_size_t size);
int brcm_pcie_write_config(struct pci_controller *bus, pci_dev_t bdf,
						   UWORD offset, ULONG value,
						   enum pci_size_t size);
#endif // PCI_BRCMSTB_H