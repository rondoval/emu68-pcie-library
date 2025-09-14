// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/devicetree_protos.h>
#include <clib/utility_protos.h>
#else
#include <proto/exec.h>
#include <proto/devicetree.h>
#include <proto/utility.h>
#endif

#include <exec/types.h>

#include <debug.h>
#include <devtree.h>

APTR DeviceTreeBase;

uint64_t DT_GetNumber(const ULONG *ptr, ULONG cells)
{
	uint64_t value = 0;

	while (cells--)
	{
		value = (value << 32) | *ptr++;
	}
	return value;
}

ULONG DT_GetPropertyValueULONG(APTR key, const char *propname, ULONG def_val, BOOL check_parent)
{
	ULONG ret = def_val;

	while (key != NULL)
	{
		APTR p = DT_FindProperty(key, (CONST_STRPTR)propname);

		if (p != NULL || check_parent == FALSE)
		{
			if (p != NULL || DT_GetPropLen(p) >= 4)
			{
				ret = *(ULONG *)DT_GetPropValue(p);
			}

			return ret;
		}
		key = DT_GetParent(key);
	}
	return ret;
}

WORD DT_TranslateAddress(APTR *address, APTR node)
{
	const ULONG *ranges = DT_GetPropValue(DT_FindProperty(node, (CONST_STRPTR) "ranges"));
	const ULONG len = DT_GetPropLen(DT_FindProperty(node, (CONST_STRPTR) "ranges"));

	const ULONG address_cells_parent = DT_GetPropertyValueULONG(DT_GetParent(node), "#address-cells", 2, FALSE);
	const ULONG address_cells_child = DT_GetPropertyValueULONG(node, "#address-cells", 2, FALSE);
	const ULONG size_cells = DT_GetPropertyValueULONG(node, "#size-cells", 2, FALSE);
	const ULONG cells_per_record = address_cells_parent + address_cells_child + size_cells;

	for (const ULONG *i = ranges; i < ranges + len / 4; i += cells_per_record)
	{
		ULONG phys_vc4 = DT_GetNumber(i, address_cells_child);
		ULONG phys_cpu = DT_GetNumber(i + address_cells_child, address_cells_parent);
		ULONG size = DT_GetNumber(i + address_cells_child + address_cells_parent, size_cells);
		KprintfH("[devtree] %s: phys_vc4=0x%08lx phys_cpu=0x%08lx size=0x%08lx\n", __func__, phys_vc4, phys_cpu, size);

		if ((ULONG)*address >= phys_vc4 && (ULONG)*address < phys_vc4 + size)
		{
			ULONG offset = phys_cpu - phys_vc4;
			*address += offset;
			KprintfH("[devtree] %s: Virtual address=0x%08lx\n", __func__, *address);
			return 0;
		}
	}
	Kprintf("[devtree] %s: No translation found for address 0x%08lx\n", __func__, address);
	return -1;
}

APTR DT_GetBaseAddressVirtual(CONST_STRPTR alias)
{
	APTR key = DT_OpenKey(alias);
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key %s\n", __func__, alias);
		return NULL;
	}

	const APTR parent = DT_GetParent(key);
	const ULONG address_cells_parent = DT_GetPropertyValueULONG(parent, "#address-cells", 2, FALSE);
	APTR address = (APTR)(ULONG)DT_GetNumber(DT_GetPropValue(DT_FindProperty(key, (CONST_STRPTR) "reg")), address_cells_parent);
	DT_TranslateAddress(&address, parent);
	DT_CloseKey(key);

	return address;
}

APTR DT_GetBaseAddress(CONST_STRPTR alias)
{
	APTR key = DT_OpenKey(alias);
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key %s\n", __func__, alias);
		return NULL;
	}

	ULONG address_cells = DT_GetPropertyValueULONG(DT_GetParent(key), "#address-cells", 2, FALSE);

	const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, (CONST_STRPTR) "reg"));
	if (reg != NULL)
	{
		DT_CloseKey(key);
		return (APTR)reg[address_cells - 1];
	}
	Kprintf("[devtree] %s: Failed to find reg property in key %s\n", __func__, alias);
	DT_CloseKey(key);
	return NULL;
}

CONST_STRPTR DT_GetAlias(CONST_STRPTR alias)
{
	APTR key = DT_OpenKey((CONST_STRPTR) "/aliases");
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key /aliases\n", __func__);
		return NULL;
	}

	APTR prop = DT_FindProperty(key, (CONST_STRPTR)alias);
	if (prop != NULL)
	{
		CONST_STRPTR value = DT_GetPropValue(prop);
		DT_CloseKey(key);
		return value;
	}
	Kprintf("[devtree] %s: Failed to find alias %s\n", __func__, alias);
	DT_CloseKey(key);
	return NULL;
}

int DT_Init()
{
	DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	if (!DeviceTreeBase)
	{
		Kprintf("[devtree] %s: Failed to open devicetree.resource\n", __func__);
		return -1;
	}
	return 0;
}