// SPDX-License-Identifier: GPL-2.0-only
//
// openpci.library compatibility shim.
// Opens bcmpcie.library and forwards all original openpci API calls (LVO -30
// through -252) through naked trampolines.  The PCIe extensions (-258 and
// above) are NOT part of the original openpci ABI and are not exposed here.
//
// Fixed version: 45.12

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/memory.h>

#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#include <proto/exec.h>
#endif

/* -----------------------------------------------------------------------
 * Compile-time defaults (overridden by CMake)
 * ----------------------------------------------------------------------- */
#ifndef LIBRARY_NAME
#define LIBRARY_NAME "openpci.library"
#endif

#ifndef LIBRARY_IDSTRING
#define LIBRARY_IDSTRING "$VER: openpci.library 45.12 (17.04.2026)"
#endif

#ifndef LIBRARY_VERSION
#define LIBRARY_VERSION 45
#endif

#ifndef LIBRARY_REVISION
#define LIBRARY_REVISION 12
#endif

#ifndef LIBRARY_PRIORITY
#define LIBRARY_PRIORITY 0
#endif

/* -----------------------------------------------------------------------
 * Library base
 *
 * struct Library  = 34 bytes (Node=14, Flags=1, Pad=1, NegSize=2,
 *                             PosSize=2, Version=2, Revision=2,
 *                             IdString=4, Sum=4, OpenCnt=2)
 * segList         = 4 bytes at offset 34
 * bcmBase         = 4 bytes at offset 38  ← BCM_OFFSET used in trampolines
 * ----------------------------------------------------------------------- */
struct OpenPCIBase {
    struct Library   libNode;
    ULONG            segList;
    struct Library  *bcmBase;
};

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
LONG __attribute__((used, no_reorder)) doNotExecute(void);
extern const UBYTE endOfCode;

static const char libraryName[]     = LIBRARY_NAME;
static const char libraryIdString[] = LIBRARY_IDSTRING;
static const APTR initTable[4];

/* -----------------------------------------------------------------------
 * Resident tag
 * ----------------------------------------------------------------------- */
const struct Resident openpciResident __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&openpciResident,
    (APTR)&endOfCode,
    RTF_AUTOINIT,
    LIBRARY_VERSION,
    NT_LIBRARY,
    LIBRARY_PRIORITY,
    (APTR)&libraryName,
    (APTR)&libraryIdString,
    (APTR)initTable,
};

/* -----------------------------------------------------------------------
 * Entry point — returns -1 if the binary is executed directly
 * ----------------------------------------------------------------------- */
LONG __attribute__((used, no_reorder)) doNotExecute(void)
{
    return -1;
}

/* -----------------------------------------------------------------------
 * Standard library vectors
 * ----------------------------------------------------------------------- */
