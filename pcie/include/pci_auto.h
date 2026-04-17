/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI BAR and bridge auto-configuration.
 *
 * These helpers are invoked during bus enumeration to assign base addresses to
 * device BARs and to size and configure PCI-to-PCI bridge memory windows.
 * Typical call sequence within pci_auto_config_devices():
 *
 *   1. pciauto_config_init()           -- reset region allocators
 *   2. For each bridge found:
 *        pciauto_prescan_setup_bridge() -- open a temporary max-size window
 *        < recurse into downstream bus >
 *        pciauto_postscan_setup_bridge() -- trim window to allocated space
 *   3. pciauto_config_device()         -- assign BARs for each leaf device
 */

#ifndef __PCI_AUTO_H
#define __PCI_AUTO_H

#include <pci_types.h>

/**
 * pciauto_config_init() - Reset BAR allocation state for a controller
 *
 * Walks the controller's region list and resets each region's bus_lower
 * pointer to bus_start, so that a fresh enumeration pass starts allocation
 * from the beginning of each window.  Must be called before any
 * pciauto_config_device() or pciauto_prescan_setup_bridge() calls.
 *
 * @hose: Controller whose regions should be reset
 */
void pciauto_config_init(struct pci_controller *hose);

/**
 * pciauto_config_device() - Assign BARs for a PCI device
 *
 * Reads each BAR to determine its size and type (I/O or memory), then
 * allocates space from the appropriate controller region and writes the
 * assigned address back.  Also writes the PCI_COMMAND register to enable
 * the newly mapped resources.
 *
 * @dev:	Device to configure
 * Return: 0 if OK, -ve on error
 */
s32 pciauto_config_device(struct pci_device *dev);

/**
 * pciauto_prescan_setup_bridge() - Open bridge windows before scanning downstream
 *
 * Writes temporary maximum-range I/O and memory windows into the bridge's
 * config-space window registers so that devices behind the bridge can be
 * reached during enumeration.  Must be called before scanning the secondary
 * bus; paired with pciauto_postscan_setup_bridge().
 *
 * @dev: Bus whose bridge device should be pre-configured
 */
void pciauto_prescan_setup_bridge(struct pci_bus *dev);

/**
 * pciauto_postscan_setup_bridge() - Finalise bridge windows after scanning
 *
 * Trims the bridge's I/O and memory window registers down to the amount of
 * space actually consumed by devices on the secondary bus.  Must be called
 * after scanning is complete for a bus that was set up by
 * pciauto_prescan_setup_bridge().
 *
 * @dev: Bus whose bridge window registers should be finalised
 */
void pciauto_postscan_setup_bridge(struct pci_bus *dev);

#endif