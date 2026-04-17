/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * BCM2711 (Broadcom Stingray) PCIe host controller interface.
 *
 * Provides the low-level init/teardown routines, direct ECAM config-space
 * accessors, MSI aggregation interrupt enable/disable, and a device-tree
 * helper for inbound DMA window discovery.
 *
 * These functions are called by the pcie library init path and by the generic
 * pci_io.c config accessors; they should not be called directly from driver
 * code.
 */

#ifndef _PCI_BRCMSTB_H
#define _PCI_BRCMSTB_H

#include <exec/types.h>
#include <pci_types.h>

/**
 * brcm_pcie_probe() - Initialise the BCM2711 PCIe host controller
 *
 * Resets the controller, programs the inbound and outbound address
 * translation windows from the device tree, waits for link-up (with retry),
 * and performs the Gen2/3 speed negotiation.  On success the controller is
 * ready for config-space access and bus enumeration.
 *
 * @pcie:            Controller state structure to populate
 * @bus_number_base: Root bus number to assign (normally 0)
 * Return: 0 on success, negative on link timeout or device-tree error
 */
s32 brcm_pcie_probe(struct pci_controller *pcie, u32 bus_number_base);

/**
 * brcm_pcie_remove() - Tear down the BCM2711 PCIe host controller
 *
 * Disables MSI if active, asserts PERST#, and clears the controller's
 * interrupt and address-translation configuration.  Safe to call if probe
 * failed partway through.
 *
 * @pcie: Controller to shut down
 * Return: 0 on success, negative on error
 */
s32 brcm_pcie_remove(struct pci_controller *pcie);

/**
 * brcm_pcie_read_config() - Read a config-space register via ECAM
 *
 * Performs a direct ECAM read for the device at @bdf.  Bus 0 (the root
 * complex) is accessed via the PCIE_EXT_CFG window; all other buses use
 * the standard ECAM aperture.  The value is masked to the requested @size
 * before being returned in *@valuep.
 *
 * @bus:    Controller to use
 * @bdf:    PCI bus/device/function — see PCI_BDF()
 * @offset: Config-space register byte offset
 * @valuep: Receives the read value
 * @size:   Access width (PCI_SIZE_8 / _16 / _32)
 * Return: 0 on success, negative on error
 */
s32 brcm_pcie_read_config(const struct pci_controller *bus, pci_dev_t bdf,
                          u32 offset, u32 *valuep,
                          enum pci_size_t size);

/**
 * brcm_pcie_write_config() - Write a config-space register via ECAM
 *
 * Performs a direct ECAM write for the device at @bdf.  Bus 0 is handled
 * via the PCIE_EXT_CFG window; all other buses use the ECAM aperture.
 * For sub-32-bit widths the existing 32-bit value is read, updated, and
 * written back.
 *
 * @bus:    Controller to use
 * @bdf:    PCI bus/device/function — see PCI_BDF()
 * @offset: Config-space register byte offset
 * @value:  Value to write
 * @size:   Access width (PCI_SIZE_8 / _16 / _32)
 * Return: 0 on success, negative on error
 */
s32 brcm_pcie_write_config(struct pci_controller *bus, pci_dev_t bdf,
                           u32 offset, u32 value,
                           enum pci_size_t size);

/**
 * bcm2711_reload_vl805_firmware() - Reload VL805 USB controller firmware
 *
 * Sends the VL805 firmware-reload mailbox message to the VideoCore.  Required
 * on BCM2711 boards where the VL805's on-chip ROM does not contain a complete
 * firmware image; must be called before the xHCI driver attempts to use the
 * controller.
 *
 * Return: 0 on success, negative if the mailbox call failed
 */
s32 bcm2711_reload_vl805_firmware(void);

/**
 * brcm_pcie_enable_msi() - Enable the BCM2711 MSI aggregation interrupt
 *
 * Configures the MSI target address and enables the root-complex MSI
 * aggregation interrupt via gic400.library.  Must be called before any
 * device driver calls add_int_server() to request MSI delivery.
 *
 * @pcie: Controller to configure
 * Return: 0 on success, negative on error
 */
s32 brcm_pcie_enable_msi(struct pci_controller *pcie);

/**
 * brcm_pcie_disable_msi() - Disable the BCM2711 MSI aggregation interrupt
 *
 * Removes the MSI aggregation ISR from gic400.library and clears the
 * controller MSI enable register.  Safe to call if MSI was never enabled.
 *
 * @pcie: Controller whose MSI should be disabled
 */
void brcm_pcie_disable_msi(struct pci_controller *pcie);

/**
 * pci_get_devtree_dma_regions() - Read an inbound DMA window from the device tree
 *
 * Parses the "dma-ranges" property of the PCIe controller node to obtain
 * the @index-th inbound window and fills @memp with its bus_start, phys_start,
 * and size.  Used during probe to configure the controller's inbound address
 * translation registers.
 *
 * @ctlr:  Controller whose device-tree node should be queried
 * @memp:  Region structure to fill with the discovered window
 * @index: Zero-based index of the dma-ranges entry to read
 * Return: 0 on success, negative if the property is missing or @index is
 *         out of range
 */
s32 pci_get_devtree_dma_regions(struct pci_controller *ctlr, struct pci_region *memp, u32 index);

#endif /* _PCI_BRCMSTB_H */