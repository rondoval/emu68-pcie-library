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
 * pci_bus_find_bdf() - Find a device across a controller by full BDF address
 *
 * Locates the bus for the bus number encoded in @bdf, then searches that bus
 * for a device matching the device/function fields of @bdf.
 *
 * @controller: Controller to search
 * @bdf:        Full bus/device/function address (see PCI_BDF())
 * @devp:       Set to the matching device on success
 * Return:      0 on success, -ENODEV if not found
 */
s32 pci_bus_find_bdf(struct pci_controller *controller, pci_dev_t bdf,
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

/**
 * pci_find_device_id() - Find a device across all buses by vendor/device ID list
 *
 * Searches every bus registered under @controller for the @index-th device
 * matching any entry in @ids.
 *
 * @controller: Controller to search
 * @ids:        Table of vendor/device ID pairs, terminated by a {0,0} entry
 * @index:      0-based match index (0 = first match, 1 = second, etc.)
 * @devp:       Set to the matching device on success; NULL on failure
 * Return:      0 on success, -ENODEV if not found
 */
s32 pci_find_device_id(struct pci_controller *controller,
					   const struct pci_device_id *ids, int index,
					   struct pci_device **devp);

/**
 * pci_find_device() - Find a device across all buses by vendor and device ID
 *
 * Searches every bus under @controller for the @index-th device matching
 * both @vendor and @device.
 *
 * @controller: Controller to search
 * @vendor:     PCI vendor ID to match
 * @device:     PCI device ID to match
 * @index:      0-based match index
 * @devp:       Set to the matching device on success; NULL on failure
 * Return:      0 on success, -ENODEV if not found
 */
s32 pci_find_device(struct pci_controller *controller, u16 vendor, u16 device,
					int index, struct pci_device **devp);

/**
 * pci_find_class() - Find a device across all buses by class code
 *
 * Searches every bus under @controller for the @index-th device whose 24-bit
 * class field (base class << 16 | sub-class << 8 | prog-if) matches
 * @find_class exactly.
 *
 * @controller: Controller to search
 * @find_class: 24-bit class code to match
 * @index:      0-based match index
 * @devp:       Set to the matching device on success; NULL on failure
 * Return:      0 on success, -ENODEV if not found
 */
s32 pci_find_class(struct pci_controller *controller, u32 find_class,
				   int index, struct pci_device **devp);

/**
 * pci_find_first_device() - Return the first enumerated device in a controller
 *
 * Iterates to the first bus in @controller and returns its first device.
 * Use pci_find_next_device() to advance to subsequent devices.  *@devp is
 * set to NULL if the controller has no devices.
 *
 * @controller: Controller to iterate
 * @devp:       Set to the first device, or NULL if none exist
 * Return:      0 on success, negative on error
 */
s32 pci_find_first_device(struct pci_controller *controller,
						  struct pci_device **devp);

/**
 * pci_find_next_device() - Advance to the next device in enumeration order
 *
 * Given the device most recently returned by pci_find_first_device() or a
 * previous call to this function, advances *@devp to the next device in
 * bus-major, device-minor order.  Sets *@devp to NULL when all devices have
 * been visited.
 *
 * @devp: On entry, the last device returned.  On exit, the next device or
 *        NULL if no further devices exist.
 * Return: 0 on success, negative on error
 */
s32 pci_find_next_device(struct pci_device **devp);

#endif /* _PCI_LOOKUP_H */
