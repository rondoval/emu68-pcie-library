/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI capability list search.
 *
 * Standard PCI capabilities (type-0 and type-1 headers) are chained via the
 * Capability List starting at offset PCI_CAPABILITY_LIST.  PCIe extended
 * capabilities start at offset 0x100 in the extended config space.
 *
 * All functions return the byte offset of the matching capability structure
 * within the device's config space, or 0 if the capability is not present.
 * A TTL counter limits the walk to avoid infinite loops on corrupt hardware.
 */

#ifndef _PCI_CAPABILITY_H
#define _PCI_CAPABILITY_H

#include <pci_types.h>

/**
 * pci_find_capability() - Find the first occurrence of a PCI capability
 *
 * Checks PCI_STATUS_CAP_LIST and then walks the standard capability list
 * from its root (PCI_CAPABILITY_LIST for normal/bridge headers, or
 * PCI_CB_CAPABILITY_LIST for CardBus headers).
 *
 * @dev:   Device to search
 * @cap:   Capability ID to find (e.g. PCI_CAP_ID_MSI, PCI_CAP_ID_EXP)
 * Return: Config-space offset of the capability structure, or 0 if absent
 */
u32 pci_find_capability(struct pci_device *dev, u8 cap);

/**
 * pci_find_ext_capability() - Find the first occurrence of a PCIe extended capability
 *
 * Searches the PCIe extended capability list starting at offset 0x100.
 *
 * @dev:   Device to search
 * @cap:   Extended capability ID to find (e.g. PCI_EXT_CAP_ID_ERR)
 * Return: Config-space offset of the capability structure, or 0 if absent
 */
u32 pci_find_ext_capability(struct pci_device *dev, u16 cap);

#endif /* _PCI_CAPABILITY_H */
