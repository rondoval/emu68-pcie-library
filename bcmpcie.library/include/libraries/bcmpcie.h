/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * bcmpcie.library's public constants.  proto/bcmpcie.h includes this header.
 */

#ifndef LIBRARIES_BCMPCIE_H
#define LIBRARIES_BCMPCIE_H

/*
 * Interrupt types: the flags AllocIntVectors() takes and the value
 * GetIntVectorType() returns.  A type is attempted only when its bit is set,
 * in priority order MSI-X -> MSI -> INTx.  Omit a bit to forbid that type
 * (e.g. drop PCI_IRQ_MSIX to disable MSI-X).
 */
#define PCI_IRQ_INTX      (1UL << 0) /* legacy INTx pin */
#define PCI_IRQ_MSI       (1UL << 1) /* message-signalled interrupts */
#define PCI_IRQ_MSIX      (1UL << 2) /* extended message-signalled interrupts */
#define PCI_IRQ_ALL_TYPES (PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX)

#endif /* LIBRARIES_BCMPCIE_H */
