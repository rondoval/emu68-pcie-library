/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCI interrupts: INTx, MSI and MSI-X.
 *
 * Layers, top down:
 *   bcmpcie.library/src/pcie_irq.c   the LVOs: argument checks, semaphore, error codes
 *   pci_irq.c                        choice of type, demux-slot pool, per-vector dispatch
 *   pci_int.c, pcie_msi.c,           one interrupt type each: what the device
 *   pcie_msix.c                      offers (probe) and how it is programmed
 *   pcie_brcmstb_msi.c               the controller: gic400 and the MSI demux
 *                                    (pcie_brcmstb.h)
 *
 * Vocabulary:
 *   vec      a device's vector index, 0..dev->active.nvec-1
 *   slot     a bit of the root complex's MSI demux (dev->active.slots[vec]);
 *            MSI and MSI-X only, an opaque token outside the back-end
 *   gic_irq  a gic400 interrupt number
 *   server   the Exec struct Interrupt run for a vector
 *   mask / unmask   the runtime gate (MaskIntVector/UnmaskIntVector)
 *   open / close    the device-level gate (MSI-X table entry, MSI mask bit),
 *                   moved only when a server is added or removed
 *
 * A device's interrupts, in order:
 *   probe    pci_assign_irq, pci_msi_init, pci_msix_init: route the INTx pin,
 *            locate and decode the MSI and MSI-X capabilities, leave both off
 *   alloc    pci_irq_vectors_alloc: pick the type, reserve slots, program the
 *            device; every vector closed
 *   add      pci_irq_add_server: bind the server (its slot unmasked), open the
 *            vector
 *   run      pci_irq_vec_mask / pci_irq_vec_unmask
 *   remove   pci_irq_rem_server: close the vector, unbind (slot masked)
 *   free     pci_irq_vectors_free: remove what is still attached, switch the
 *            type off at the device, release the slots
 *
 * One server per VECTOR, possibly several devices per LINE.  A vector is
 * device-private - an MSI/MSI-X message or one device's INTx pin - so it takes
 * exactly one server.  A GIC line is not: the pin swizzle maps the whole device
 * tree onto four INTx lines, so two functions can land on one.  gic400 takes one
 * server per IRQ today and refuses the second device (-EBUSY); when it chains
 * servers that stops being true and every server on the line is called for every
 * interrupt.  A server must then return "not handled" unless its own device
 * raised the interrupt - see interrupt-chaining.md in the gic400 component.
 * Neither way needs a pending guard in the INTx mask: it is per device.
 *
 * Callers serialise alloc, free, add and remove (the LVOs hold the library
 * semaphore).  Mask and unmask take no lock; see them for the context rules.
 */

#ifndef _PCI_IRQ_H
#define _PCI_IRQ_H

#include <pci_types.h>
#include <exec/interrupts.h>

/* ---- probe: what the device offers (pciauto_setup_device, BARs mapped) ---- */

/**
 * pci_assign_irq() - route the device's INTx pin to its GIC IRQ.
 * Swizzles PCI_INTERRUPT_PIN through every bridge up to the root bus and looks
 * the result up in the controller's INT_x_mapping[].  Fills dev->intx;
 * intx.gic_irq stays 0 (no INTx) without a pin or a mapping.
 */
void pci_assign_irq(struct pci_device *dev);

/**
 * pci_msi_init() - locate and decode the MSI capability, MSI off.
 * Fills dev->msi (64-bit address, per-vector mask bits, vector count);
 * msi.cap_offset stays 0 without the capability.
 */
void pci_msi_init(struct pci_device *dev);

/**
 * pci_msix_init() - locate the MSI-X capability and resolve its table, MSI-X off.
 * Fills dev->msix; msix.table_virt stays NULL without the capability or when
 * the table is not inside a mapped memory BAR - MSI-X is then unusable.
 */
void pci_msix_init(struct pci_device *dev);

/* ---- demux-slot pool: MSI_MAX_VECTORS opaque tokens ---- */

/**
 * pci_irq_slots_alloc_any() - reserve up to @n arbitrary free slots into @out
 * (MSI-X: no alignment).  @return the number reserved, 0..@n.
 */
u32 pci_irq_slots_alloc_any(struct pci_controller *pcie, u32 n, s32 *out);

/**
 * pci_irq_slots_alloc_aligned() - reserve a 2^k-aligned contiguous block of @n
 * slots (@n a power of two), as multi-message MSI requires.
 * @return the base slot, or -1 if no aligned block is free.
 */
s32 pci_irq_slots_alloc_aligned(struct pci_controller *pcie, u32 n);

/** pci_irq_slots_free() - release the @n slots listed in @slots. */
void pci_irq_slots_free(struct pci_controller *pcie, const s32 *slots, u32 n);

/* ---- allocation and teardown ---- */

