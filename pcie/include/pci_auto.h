/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PCI_AUTO_H
#define __PCI_AUTO_H

#include <pci_types.h>

void pciauto_config_init(struct pci_controller *hose);

/**
 * pciauto_config_device() - configure a device ready for use
 *
 * Space is allocated for each PCI base address register (BAR) so that the
 * devices are mapped into memory and I/O space ready for use.
 *
 * @dev:	Device to configure
 * Return: 0 if OK, -ve on error
 */
s32 pciauto_config_device(struct pci_device *dev);

void pciauto_prescan_setup_bridge(struct pci_bus *dev);
void pciauto_postscan_setup_bridge(struct pci_bus *dev);

#endif