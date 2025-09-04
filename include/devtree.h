#ifndef DEV_TREE_H
#define DEV_TREE_H

#ifdef __INTELLISENSE__
#include <clib/devicetree_protos.h>
#else
#include <proto/devicetree.h>
#endif

extern APTR DeviceTreeBase;

uint64_t DT_GetNumber(ULONG *ptr, ULONG cells);
ULONG DT_GetPropertyValueULONG(APTR key, const char *propname, ULONG def_val, BOOL check_parent);
ULONG DT_GetAddressTranslationOffset(APTR address);
APTR DT_GetBaseAddress(CONST_STRPTR alias);
CONST_STRPTR DT_GetAlias(const char *alias);
int DT_Init();

#endif // DEV_TREE_H