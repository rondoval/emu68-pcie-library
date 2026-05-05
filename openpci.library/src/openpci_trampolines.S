| SPDX-License-Identifier: GPL-2.0-only
|
| openpci.library trampolines.
|
| Each stub loads bcmBase from offset 38 in the OpenPCIBase (a6) and
| jumps (not calls) to the matching LVO slot in bcmpcie.library.
| Register parameters are not touched; bcmpcie RTS returns directly to
| the original caller.
|
| BCM_OFFSET = 38
|   struct Library libNode = 34 bytes
|   ULONG         segList  =  4 bytes  (offset 34)
|   struct Library *bcmBase            (offset 38)
|
| Symbol names carry a leading underscore (AmigaOS/COFF convention).

    .text

.macro FORWARD cname, lvo
    .global _\cname
_\cname:
    movea.l  38(a6),a6
    jmp      \lvo(a6)
.endm

| LVO  -30 .. -192: original openpci slots
FORWARD ShimPCIBus,              -30
FORWARD ShimPCIInb,              -36
FORWARD ShimPCIOutb,             -42
FORWARD ShimPCIInw,              -48
FORWARD ShimPCIOutw,             -54
FORWARD ShimPCIInl,              -60
FORWARD ShimPCIOutl,             -66
FORWARD ShimPCIToHostCpy,        -72
FORWARD ShimHostToPCICpy,        -78
FORWARD ShimPCIToPCICpy,         -84
FORWARD ShimFindDevice,          -90
FORWARD ShimFindClass,           -96
FORWARD ShimFindSlot,           -102
FORWARD ShimReadConfigByte,     -108
FORWARD ShimReadConfigWord,     -114
FORWARD ShimReadConfigLong,     -120
FORWARD ShimWriteConfigByte,    -126
FORWARD ShimWriteConfigWord,    -132
FORWARD ShimWriteConfigLong,    -138
FORWARD ShimSetMaster,          -144
FORWARD ShimAddIntServer,       -150
FORWARD ShimRemIntServer,       -156
FORWARD ShimAllocDMAMem,        -162
FORWARD ShimFreeDMAMem,         -168
FORWARD ShimLogicToPhysic,      -174
FORWARD ShimPhysicToLogic,      -180
FORWARD ShimObtainCard,         -186
FORWARD ShimReleaseCard,        -192

| LVO -198: private slot in bcmpcie — not forwarded (LibNull handles it)

| LVO -204 .. -252: openpci v3 extensions
FORWARD ShimFindBoardA,         -204
FORWARD ShimGetBoardAttrsA,     -210
FORWARD ShimSetBoardAttrsA,     -216
FORWARD ShimAllocDMAForBoard,   -222
FORWARD ShimReleaseDMAForBoard, -228
FORWARD ShimAddMemHandler,      -234
FORWARD ShimRemMemHandler,      -240
FORWARD ShimObtainPCIRegion,    -246
FORWARD ShimReleasePCIRegion,   -252
