/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI device lookup — find devices by BDF, vendor/device ID, or class code.
 *
 * All search functions walk the MinList structures maintained inside the
 * pci_controller (list of pci_bus nodes) and each pci_bus (list of pci_device
 * nodes).  Devices are only present after pci_bind_bus_devices() has run.
 */

#ifndef _PCI_LOOKUP_H
#define _PCI_LOOKUP_H

#include <pci_types.h>

/**
 * pci_bus_find_devfn() - Find a device on a single bus by device/function
 *
 * Scans the device list of @bus for a pci_device whose devfn field matches
 * @find_devfn (the bus number is ignored).
 *
 * @bus:         Bus to search
 * @find_devfn:  PCI devfn to match (device and function bits only, no bus)
 * @devp:        Set to the matching device on success
 * Return:       0 on success, -ENODEV if not found
 */
s32 pci_bus_find_devfn(const struct pci_bus *bus, pci_dev_t find_devfn,
					   struct pci_device **devp);

/**
 * pci_bus_find_devices() - Find devices on a single bus by vendor/device ID list
 *
 * Scans @bus for devices matching any entry in the NULL-terminated @ids table.
 * @indexp is decremented for each match; *@devp is set when it reaches zero.
 * Useful for finding the nth matching device.
 *
 * @bus:    Bus to search
 * @ids:    Table of vendor/device ID pairs, terminated by a {0,0} entry
 * @indexp: Match index; set to 0 to return the first match, 1 for the second,
 *          etc.  Decremented for each match found
 * @devp:   Set to the matching device when *@indexp reaches zero
 * Return:  0 when a match is found, -ENODEV if no (further) match exists
 */
s32 pci_bus_find_devices(struct pci_bus *bus, const struct pci_device_id *ids,
						 int *indexp, struct pci_device **devp);

#endif /* _PCI_LOOKUP_H */
