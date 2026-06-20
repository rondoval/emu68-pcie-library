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
 * brcm_pcie_open_gic400() / brcm_pcie_close_gic400() - gic400.library lifetime
 *
 * Open/close the gic400.library handle the controller uses for both the MSI
 * aggregation ISR and per-device INTx registration.  Owned by
 * brcm_pcie_probe()/brcm_pcie_remove(); open is idempotent (NULL-guarded).
 *
 * @pcie: Controller whose gic400 handle to manage
 * Return (open): 0 on success, -ENODEV if the library could not be opened.
 */
s32 brcm_pcie_open_gic400(struct pci_controller *pcie);
void brcm_pcie_close_gic400(struct pci_controller *pcie);

/**
 * brcm_pcie_compose_msi_msg() - Build the MSI/MSI-X message for a demux slot
 *
 * Produces the memory-write address and data that steer a device's MSI/MSI-X
 * message onto controller demux @slot.  This is the only place that knows the
 * BCM2711 doorbell encoding; the generic MSI/MSI-X cap programmers call it
 * instead of touching controller registers.
 *
 * @pcie:     Controller whose doorbell to target
 * @slot:     Demux slot reserved for this vector (0..MSI_MAX_VECTORS-1)
 * @addr_lo:  Set to the low 32 bits of the message address
 * @addr_hi:  Set to the high 32 bits of the message address
 * @data:     Set to the 16-bit message data word
 */
void brcm_pcie_compose_msi_msg(struct pci_controller *pcie, s32 slot,
							   u32 *addr_lo, u32 *addr_hi, u16 *data);

/**
 * brcm_msi_bind() / brcm_msi_unbind() - Attach/detach an ISR to a demux slot
 *
 * Records (or clears) the Exec interrupt server the controller's MSI demux ISR
 * dispatches to when slot @slot fires.  The generic vector layer calls these;
 * the per-slot dispatch table is owned here.
 *
 * @pcie: Controller owning the demux
 * @slot: Demux slot (0..MSI_MAX_VECTORS-1)
 * @isr:  Interrupt server to dispatch to (bind only)
 */
void brcm_msi_bind(struct pci_controller *pcie, s32 slot, struct Interrupt *isr);
void brcm_msi_unbind(struct pci_controller *pcie, s32 slot);

/**
 * brcm_intx_bind() / brcm_intx_unbind() - Attach/detach a device's INTx ISR
 *
 * Registers (or removes) @isr with gic400 on the device's INTx line
 * (dev->intx.gic_line + the controller's GIC SPI base).  The INTx analog of
 * brcm_msi_bind(); the generic PCI INTx config (command-register enable, mask)
 * is handled separately by the pci_irq dispatch via pci_int.c.
 *
 * @pcie: Controller owning the gic400 handle
 * @dev:  Device whose INTx line to (un)register
 * @isr:  Interrupt server to dispatch to
 * Return (bind): 0 on success, -ENODEV if gic400 is unavailable, -EIO if
 *         registration failed.
 */
s32 brcm_intx_bind(struct pci_controller *pcie, struct pci_device *dev, struct Interrupt *isr);
void brcm_intx_unbind(struct pci_controller *pcie, struct pci_device *dev, struct Interrupt *isr);

#endif /* _PCI_BRCMSTB_H */