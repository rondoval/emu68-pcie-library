/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * Shared bcmpcie interrupt-vector attach/detach helpers.
 * Include AFTER proto/bcmpcie.h with BCMPCIE_BASE_NAME
 * defined as `pcielibBase` — the parameter of these inlines is what the
 * proto macros expand against.  Error logging stays with the caller (it has
 * pcie_strerror and its own prefix); a negative return is the failing
 * bcmpcie result.
 */

#ifndef LIBRARIES_PCI_IRQ_H
#define LIBRARIES_PCI_IRQ_H

#include <exec/types.h>
#include <exec/interrupts.h>

struct pci_dev;

/* Allocate one interrupt vector from allowed_flags (PCI_IRQ_*), report the
 * granted type through *itype_out (optional), and add isr as its server.
 * Returns 0, or the failing AllocIntVectors/AddIntVectorServer result. */
static inline LONG pci_irq_attach(struct Library *pcielibBase, struct pci_dev *pd,
                                  struct Interrupt *isr, ULONG allowed_flags,
                                  ULONG *itype_out)
{
	LONG nvec = AllocIntVectors(pd, 1, 1, allowed_flags);
	if (nvec < 1)
		return nvec;

	if (itype_out)
		*itype_out = GetIntVectorType(pd);

	LONG rc = AddIntVectorServer(pd, 0, isr);
	if (rc != 0)
	{
		FreeIntVectors(pd);
		return rc;
	}

	return 0;
}

static inline void pci_irq_detach(struct Library *pcielibBase, struct pci_dev *pd,
                                  struct Interrupt *isr)
{
	RemIntVectorServer(pd, 0, isr);
	FreeIntVectors(pd);
}

#endif /* LIBRARIES_PCI_IRQ_H */
