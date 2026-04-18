/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI bus and device probe functions.
 *
 * These functions build and maintain the in-memory PCI bus and device trees
 * that represent the enumerated hardware topology.  The typical call sequence
 * for a fresh scan is:
 *
 *   1. pci_create_bus()         — allocate the root bus node
 *   2. pci_bind_bus_devices()   — scan config space and create pci_device nodes
 *   3. pci_auto_config_devices() — assign BARs and configure each device
 *
 * For downstream bridges pci_probe_bus() orchestrates steps 2–3 recursively,
 * calling pciauto_prescan_setup_bridge() before the scan and
 * pciauto_postscan_setup_bridge() after, so bridge windows are sized correctly.
 */

#ifndef _PCI_PROBE_H
#define _PCI_PROBE_H

#include <pci_types.h>

/**
 * pci_skip_dev() - Test whether a device should be excluded from enumeration
 *
 * Called during bus scanning before binding a device.  Returns non-zero if
 * the device should be skipped (e.g. blacklisted by the platform).
 *
 * @hose: Controller performing the scan
 * @dev:  BDF address of the candidate device
 * Return: 0 to include the device, non-zero to skip it
 */
s32 pci_skip_dev(struct pci_controller *hose, pci_dev_t dev);

/**
 * pci_create_bus() - Allocate and register a new PCI bus node
 *
 * Allocates a pci_bus structure, links it into @ctlr's bus list, and
 * associates it with the bridge device @bridge that leads to it.  The
 * parent bus @parent may be NULL for the root bus.
 *
 * @busp:   Receives the pointer to the new bus on success
 * @parent: Parent bus (NULL for the root bus)
 * @bridge: Bridge device whose secondary side is this bus
 * @ctlr:   Controller that owns the bus
 * Return:  0 on success, -ENOMEM if allocation fails
 */
s32 pci_create_bus(struct pci_bus **busp, struct pci_bus *parent,
				   struct pci_device *bridge, struct pci_controller *ctlr);

/**
 * pci_probe_bus() - Probe a downstream bus behind a bridge
 *
 * Verifies that the bridge device presents a Type 1 (bridge) header, assigns
 * a bus number, calls pciauto_prescan_setup_bridge(), scans devices via
 * pci_bind_bus_devices(), configures them with pci_auto_config_devices(), and
 * finalises the bridge window with pciauto_postscan_setup_bridge().
 *
 * Parent buses are probed recursively before the bridge is activated to ensure
 * the full ancestry is ready before the subordinate scan proceeds.
 *
 * Does nothing and returns 0 for the root bus (bus->pci_bridge == NULL).
 *
 * @bus:    Bus to probe
 * Return:  0 on success, negative on error
 */
s32 pci_probe_bus(struct pci_bus *bus);

/**
 * pci_bind_bus_devices() - Scan a PCI bus and create device nodes
 *
 * Iterates over all device/function slots on @bus, reads vendor, device, and
 * class from config space, and calls pci_create_device() for each valid entry.
 * Skips absent slots (vendor 0x0000 or 0xffff) and non-first-function slots
 * on single-function devices.
 *
 * @bus:   Bus to scan
 * Return: 0 on success, negative on error
 */
s32 pci_bind_bus_devices(struct pci_bus *bus);

/**
 * pci_auto_config_devices() - Assign BARs and configure all devices on a bus
 *
 * Calls pciauto_config_init() to reset the allocator state for the controller,
 * then iterates over every device on @bus and calls pciauto_config_device() to
 * assign BARs, set up the command register, and configure MSI.  Bridge devices
 * trigger a recursive scan of their subordinate bus.
 *
 * @bus:   Bus whose devices should be configured
 * Return: 0 on success, negative on error
 */
s32 pci_auto_config_devices(struct pci_bus *bus);

/**
 * pci_create_device() - Allocate and register a new PCI device node
 *
 * Allocates a pci_device structure, populates it from the supplied fields,
 * and links it into @bus's device list.
 *
 * @bus:         Bus the device lives on
 * @bdf:         Full bus/device/function address (see PCI_BDF())
 * @vendor:      PCI vendor ID read from config space
 * @device:      PCI device ID read from config space
 * @class:       24-bit class code (base << 16 | sub << 8 | prog-if)
 * @header_type: PCI_HEADER_TYPE value with multifunction bit already cleared
 * @devp:        Receives the pointer to the new device on success
 * Return:       0 on success, -ENOMEM if allocation fails
 */
s32 pci_create_device(struct pci_bus *bus, pci_dev_t bdf, u16 vendor,
					  u16 device, u32 class, u8 header_type,
					  struct pci_device **devp);

#endif /* _PCI_PROBE_H */
