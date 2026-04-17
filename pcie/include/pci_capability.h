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
 * pci_find_next_capability() - Find a capability starting after a given offset
 *
 * Walks the standard capability list from the entry whose next-pointer is
 * stored at @start + PCI_CAP_LIST_NEXT, looking for a capability with ID @cap.
 * Use this to iterate multiple instances of the same capability type by
 * passing the offset of the previously found capability as @start.
 *
 * @dev:   Device to search
 * @start: Offset of the capability to resume after (not of the next-pointer)
 * @cap:   Capability ID to find (e.g. PCI_CAP_ID_MSI, PCI_CAP_ID_EXP)
 * Return: Config-space offset of the matching capability, or 0 if not found
 */
u32 pci_find_next_capability(struct pci_device *dev, u8 start, u8 cap);

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
 * pci_find_next_ext_capability() - Find a PCIe extended capability from an offset
 *
 * Walks the PCIe extended capability list (which starts at 0x100) beginning
 * at @start, looking for a capability with extended ID @cap.  Pass @start = 0
 * to search from the beginning.  Use the previously returned offset as @start
 * to find subsequent instances.
 *
 * @dev:   Device to search
 * @start: Config-space offset to start from (0 to start at 0x100)
 * @cap:   Extended capability ID to find (e.g. PCI_EXT_CAP_ID_ERR)
 * Return: Config-space offset of the matching capability, or 0 if not found
 */
u32 pci_find_next_ext_capability(struct pci_device *dev, u32 start, u16 cap);

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
