/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LIBRARIES_BCMPCIE_ERRORS_H
#define LIBRARIES_BCMPCIE_ERRORS_H

#include <exec/types.h>

/*
 * Error codes returned by the LONG-returning bcmpcie.library calls
 * (AllocIntVectors, AddIntVectorServer, EnableMSI, FLR).
 *
 * Success is PCIE_OK (0) or, for AllocIntVectors, a positive vector count.
 * Every failure is negative, so `result < 0` reliably detects an error and
 * the existing `< 1` / `!= 0` caller checks keep working unchanged.
 */
enum pcie_error {
    PCIE_OK          =  0,  /* success */
    PCIE_ERR_INVAL   = -1,  /* NULL/invalid argument or bad vector index */
    PCIE_ERR_BUSY    = -2,  /* resource already in use (vectors/card) */
    PCIE_ERR_NODEV   = -3,  /* requested capability/resource not present */
    PCIE_ERR_NOTSUPP = -4,  /* operation unsupported (no FLR, MSI demux off, no gic400/INTx) */
    PCIE_ERR_NOMEM   = -5,  /* allocation failed / no free interrupt vectors */
    PCIE_ERR_IO      = -6,  /* low-level hardware/install operation failed */
};

/* Convenience for callers' debug logging; header-only, no library entry point. */
static inline const char *pcie_strerror(LONG err)
{
    switch (err) {
    case PCIE_OK:          return "ok";
    case PCIE_ERR_INVAL:   return "invalid argument";
    case PCIE_ERR_BUSY:    return "resource busy";
    case PCIE_ERR_NODEV:   return "no such device/capability";
    case PCIE_ERR_NOTSUPP: return "not supported";
    case PCIE_ERR_NOMEM:   return "out of memory/vectors";
    case PCIE_ERR_IO:      return "I/O error";
    default:               return (err > 0) ? "ok" : "unknown error";
    }
}

#endif /* LIBRARIES_BCMPCIE_ERRORS_H */
