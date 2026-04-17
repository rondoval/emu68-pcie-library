/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PCI_BRCMSTB_H
#define _PCI_BRCMSTB_H

#include <exec/types.h>
#include <pci_types.h>

s32 brcm_pcie_probe(struct pci_controller *pcie, u32 bus_number_base);
s32 brcm_pcie_remove(struct pci_controller *pcie);
s32 brcm_pcie_read_config(const struct pci_controller *bus, pci_dev_t bdf,
                                                  u32 offset, u32 *valuep,
                                                  enum pci_size_t size);
s32 brcm_pcie_write_config(struct pci_controller *bus, pci_dev_t bdf,
                                                   u32 offset, u32 value,
                                                   enum pci_size_t size);

s32 bcm2711_reload_vl805_firmware(void);

s32 brcm_pcie_enable_msi(struct pci_controller *pcie);
void brcm_pcie_disable_msi(struct pci_controller *pcie);

s32 pci_get_devtree_dma_regions(struct pci_controller *ctlr, struct pci_region *memp, u32 index);

#endif // PCI_BRCMSTB_H