static ULONG LibExpunge(struct OpenPCIBase *base asm("a6"))
{
    ULONG segList = base->segList;
    if (base->libNode.lib_OpenCnt > 0)
    {
        base->libNode.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    Forbid();
    Remove((struct Node *)base);
    Permit();
    ULONG size = (ULONG)base->libNode.lib_NegSize + base->libNode.lib_PosSize;
    FreeMem((APTR)((ULONG)base - base->libNode.lib_NegSize), size);
    return segList;
}

static ULONG LibClose(struct OpenPCIBase *base asm("a6"))
{
    base->libNode.lib_OpenCnt--;
    if (base->libNode.lib_OpenCnt == 0)
    {
        CloseLibrary(base->bcmBase);
        base->bcmBase = NULL;
        if (base->libNode.lib_Flags & LIBF_DELEXP)
            return LibExpunge(base);
    }
    return 0;
}

static struct OpenPCIBase *LibOpen(ULONG version asm("d0"), struct OpenPCIBase *base asm("a6"))
{
    (void)version;
    if (!base->bcmBase)
    {
        base->bcmBase = OpenLibrary((CONST_STRPTR)"bcmpcie.library", 1);
        if (!base->bcmBase)
            return NULL;
    }
    base->libNode.lib_OpenCnt++;
    base->libNode.lib_Flags &= (UBYTE)~LIBF_DELEXP;
    return base;
}

static ULONG LibNull(void)
{
    return 0;
}

static struct Library *LibInit(struct Library *libBase asm("d0"), ULONG seglist asm("a0"),
                                struct ExecBase *execBase asm("a6"))
{
    struct OpenPCIBase *base = (struct OpenPCIBase *)libBase;
    (void)execBase;

    base->segList                = seglist;
    base->libNode.lib_Revision   = (UWORD)LIBRARY_REVISION;
    base->bcmBase                = NULL;

    return libBase;
}

/* -----------------------------------------------------------------------
 * Trampolines — implemented in openpci_trampolines.S.  Each naked stub
 * loads bcmBase from offset 38 in the OpenPCIBase (a6) and jumps to the
 * matching LVO in bcmpcie.library.
 * ----------------------------------------------------------------------- */
extern void ShimPCIBus(void);
extern void ShimPCIInb(void);
extern void ShimPCIOutb(void);
extern void ShimPCIInw(void);
extern void ShimPCIOutw(void);
extern void ShimPCIInl(void);
extern void ShimPCIOutl(void);
extern void ShimPCIToHostCpy(void);
extern void ShimHostToPCICpy(void);
extern void ShimPCIToPCICpy(void);
extern void ShimFindDevice(void);
extern void ShimFindClass(void);
extern void ShimFindSlot(void);
extern void ShimReadConfigByte(void);
extern void ShimReadConfigWord(void);
extern void ShimReadConfigLong(void);
extern void ShimWriteConfigByte(void);
extern void ShimWriteConfigWord(void);
extern void ShimWriteConfigLong(void);
extern void ShimSetMaster(void);
extern void ShimAddIntServer(void);
extern void ShimRemIntServer(void);
extern void ShimAllocDMAMem(void);
extern void ShimFreeDMAMem(void);
extern void ShimLogicToPhysic(void);
extern void ShimPhysicToLogic(void);
extern void ShimObtainCard(void);
extern void ShimReleaseCard(void);
extern void ShimFindBoardA(void);
extern void ShimGetBoardAttrsA(void);
extern void ShimSetBoardAttrsA(void);
extern void ShimAllocDMAForBoard(void);
extern void ShimReleaseDMAForBoard(void);
extern void ShimAddMemHandler(void);
extern void ShimRemMemHandler(void);
extern void ShimObtainPCIRegion(void);
extern void ShimReleasePCIRegion(void);

/* -----------------------------------------------------------------------
 * Function table and init table
 * ----------------------------------------------------------------------- */
static const APTR funcTable[] = {
    /* Standard library vectors */
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibNull,
    /* openpci-compatible API */
    (APTR)ShimPCIBus,              /* -30  */
    (APTR)ShimPCIInb,              /* -36  */
    (APTR)ShimPCIOutb,             /* -42  */
    (APTR)ShimPCIInw,              /* -48  */
    (APTR)ShimPCIOutw,             /* -54  */
    (APTR)ShimPCIInl,              /* -60  */
    (APTR)ShimPCIOutl,             /* -66  */
    (APTR)ShimPCIToHostCpy,        /* -72  */
    (APTR)ShimHostToPCICpy,        /* -78  */
    (APTR)ShimPCIToPCICpy,         /* -84  */
    (APTR)ShimFindDevice,          /* -90  */
    (APTR)ShimFindClass,           /* -96  */
    (APTR)ShimFindSlot,            /* -102 */
    (APTR)ShimReadConfigByte,      /* -108 */
    (APTR)ShimReadConfigWord,      /* -114 */
    (APTR)ShimReadConfigLong,      /* -120 */
    (APTR)ShimWriteConfigByte,     /* -126 */
    (APTR)ShimWriteConfigWord,     /* -132 */
    (APTR)ShimWriteConfigLong,     /* -138 */
    (APTR)ShimSetMaster,           /* -144 */
    (APTR)ShimAddIntServer,        /* -150 */
    (APTR)ShimRemIntServer,        /* -156 */
    (APTR)ShimAllocDMAMem,         /* -162 */
    (APTR)ShimFreeDMAMem,          /* -168 */
    (APTR)ShimLogicToPhysic,       /* -174 */
    (APTR)ShimPhysicToLogic,       /* -180 */
    (APTR)ShimObtainCard,          /* -186 */
    (APTR)ShimReleaseCard,         /* -192 */
    (APTR)LibNull,                 /* -198 private placeholder */
    (APTR)ShimFindBoardA,          /* -204 */
    (APTR)ShimGetBoardAttrsA,      /* -210 */
    (APTR)ShimSetBoardAttrsA,      /* -216 */
    (APTR)ShimAllocDMAForBoard,    /* -222 */
    (APTR)ShimReleaseDMAForBoard,  /* -228 */
    (APTR)ShimAddMemHandler,       /* -234 */
    (APTR)ShimRemMemHandler,       /* -240 */
    (APTR)ShimObtainPCIRegion,     /* -246 */
    (APTR)ShimReleasePCIRegion,    /* -252 */
    (APTR)-1,
};

static const APTR initTable[4] = {
    (APTR)sizeof(struct OpenPCIBase),
    (APTR)funcTable,
    NULL,
    (APTR)LibInit,
};