/**
 * pci_irq_vectors_alloc() - reserve [@min,@max] vectors of the best allowed type.
 * @flags is a mask of PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX; the types are
 * tried MSI-X, MSI, INTx.  Records the result in dev->active.
 * @return the vector count (>= @min), or negative errno: -EBUSY the device
 * already has vectors, -ERANGE bad range, else why the last type tried failed
 * (-ENODEV not available on this device, -ENOSPC out of demux slots).
 */
s32 pci_irq_vectors_alloc(struct pci_device *dev, u32 min, u32 max, u32 flags);

/**
 * pci_irq_vectors_free() - tear down the device's allocation, whatever its type.
 * Removes every server still attached first.  Does nothing without an
 * allocation.
 */
void pci_irq_vectors_free(struct pci_device *dev);

/* ---- servers ---- */

/**
 * pci_irq_add_server() - attach @isr to vector @vec and let it fire.
 * MSI and MSI-X bind @isr to the vector's demux slot and open the vector at
 * the device; INTx registers @isr with gic400 and lets the device assert the
 * line.  A vector takes one server.
 * @return 0, or negative errno: -EINVAL bad @vec, -EBUSY the vector already has
 * a server, or gic400 refused the INTx line to a second device (it takes one
 * server per IRQ until it chains them), -EIO gic400 failed otherwise.
 */
s32 pci_irq_add_server(struct pci_device *dev, u32 vec, struct Interrupt *isr);

/**
 * pci_irq_rem_server() - detach @isr from vector @vec.
 * Quiets the device first (closes the vector; INTx: disables the line), then
 * takes the server away.  Does nothing unless @isr is the vector's server.
 */
void pci_irq_rem_server(struct pci_device *dev, u32 vec, struct Interrupt *isr);

/* ---- runtime mask ---- */

/**
 * pci_irq_vec_mask() / pci_irq_vec_unmask() - hold back / let through a vector.
 *
 * MSI and MSI-X mask the vector's demux slot at the root complex: a local
 * register write, no PCIe transaction, safe from an interrupt server.  A
 * message that arrives while masked latches and fires on unmask, so
 * mask-in-the-server / unmask-after-the-task-drained coalesces a burst into
 * one interrupt.
 * INTx sets or clears PCI_COMMAND.INTX_DISABLE.  That is a config-space
 * access: task context only.
 * @return TRUE, or FALSE for a bad @vec (no allocation included).
 */
BOOL pci_irq_vec_mask(struct pci_device *dev, u32 vec);
BOOL pci_irq_vec_unmask(struct pci_device *dev, u32 vec);

/* ---- per type, for pci_irq.c ---- */

/*
 * The allocators are called with 1 <= @min <= @max <= MSI_MAX_VECTORS.  They
 * reserve their slots into dev->active.slots[], program the device with every
 * vector closed and return the vector count, or negative errno (-ENODEV the
 * device cannot do this type, -ENOSPC out of demux slots, -ERANGE).
 * pci_irq_vectors_alloc records mode and count.  The shutdowns switch the type
 * off at the device; they are for a device with an allocation of that type.
 */

/** pci_intx() - let the device assert INTx (@enable) or not: PCI_COMMAND.INTX_DISABLE. */
void pci_intx(struct pci_device *dev, int enable);

/** pci_intx_alloc() - INTx is one vector on the device's routed pin, or -ENODEV / -ERANGE. */
s32 pci_intx_alloc(struct pci_device *dev, u32 min, u32 max);

/**
 * pci_msi_alloc() - the largest 2^k-aligned slot block within the range and
 * the device's Multiple-Message-Capable count; MSI enabled, INTx disabled.
 */
s32 pci_msi_alloc(struct pci_device *dev, u32 min, u32 max);

/** pci_msi_shutdown() - disable MSI and restore the unmasked reset state. */
void pci_msi_shutdown(struct pci_device *dev);

/**
 * pci_msi_update_mask() - clear and set bits of the per-vector mask register;
 * bit i masks vector i.  Does nothing on a device without mask bits (they are
 * optional for MSI).
 */
void pci_msi_update_mask(struct pci_device *dev, u32 clear, u32 set);

/**
 * pci_msix_alloc() - as many free slots as the range and the table size
 * allow; one table entry per vector; MSI-X enabled, INTx disabled.
 */
s32 pci_msix_alloc(struct pci_device *dev, u32 min, u32 max);

/** pci_msix_shutdown() - mask the function and disable MSI-X. */
void pci_msix_shutdown(struct pci_device *dev);

/** pci_msix_entry_mask() / pci_msix_entry_unmask() - the mask bit of table entry @vec. */
void pci_msix_entry_mask(struct pci_device *dev, u32 vec);
void pci_msix_entry_unmask(struct pci_device *dev, u32 vec);

#endif /* _PCI_IRQ_H */
