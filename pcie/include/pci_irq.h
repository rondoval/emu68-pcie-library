/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic, controller-agnostic interrupt-vector management.
 *
 * Two responsibilities, both free of controller- and message-type specifics:
 *   - the demux-slot pool: a bitmap of MSI_MAX_VECTORS opaque "slots" (tokens)
 *     that the per-type allocators (pcie_msi.c / pcie_msix.c) reserve;
 *   - type-agnostic dispatch over dev->active.mode: install/uninstall an ISR on
 *     a vector, per-vector mask/unmask, and free the whole allocation.
 *
 * A slot is just an integer here; its hardware meaning (the MSI message and the
 * demux ISR) belongs to the controller back-end (pcie_brcmstb_msi.c), reached
 * via brcm_msi_bind()/brcm_msi_unbind().  INTx is a first-class type too: the
 * dispatch routes it to brcm_intx_bind()/brcm_intx_unbind() (gic400) plus the
 * generic config bits in pci_int.c.  Callers must serialise
 * alloc/free/install/uninstall (the shim holds its semaphore); mask/unmask are
 * register-only and ISR-safe.
 */

#ifndef _PCI_IRQ_H
#define _PCI_IRQ_H

#include <pci_types.h>
#include <exec/interrupts.h>

/* ---- demux-slot pool: MSI_MAX_VECTORS opaque tokens ---- */

/** pci_irq_slots_free_count() - number of currently free slots. */
u32 pci_irq_slots_free_count(const struct pci_controller *pcie);

/**
 * pci_irq_slots_alloc_any() - reserve @n arbitrary free slots into @out.
 * @return 0 on success, -1 if fewer than @n are free (MSI-X: no alignment).
 */
s32 pci_irq_slots_alloc_any(struct pci_controller *pcie, u32 n, s32 *out);

/**
 * pci_irq_slots_alloc_aligned() - reserve a 2^k-aligned contiguous block of @n
 * slots (@n a power of two), as multi-message MSI requires.
 * @return the base slot, or -1 if no aligned block is free.
 */
s32 pci_irq_slots_alloc_aligned(struct pci_controller *pcie, u32 n);

/** pci_irq_slots_free() - release the @n slots listed in @slots. */
void pci_irq_slots_free(struct pci_controller *pcie, const s32 *slots, u32 n);

/* ---- type-agnostic dispatch over dev->active.mode ---- */

/** pci_irq_free() - tear down the active allocation (INTx, MSI, or MSI-X). */
void pci_irq_free(struct pci_device *dev);

/**
 * pci_irq_install() - attach @isr for vector @vec and unmask it.
 * MSI/MSI-X bind into the demux slot; INTx registers with gic400 and enables
 * the device INTx line.  @return 0 on success, negative errno on failure.
 */
s32 pci_irq_install(struct pci_device *dev, u32 vec, struct Interrupt *isr);

/** pci_irq_uninstall() - mask vector @vec and detach @isr (INTx needs @isr). */
void pci_irq_uninstall(struct pci_device *dev, u32 vec, struct Interrupt *isr);

/**
 * pci_irq_mask() / pci_irq_unmask() - per-vector mask (all types), ISR-safe.
 *
 * Return: TRUE if the (un)mask took effect.  For INTx (level-triggered, may be
 * shared) FALSE means the change was deferred because the interrupt-pending
 * state did not match the request - on mask, nothing was pending (not our
 * line); on unmask, an interrupt is still pending (drain and retry).  MSI-X
 * per-vector masking is mandatory and always returns TRUE.  MSI per-vector
 * masking is optional: FALSE means the device lacks the Per-Vector Masking
 * Capability, so the vector cannot be masked at the device.  Invalid args (vec
 * out of range, no active mode) return FALSE.
 */
BOOL pci_irq_mask(struct pci_device *dev, u32 vec);
BOOL pci_irq_unmask(struct pci_device *dev, u32 vec);

#endif /* _PCI_IRQ_H */
