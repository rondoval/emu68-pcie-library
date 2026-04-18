// Standalone PCIe device listing tool using pcie.library
// SPDX-License-Identifier: GPL-2.0-only
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/bcmpcie_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bcmpcie.h>
#endif

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <utility/tagitem.h>
#include <dos/rdargs.h>

/* PCI capability IDs, EA fields, extended cap IDs */
#include <libraries/pci_constants.h>
#include <libraries/pcitags.h>

#ifndef PCI_HEADER_TYPE_MASK
#define PCI_HEADER_TYPE_MASK 0x7f
#endif
#ifndef PCI_HEADER_TYPE_MULTI
#define PCI_HEADER_TYPE_MULTI 0x80
#endif

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Library *BCMPCIEBase;

static BOOL quiet_mode = FALSE;

static void dump_vl805_registers(struct pci_dev *dev);
static CONST_STRPTR pci_header_type_name(u8 header_type);
static void dump_bridge_config(struct pci_dev *dev);
static void list_bars(struct pci_dev *dev);
static void print_device_machine(struct pci_dev *dev);

static void list_bars(struct pci_dev *dev)
{
    ULONG addr0 = 0, addr1 = 0, addr2 = 0, addr3 = 0, addr4 = 0, addr5 = 0;
    ULONG size0 = 0, size1 = 0, size2 = 0, size3 = 0, size4 = 0, size5 = 0;
    struct TagItem tags[] = {
        {PRM_MemoryAddr0, (ULONG)&addr0},
        {PRM_MemoryAddr1, (ULONG)&addr1},
        {PRM_MemoryAddr2, (ULONG)&addr2},
        {PRM_MemoryAddr3, (ULONG)&addr3},
        {PRM_MemoryAddr4, (ULONG)&addr4},
        {PRM_MemoryAddr5, (ULONG)&addr5},
        {PRM_MemorySize0, (ULONG)&size0},
        {PRM_MemorySize1, (ULONG)&size1},
        {PRM_MemorySize2, (ULONG)&size2},
        {PRM_MemorySize3, (ULONG)&size3},
        {PRM_MemorySize4, (ULONG)&size4},
        {PRM_MemorySize5, (ULONG)&size5},
        {TAG_DONE, 0}
    };
    GetBoardAttrsA(dev, tags);
    ULONG sz0 = size0 ? (~size0 + 1u) : 0u;
    if (addr0 != 0 || sz0 != 0)
        Printf((CONST_STRPTR) "  BAR0: addr=0x%08lx size=0x%08lx\n", addr0, sz0);
    ULONG sz1 = size1 ? (~size1 + 1u) : 0u;
    if (addr1 != 0 || sz1 != 0)
        Printf((CONST_STRPTR) "  BAR1: addr=0x%08lx size=0x%08lx\n", addr1, sz1);
    ULONG sz2 = size2 ? (~size2 + 1u) : 0u;
    if (addr2 != 0 || sz2 != 0)
        Printf((CONST_STRPTR) "  BAR2: addr=0x%08lx size=0x%08lx\n", addr2, sz2);
    ULONG sz3 = size3 ? (~size3 + 1u) : 0u;
    if (addr3 != 0 || sz3 != 0)
        Printf((CONST_STRPTR) "  BAR3: addr=0x%08lx size=0x%08lx\n", addr3, sz3);
    ULONG sz4 = size4 ? (~size4 + 1u) : 0u;
    if (addr4 != 0 || sz4 != 0)
        Printf((CONST_STRPTR) "  BAR4: addr=0x%08lx size=0x%08lx\n", addr4, sz4);
    ULONG sz5 = size5 ? (~size5 + 1u) : 0u;
    if (addr5 != 0 || sz5 != 0)
        Printf((CONST_STRPTR) "  BAR5: addr=0x%08lx size=0x%08lx\n", addr5, sz5);
}

static void print_device(struct pci_dev *dev)
{
    ULONG bus_num = 0;
    ULONG owner_ptr = 0;
    struct TagItem tags[] = {
        {PRM_BusNumber,  (ULONG)&bus_num},
        {PRM_BoardOwner, (ULONG)&owner_ptr},
        {TAG_DONE, 0}
    };
    GetBoardAttrsA(dev, tags);
    Printf((CONST_STRPTR) "Bus %ld Dev %ld Func %ld: Vendor %04lx Device %04lx Class %02lx:%02lx:%02lx\n",
           bus_num,
           (ULONG)((dev->devfn >> 3) & 0x1f),
           (ULONG)(dev->devfn & 0x07),
           (ULONG)dev->vendor, (ULONG)dev->device,
           (ULONG)((dev->devclass >> 16) & 0xff),
           (ULONG)((dev->devclass >> 8) & 0xff),
           (ULONG)(dev->devclass & 0xff));
    if (owner_ptr != 0) {
        CONST_STRPTR owner_name = (CONST_STRPTR)((struct Task *)owner_ptr)->tc_Node.ln_Name;
        Printf((CONST_STRPTR) "  Owner: %s\n", (ULONG)owner_name);
    }
    list_bars(dev);
}

static CONST_STRPTR pci_capability_name(u8 cap_id)
{
    switch (cap_id)
    {
    case PCI_CAP_ID_PM:
        return (CONST_STRPTR) "Power Management";
    case PCI_CAP_ID_AGP:
        return (CONST_STRPTR) "AGP";
    case PCI_CAP_ID_VPD:
        return (CONST_STRPTR) "VPD";
    case PCI_CAP_ID_SLOTID:
        return (CONST_STRPTR) "Slot Identification";
    case PCI_CAP_ID_MSI:
        return (CONST_STRPTR) "MSI";
    case PCI_CAP_ID_CHSWP:
        return (CONST_STRPTR) "Hot Swap";
    case PCI_CAP_ID_PCIX:
        return (CONST_STRPTR) "PCI-X";
    case PCI_CAP_ID_HT:
        return (CONST_STRPTR) "HyperTransport";
    case PCI_CAP_ID_VNDR:
        return (CONST_STRPTR) "Vendor-Specific";
    case PCI_CAP_ID_DBG:
        return (CONST_STRPTR) "Debug";
    case PCI_CAP_ID_SHPC:
        return (CONST_STRPTR) "Hot-Plug";
    case PCI_CAP_ID_SSVID:
        return (CONST_STRPTR) "Subsystem ID";
    case PCI_CAP_ID_EXP:
        return (CONST_STRPTR) "PCI Express";
    case PCI_CAP_ID_MSIX:
        return (CONST_STRPTR) "MSI-X";
    case PCI_CAP_ID_SATA:
        return (CONST_STRPTR) "SATA";
    case PCI_CAP_ID_AF:
        return (CONST_STRPTR) "Advanced Features";
    case PCI_CAP_ID_EA:
        return (CONST_STRPTR) "Enhanced Allocation";
    default:
        return (CONST_STRPTR) "Unknown";
    }
}

static CONST_STRPTR pcie_ext_capability_name(u16 cap_id)
{
    switch (cap_id)
    {
    case PCI_EXT_CAP_ID_ERR:
        return (CONST_STRPTR) "Advanced Error Reporting";
    case PCI_EXT_CAP_ID_VC:
        return (CONST_STRPTR) "Virtual Channel";
    case PCI_EXT_CAP_ID_DSN:
        return (CONST_STRPTR) "Device Serial Number";
    case PCI_EXT_CAP_ID_PWR:
        return (CONST_STRPTR) "Power Budgeting";
    case PCI_EXT_CAP_ID_RCLD:
        return (CONST_STRPTR) "RC Link Declaration";
    case PCI_EXT_CAP_ID_RCILC:
        return (CONST_STRPTR) "RC Internal Link";
    case PCI_EXT_CAP_ID_RCEC:
        return (CONST_STRPTR) "RC Event Collector";
    case PCI_EXT_CAP_ID_MFVC:
        return (CONST_STRPTR) "Multi-Function VC";
    case PCI_EXT_CAP_ID_ACS:
        return (CONST_STRPTR) "Access Control Services";
    case PCI_EXT_CAP_ID_ARI:
        return (CONST_STRPTR) "Alternate Routing ID";
    case PCI_EXT_CAP_ID_ATS:
        return (CONST_STRPTR) "Address Translation Services";
    case PCI_EXT_CAP_ID_SRIOV:
        return (CONST_STRPTR) "SR-IOV";
    case PCI_EXT_CAP_ID_MRIOV:
        return (CONST_STRPTR) "MR-IOV";
    case PCI_EXT_CAP_ID_REBAR:
        return (CONST_STRPTR) "Resizable BAR";
    case PCI_EXT_CAP_ID_DPA:
        return (CONST_STRPTR) "Dynamic Power Allocation";
    case PCI_EXT_CAP_ID_TPH:
        return (CONST_STRPTR) "TPH Requester";
    case PCI_EXT_CAP_ID_LTR:
        return (CONST_STRPTR) "Latency Tolerance Reporting";
    case PCI_EXT_CAP_ID_PASID:
        return (CONST_STRPTR) "PASID";
    case PCI_EXT_CAP_ID_PRI:
        return (CONST_STRPTR) "Page Request Interface";
    case PCI_EXT_CAP_ID_DPC:
        return (CONST_STRPTR) "Downstream Port Containment";
    case PCI_EXT_CAP_ID_L1SS:
        return (CONST_STRPTR) "L1 PM Substates";
    case PCI_EXT_CAP_ID_PTM:
        return (CONST_STRPTR) "Precision Time Measurement";
    case PCI_EXT_CAP_ID_VNDR:
        return (CONST_STRPTR) "Vendor-Specific";
    default:
        return (CONST_STRPTR) "Unknown";
    }
}

static CONST_STRPTR pci_header_type_name(u8 header_type)
{
    switch (header_type)
    {
    case PCI_HEADER_TYPE_NORMAL:
        return (CONST_STRPTR) "Endpoint";
    case PCI_HEADER_TYPE_BRIDGE:
        return (CONST_STRPTR) "PCI-to-PCI Bridge";
    case PCI_HEADER_TYPE_CARDBUS:
        return (CONST_STRPTR) "CardBus Bridge";
    default:
        return (CONST_STRPTR) "Unknown";
    }
}

static void dump_bridge_config(struct pci_dev *dev)
{
    UBYTE primary = (UBYTE)(pci_read_config_long(PCI_PRIMARY_BUS, dev) & 0xff);
    UBYTE secondary = (UBYTE)((pci_read_config_long(PCI_PRIMARY_BUS, dev) >> 8) & 0xff);
    UBYTE subordinate = (UBYTE)((pci_read_config_long(PCI_PRIMARY_BUS, dev) >> 16) & 0xff);
    UBYTE sec_latency = (UBYTE)((pci_read_config_long(PCI_PRIMARY_BUS, dev) >> 24) & 0xff);

    UBYTE io_base = pci_read_config_byte(PCI_IO_BASE, dev);
    UBYTE io_limit = pci_read_config_byte(PCI_IO_LIMIT, dev);
    UWORD io_base_upper = pci_read_config_word(PCI_IO_BASE_UPPER16, dev);
    UWORD io_limit_upper = pci_read_config_word(PCI_IO_LIMIT_UPPER16, dev);
    UWORD mem_base = pci_read_config_word(PCI_MEMORY_BASE, dev);
    UWORD mem_limit = pci_read_config_word(PCI_MEMORY_LIMIT, dev);
    UWORD pref_mem_base = pci_read_config_word(PCI_PREF_MEMORY_BASE, dev);
    UWORD pref_mem_limit = pci_read_config_word(PCI_PREF_MEMORY_LIMIT, dev);
    ULONG pref_base_upper = pci_read_config_long(PCI_PREF_BASE_UPPER32, dev);
    ULONG pref_limit_upper = pci_read_config_long(PCI_PREF_LIMIT_UPPER32, dev);
    UWORD sec_status = pci_read_config_word(PCI_SEC_STATUS, dev);
    UWORD bridge_ctrl = pci_read_config_word(PCI_BRIDGE_CONTROL, dev);

    ULONG io_base_addr = (ULONG)((io_base & 0xf0) << 8);
    ULONG io_limit_addr = (ULONG)((io_limit & 0xf0) << 8) | 0xfff;
    if ((io_base & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32)
    {
        io_base_addr |= (ULONG)io_base_upper << 16;
        io_limit_addr = ((ULONG)io_limit_upper << 16) | ((ULONG)(io_limit & 0xf0) << 8) | 0xfff;
    }

    ULONG mem_base_addr = (ULONG)((mem_base & PCI_MEMORY_RANGE_MASK) << 16);
    ULONG mem_limit_addr = (ULONG)((mem_limit & PCI_MEMORY_RANGE_MASK) << 16) | 0xfffff;

    ULONG pref_base_low = (ULONG)((pref_mem_base & PCI_PREF_RANGE_MASK) << 16);
    ULONG pref_limit_low = (ULONG)((pref_mem_limit & PCI_PREF_RANGE_MASK) << 16) | 0xfffff;
    UBYTE pref_type = (UBYTE)(pref_mem_base & PCI_PREF_RANGE_TYPE_MASK);

    Printf((CONST_STRPTR) "    Bridge bus numbers: primary=%ld secondary=%ld subordinate=%ld sec_latency=%ld\n",
           (ULONG)primary, (ULONG)secondary, (ULONG)subordinate, (ULONG)sec_latency);

    Printf((CONST_STRPTR) "    Bridge I/O range: type=%ld base=0x%08lx limit=0x%08lx\n",
           (ULONG)(io_base & PCI_IO_RANGE_TYPE_MASK), io_base_addr, io_limit_addr);
    if ((io_base & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32)
    {
        Printf((CONST_STRPTR) "      I/O upper16: base=0x%04lx limit=0x%04lx\n",
               (ULONG)io_base_upper, (ULONG)io_limit_upper);
    }

    Printf((CONST_STRPTR) "    Bridge memory range: base=0x%08lx limit=0x%08lx\n",
           mem_base_addr, mem_limit_addr);

    if (pref_type == PCI_PREF_RANGE_TYPE_64)
    {
        Printf((CONST_STRPTR) "    Bridge prefetchable memory range (64-bit): base=0x%08lx%08lx limit=0x%08lx%08lx\n",
               (ULONG)pref_base_upper, pref_base_low,
               (ULONG)pref_limit_upper, pref_limit_low);
    }
    else
    {
        Printf((CONST_STRPTR) "    Bridge prefetchable memory range: base=0x%08lx limit=0x%08lx\n",
               pref_base_low, pref_limit_low);
    }

    Printf((CONST_STRPTR) "    Secondary status = 0x%04lx, Bridge control = 0x%04lx\n",
           (ULONG)sec_status, (ULONG)bridge_ctrl);
}

static CONST_STRPTR pci_ea_bei_name(u8 bei)
{
    switch (bei)
    {
    case PCI_EA_BEI_BAR0:
        return (CONST_STRPTR) "BAR0";
    case PCI_EA_BEI_BAR0 + 1:
        return (CONST_STRPTR) "BAR1";
    case PCI_EA_BEI_BAR0 + 2:
        return (CONST_STRPTR) "BAR2";
    case PCI_EA_BEI_BAR0 + 3:
        return (CONST_STRPTR) "BAR3";
    case PCI_EA_BEI_BAR0 + 4:
        return (CONST_STRPTR) "BAR4";
    case PCI_EA_BEI_BAR5:
        return (CONST_STRPTR) "BAR5";
    case PCI_EA_BEI_BRIDGE:
        return (CONST_STRPTR) "Bridge window";
    case PCI_EA_BEI_ENI:
        return (CONST_STRPTR) "Equivalent Not Indicated";
    case PCI_EA_BEI_ROM:
        return (CONST_STRPTR) "Expansion ROM";
    case PCI_EA_BEI_VF_BAR0:
        return (CONST_STRPTR) "VF BAR0";
    case PCI_EA_BEI_VF_BAR0 + 1:
        return (CONST_STRPTR) "VF BAR1";
    case PCI_EA_BEI_VF_BAR0 + 2:
        return (CONST_STRPTR) "VF BAR2";
    case PCI_EA_BEI_VF_BAR0 + 3:
        return (CONST_STRPTR) "VF BAR3";
    case PCI_EA_BEI_VF_BAR0 + 4:
        return (CONST_STRPTR) "VF BAR4";
    case PCI_EA_BEI_VF_BAR5:
        return (CONST_STRPTR) "VF BAR5";
    default:
        return (CONST_STRPTR) "Unknown";
    }
}

static CONST_STRPTR pci_ea_property_name(u8 prop)
{
    switch (prop)
    {
    case PCI_EA_P_MEM:
        return (CONST_STRPTR) "Non-prefetchable memory";
    case PCI_EA_P_MEM_PREFETCH:
        return (CONST_STRPTR) "Prefetchable memory";
    case PCI_EA_P_IO:
        return (CONST_STRPTR) "I/O space";
    case PCI_EA_P_VF_MEM_PREFETCH:
        return (CONST_STRPTR) "VF prefetchable memory";
    case PCI_EA_P_VF_MEM:
        return (CONST_STRPTR) "VF non-prefetchable memory";
    case PCI_EA_P_BRIDGE_MEM:
        return (CONST_STRPTR) "Bridge non-prefetchable memory";
    case PCI_EA_P_BRIDGE_MEM_PREFETCH:
        return (CONST_STRPTR) "Bridge prefetchable memory";
    case PCI_EA_P_BRIDGE_IO:
        return (CONST_STRPTR) "Bridge I/O space";
    case PCI_EA_P_MEM_RESERVED:
        return (CONST_STRPTR) "Reserved memory";
    case PCI_EA_P_IO_RESERVED:
        return (CONST_STRPTR) "Reserved I/O";
    case PCI_EA_P_UNAVAILABLE:
        return (CONST_STRPTR) "Unavailable";
    default:
        return (CONST_STRPTR) "Unknown";
    }
}

static void dump_ea_print_u64(CONST_STRPTR label, unsigned long long value)
{
    if ((value >> 32) != 0)
    {
        Printf((CONST_STRPTR) "          %s = 0x%08lx%08lx\n",
               (ULONG)(APTR)label,
               (ULONG)(value >> 32),
               (ULONG)(value & 0xffffffffu));
    }
    else
    {
        Printf((CONST_STRPTR) "          %s = 0x%08lx\n",
               (ULONG)(APTR)label,
               (ULONG)value);
    }
}

static s32 dump_ea_entry(struct pci_dev *dev, u32 entry_offset, u32 index)
{
    u32 start_offset = entry_offset;
    u32 dw0 = pci_read_config_long((UBYTE)entry_offset, dev);
    entry_offset += 4;

    u32 entry_size = (u32)(((dw0 & PCI_EA_ES) + 1u) << 2);
    u8 bei = (u8)((dw0 & PCI_EA_BEI) >> 4);
    u8 primary_prop = (u8)((dw0 & PCI_EA_PP) >> 8);
    u8 secondary_prop = (u8)((dw0 & PCI_EA_SP) >> 16);
    u8 property = primary_prop;

    if (property > PCI_EA_P_BRIDGE_IO && property < PCI_EA_P_MEM_RESERVED)
        property = secondary_prop;

    u32 enabled = (dw0 & PCI_EA_ENABLE) ? 1u : 0u;
    u32 writable = (dw0 & PCI_EA_WRITABLE) ? 1u : 0u;
    CONST_STRPTR bei_name = pci_ea_bei_name(bei);
    CONST_STRPTR prop_name = pci_ea_property_name(property);

	Printf((CONST_STRPTR) "        EA[%ld]: size=%ld bytes BEI=%ld (%s) prop=0x%02lx (%s) enabled=%ld writable=%ld\n",
           (ULONG)index,
           (ULONG)entry_size,
           (ULONG)bei,
           (ULONG)(APTR)bei_name,
           (ULONG)property,
           (ULONG)(APTR)prop_name,
           (ULONG)enabled,
           (ULONG)writable);

    if (!enabled)
		return (s32)(start_offset + entry_size);
    u32 base_lo = pci_read_config_long((UBYTE)entry_offset, dev);
    entry_offset += 4;
    u32 max_offset_lo = pci_read_config_long((UBYTE)entry_offset, dev);
    entry_offset += 4;

    unsigned long long start = (unsigned long long)(base_lo & PCI_EA_FIELD_MASK);
    unsigned long long max_offset = (unsigned long long)(max_offset_lo & PCI_EA_FIELD_MASK);

    if (base_lo & PCI_EA_IS_64)
    {
        u32 base_hi = pci_read_config_long((UBYTE)entry_offset, dev);
        entry_offset += 4;
        start |= ((unsigned long long)base_hi << 32);
    }

    if (max_offset_lo & PCI_EA_IS_64)
    {
        u32 max_offset_hi = pci_read_config_long((UBYTE)entry_offset, dev);
        entry_offset += 4;
        max_offset |= ((unsigned long long)max_offset_hi << 32);
    }

    unsigned long long limit = start + (max_offset | 0x03ull);
    unsigned long long length = 0;
    if (limit >= start)
        length = (limit - start) + 1;

    dump_ea_print_u64((CONST_STRPTR) "Base", start);
    dump_ea_print_u64((CONST_STRPTR) "Limit", limit);
    dump_ea_print_u64((CONST_STRPTR) "Size", length);

    u32 consumed = (u32)(entry_offset - start_offset);
    if (consumed < entry_size)
        entry_offset = start_offset + entry_size;

    return (s32)entry_offset;
}

static void dump_ea_capability(struct pci_dev *dev, u32 cap_offset, u8 header_layout)
{
    u8 num_entries = (u8)(pci_read_config_byte((UBYTE)(cap_offset + PCI_EA_NUM_ENT), dev)
                         & PCI_EA_NUM_ENT_MASK);

    Printf((CONST_STRPTR) "        Enhanced Allocation entries: %ld\n", (ULONG)num_entries);

    if (header_layout == PCI_HEADER_TYPE_BRIDGE)
    {
        u32 bus_info = pci_read_config_long((UBYTE)(cap_offset + PCI_EA_FIRST_ENT), dev);
        u8 ea_secondary = (u8)(bus_info & PCI_EA_SEC_BUS_MASK);
        u8 ea_subordinate = (u8)((bus_info & PCI_EA_SUB_BUS_MASK) >> PCI_EA_SUB_BUS_SHIFT);

        if (ea_secondary != 0 && ea_subordinate >= ea_secondary)
        {
            Printf((CONST_STRPTR) "          Fixed bus numbers: secondary=%ld subordinate=%ld\n",
                   (ULONG)ea_secondary, (ULONG)ea_subordinate);
        }
        else
        {
            Printf((CONST_STRPTR) "          Fixed bus numbers: not provided\n");
        }
    }

    u32 entry_offset = cap_offset + (u32)((header_layout == PCI_HEADER_TYPE_BRIDGE) ? PCI_EA_FIRST_ENT_BRIDGE : PCI_EA_FIRST_ENT);

    for (u32 index = 0; index < num_entries; ++index)
    {
        s32 next = dump_ea_entry(dev, entry_offset, index);
        if (next <= (s32)entry_offset)
        {
            Printf((CONST_STRPTR) "          Failed to parse EA entry %ld\n", (ULONG)index);
            break;
        }
        entry_offset = (u32)next;
    }
}

static void probe_pci_device(struct pci_dev *dev)
{
    if (!dev)
        return;

    UWORD status = pci_read_config_word(PCI_STATUS, dev);
    UWORD command = pci_read_config_word(PCI_COMMAND, dev);
    UBYTE revision = pci_read_config_byte(PCI_REVISION_ID, dev);
    UBYTE prog_if = pci_read_config_byte(PCI_CLASS_PROG, dev);
    UBYTE subclass = pci_read_config_byte(PCI_CLASS_DEVICE, dev);
    UBYTE baseclass = pci_read_config_byte((UBYTE)(PCI_CLASS_DEVICE + 1), dev);
    UBYTE capability_pointer = pci_read_config_byte(PCI_CAPABILITY_LIST, dev);
    UBYTE interrupt_line = pci_read_config_byte(PCI_INTERRUPT_LINE, dev);
    UBYTE interrupt_pin = pci_read_config_byte(PCI_INTERRUPT_PIN, dev);
    UBYTE header_type = pci_read_config_byte(PCI_HEADER_TYPE, dev);

    u8 header_layout = (u8)(header_type & PCI_HEADER_TYPE_MASK);
    u32 multi_function = (header_type & PCI_HEADER_TYPE_MULTI) ? 1u : 0u;
    CONST_STRPTR header_desc = pci_header_type_name(header_layout);

    Printf((CONST_STRPTR) "  Class = %02lx:%02lx:%02lx (rev %02lx)\n",
           (ULONG)baseclass, (ULONG)subclass, (ULONG)prog_if, (ULONG)revision);
    Printf((CONST_STRPTR) "  Command = 0x%04lx, Status = 0x%04lx\n",
           (ULONG)command, (ULONG)status);
    Printf((CONST_STRPTR) "  Capability Pointer = 0x%02lx\n", (ULONG)capability_pointer);
    Printf((CONST_STRPTR) "  Interrupt Line = 0x%02lx, Pin = 0x%02lx\n",
           (ULONG)interrupt_line, (ULONG)interrupt_pin);
    Printf((CONST_STRPTR) "  Header Type = 0x%02lx (%s)\n",
        (ULONG)header_type, (ULONG)header_desc);
    if (multi_function)
    {
        Printf((CONST_STRPTR) "    Reports multi-function device\n");
    }

    if (header_layout == PCI_HEADER_TYPE_BRIDGE)
    {
        dump_bridge_config(dev);
    }

    if (!(status & PCI_STATUS_CAP_LIST))
    {
        Printf((CONST_STRPTR) "    No standard capabilities advertised\n");
    }
    else
    {
        Printf((CONST_STRPTR) "\n    Standard PCI capabilities:\n");
        for (u32 cap_id = 1; cap_id <= PCI_CAP_ID_MAX; ++cap_id)
        {
            u32 offset = FindCapability(dev, (UBYTE)cap_id);
            if (offset == 0)
                continue;

            UBYTE next_ptr = pci_read_config_byte((UBYTE)(offset + PCI_CAP_LIST_NEXT), dev);
            ULONG dword0 = pci_read_config_long((UBYTE)offset, dev);
            ULONG dword1 = pci_read_config_long((UBYTE)(offset + 4), dev);

            CONST_STRPTR name = pci_capability_name((u8)cap_id);
            Printf((CONST_STRPTR) "      off=0x%02lx id=0x%02lx (%s) next=0x%02lx\n",
                   (ULONG)offset, (ULONG)cap_id, (ULONG)name, (ULONG)next_ptr);
            Printf((CONST_STRPTR) "        dword0=0x%08lx dword1=0x%08lx\n",
                   dword0, dword1);

            if (cap_id == PCI_CAP_ID_MSI)
            {
                UWORD msg_ctl = pci_read_config_word((UBYTE)(offset + PCI_MSI_FLAGS), dev);
                ULONG addr_lo = pci_read_config_long((UBYTE)(offset + PCI_MSI_ADDRESS_LO), dev);
                Printf((CONST_STRPTR) "        MSI: ctl=0x%04lx addr_lo=0x%08lx\n",
                       (ULONG)msg_ctl, addr_lo);
            }
            else if (cap_id == PCI_CAP_ID_MSIX)
            {
                UWORD msg_ctl = pci_read_config_word((UBYTE)(offset + PCI_MSI_FLAGS), dev);
                ULONG table = pci_read_config_long((UBYTE)(offset + 4), dev);
                ULONG pba = pci_read_config_long((UBYTE)(offset + 8), dev);
                Printf((CONST_STRPTR) "        MSI-X: ctl=0x%04lx table=0x%08lx pba=0x%08lx\n",
                       (ULONG)msg_ctl, table, pba);
            }
            else if (cap_id == PCI_CAP_ID_EXP)
            {
                UWORD exp_flags = pci_read_config_word((UBYTE)(offset + PCI_EXP_FLAGS), dev);
                Printf((CONST_STRPTR) "        PCIe Flags=0x%04lx\n",
                       (ULONG)exp_flags);
            }
            else if (cap_id == PCI_CAP_ID_EA)
            {
                dump_ea_capability(dev, offset, header_layout);
            }
        }
    }

    Printf((CONST_STRPTR) "\n    PCIe extended capabilities:\n");
    for (u32 cap_id = 1; cap_id <= PCI_EXT_CAP_ID_MAX; ++cap_id)
    {
        u32 offset = FindExtCapability(dev, (UWORD)cap_id);
        if (offset == 0 || offset >= PCI_CFG_SPACE_EXP_SIZE)
            continue;

        ULONG dword0 = ReadExtConfigLong(offset,     dev);
        ULONG dword1 = ReadExtConfigLong(offset + 4, dev);
        ULONG ext_ver  = (ULONG)PCI_EXT_CAP_VER(dword0);
        ULONG ext_next = (ULONG)PCI_EXT_CAP_NEXT(dword0);
        CONST_STRPTR name = pcie_ext_capability_name((u16)cap_id);
        Printf((CONST_STRPTR) "      off=0x%03lx id=0x%04lx (%s) ver=%ld next=0x%03lx\n",
               (ULONG)offset, (ULONG)cap_id, (ULONG)name, ext_ver, ext_next);
        Printf((CONST_STRPTR) "        dword0=0x%08lx dword1=0x%08lx\n", dword0, dword1);
    }

    Printf((CONST_STRPTR) "\n");

    if (dev->vendor == 0x1106 && dev->device == 0x3483)
    {
        Printf((CONST_STRPTR) "  VL805 specific MMIO dump:\n");
        dump_vl805_registers(dev);
    }
}

static void dump_vl805_registers(struct pci_dev *dev)
{
    if (!dev)
        return;

    void *bar0 = (void *)dev->base_address[0];
    if (!bar0)
    {
        Printf((CONST_STRPTR) "  VL805 BAR0 not mapped\n");
        return;
    }

    Printf((CONST_STRPTR) "  VL805 BAR0 mapped at 0x%08lx\n", (ULONG)bar0);

    volatile UBYTE *regs8 = (volatile UBYTE *)bar0;
    volatile ULONG *regs32 = (volatile ULONG *)bar0;

    UBYTE cap_length = regs8[0];
    UWORD hci_version = *(volatile UWORD *)(regs8 + 2);
    ULONG hcs_params1 = regs32[1];
    ULONG hcs_params2 = regs32[2];
    ULONG hcs_params3 = regs32[3];
    ULONG hcc_params1 = regs32[4];
    ULONG doorbell_offset = regs32[5];
    ULONG runtime_offset = regs32[6];
    ULONG hcc_params2 = regs32[7];

    Printf((CONST_STRPTR) "    xHCI Capability Registers:\n");
    Printf((CONST_STRPTR) "      CAPLENGTH = 0x%02lx\n", (ULONG)cap_length);
    Printf((CONST_STRPTR) "      HCIVERSION = 0x%04lx\n", (ULONG)hci_version);
    Printf((CONST_STRPTR) "      HCSPARAMS1 = 0x%08lx (Slots=%ld Intrs=%ld Ports=%ld)\n",
           hcs_params1,
           (ULONG)(hcs_params1 & 0xff),
           (ULONG)((hcs_params1 >> 8) & 0x7ff),
           (ULONG)((hcs_params1 >> 24) & 0xff));
    Printf((CONST_STRPTR) "      HCSPARAMS2 = 0x%08lx (IST=%ld ERST=%ld Scratch=%ld)\n",
           hcs_params2,
           (ULONG)(hcs_params2 & 0xf),
           (ULONG)((hcs_params2 >> 4) & 0xf),
           (ULONG)((hcs_params2 >> 27) & 0x1f));
    Printf((CONST_STRPTR) "      HCSPARAMS3 = 0x%08lx (U1=%ld U2=%ld)\n",
           hcs_params3,
           (ULONG)(hcs_params3 & 0xff),
           (ULONG)((hcs_params3 >> 16) & 0xffff));
    Printf((CONST_STRPTR) "      HCCPARAMS1 = 0x%08lx\n", hcc_params1);
    Printf((CONST_STRPTR) "      HBOFF = 0x%08lx\n", doorbell_offset);
    Printf((CONST_STRPTR) "      RTSOFF = 0x%08lx\n", runtime_offset);
    Printf((CONST_STRPTR) "      HCCPARAMS2 = 0x%08lx\n", hcc_params2);

    volatile ULONG *op_regs = (volatile ULONG *)(regs8 + cap_length);
    ULONG usbcmd = op_regs[0];
    ULONG usbsts = op_regs[1];
    ULONG pagesize = op_regs[2];
    ULONG dnctrl = op_regs[3];
    ULONG crcr_lo = op_regs[4];
    ULONG crcr_hi = op_regs[5];
    ULONG dcbaap_lo = op_regs[6];
    ULONG dcbaap_hi = op_regs[7];
    ULONG config = op_regs[8];

    Printf((CONST_STRPTR) "    xHCI Operational Registers (offset 0x%02lx):\n", (ULONG)cap_length);
    Printf((CONST_STRPTR) "      USBCMD = 0x%08lx (Run=%ld Reset=%ld IntEn=%ld HSErr=%ld)\n",
           usbcmd,
           (ULONG)(usbcmd & 1),
           (ULONG)((usbcmd >> 1) & 1),
           (ULONG)((usbcmd >> 2) & 1),
           (ULONG)((usbcmd >> 3) & 1));
    Printf((CONST_STRPTR) "      USBSTS = 0x%08lx (Halted=%ld HSE=%ld EINT=%ld PCD=%ld CNR=%ld)\n",
           usbsts,
           (ULONG)(usbsts & 1),
           (ULONG)((usbsts >> 2) & 1),
           (ULONG)((usbsts >> 3) & 1),
           (ULONG)((usbsts >> 4) & 1),
           (ULONG)((usbsts >> 11) & 1));
    Printf((CONST_STRPTR) "      PAGESIZE = 0x%08lx (Page=%ld bytes)\n",
           pagesize,
           (ULONG)(pagesize << 12));
    Printf((CONST_STRPTR) "      DNCTRL = 0x%08lx\n", dnctrl);
    Printf((CONST_STRPTR) "      CRCR = 0x%08lx%08lx\n", crcr_hi, crcr_lo);
    Printf((CONST_STRPTR) "      DCBAAP = 0x%08lx%08lx\n", dcbaap_hi, dcbaap_lo);
    Printf((CONST_STRPTR) "      CONFIG = 0x%08lx (Slots=%ld)\n",
           config,
           (ULONG)(config & 0xff));

    volatile ULONG *port_regs = (volatile ULONG *)(regs8 + cap_length + 0x400);
    ULONG max_ports = (hcs_params1 >> 24) & 0xff;

    Printf((CONST_STRPTR) "    xHCI Port Registers (offset 0x%02lx):\n", (ULONG)(cap_length + 0x400));
    for (ULONG port = 0; port < max_ports && port < 4; ++port)
    {
        ULONG portsc = port_regs[port * 4];
        ULONG portpmsc = port_regs[port * 4 + 1];
        ULONG portli = port_regs[port * 4 + 2];
        ULONG porthlpmc = port_regs[port * 4 + 3];

        Printf((CONST_STRPTR) "      Port %ld:\n", port + 1);
        Printf((CONST_STRPTR) "        PORTSC = 0x%08lx (CCS=%ld PED=%ld PP=%ld Speed=%ld PIC=%ld)\n",
               portsc,
               (ULONG)(portsc & 1),
               (ULONG)((portsc >> 1) & 1),
               (ULONG)((portsc >> 9) & 1),
               (ULONG)((portsc >> 10) & 0xf),
               (ULONG)((portsc >> 14) & 3));
        Printf((CONST_STRPTR) "        PORTPMSC = 0x%08lx\n", portpmsc);
        Printf((CONST_STRPTR) "        PORTLI = 0x%08lx\n", portli);
        Printf((CONST_STRPTR) "        PORTHLPMC = 0x%08lx\n", porthlpmc);
    }

    volatile ULONG *rt_regs = (volatile ULONG *)(regs8 + (runtime_offset & 0xffff));
    ULONG mfindex = rt_regs[0];

    Printf((CONST_STRPTR) "    xHCI Runtime Registers (offset 0x%04lx):\n", (ULONG)(runtime_offset & 0xffff));
    Printf((CONST_STRPTR) "      MFINDEX = 0x%08lx\n", mfindex);

    volatile ULONG *iman = &rt_regs[8];
    Printf((CONST_STRPTR) "      Interrupter 0:\n");
    Printf((CONST_STRPTR) "        IMAN = 0x%08lx (IP=%ld IE=%ld)\n",
           iman[0],
           (ULONG)(iman[0] & 1),
           (ULONG)((iman[0] >> 1) & 1));
    Printf((CONST_STRPTR) "        IMOD = 0x%08lx\n", iman[1]);
    Printf((CONST_STRPTR) "        ERSTSZ = 0x%08lx\n", iman[2]);
    Printf((CONST_STRPTR) "        ERSTBA = 0x%08lx%08lx\n", iman[4], iman[3]);
    Printf((CONST_STRPTR) "        ERDP = 0x%08lx%08lx\n", iman[6], iman[5]);

    volatile ULONG *db_regs = (volatile ULONG *)(regs8 + (doorbell_offset & 0xffff));
    Printf((CONST_STRPTR) "    xHCI Doorbell Array (offset 0x%04lx):\n", (ULONG)(doorbell_offset & 0xffff));
    Printf((CONST_STRPTR) "      DB[0] = 0x%08lx\n", db_regs[0]);
    for (ULONG i = 1; i <= max_ports && i <= 4; ++i)
    {
        Printf((CONST_STRPTR) "      DB[%ld] = 0x%08lx\n", i, db_regs[i]);
    }
}

static void print_device_machine(struct pci_dev *dev)
{
    ULONG bus_num = 0;
    ULONG owner_ptr = 0;
    ULONG addr0 = 0, addr1 = 0, addr2 = 0, addr3 = 0, addr4 = 0, addr5 = 0;
    ULONG size0 = 0, size1 = 0, size2 = 0, size3 = 0, size4 = 0, size5 = 0;
    struct TagItem tags[] = {
        {PRM_BusNumber,   (ULONG)&bus_num},
        {PRM_BoardOwner,  (ULONG)&owner_ptr},
        {PRM_MemoryAddr0, (ULONG)&addr0},
        {PRM_MemoryAddr1, (ULONG)&addr1},
        {PRM_MemoryAddr2, (ULONG)&addr2},
        {PRM_MemoryAddr3, (ULONG)&addr3},
        {PRM_MemoryAddr4, (ULONG)&addr4},
        {PRM_MemoryAddr5, (ULONG)&addr5},
        {PRM_MemorySize0, (ULONG)&size0},
        {PRM_MemorySize1, (ULONG)&size1},
        {PRM_MemorySize2, (ULONG)&size2},
        {PRM_MemorySize3, (ULONG)&size3},
        {PRM_MemorySize4, (ULONG)&size4},
        {PRM_MemorySize5, (ULONG)&size5},
        {TAG_DONE, 0}
    };
    GetBoardAttrsA(dev, tags);
    CONST_STRPTR owner_name = owner_ptr ?
        (CONST_STRPTR)((struct Task *)owner_ptr)->tc_Node.ln_Name :
        (CONST_STRPTR)"-";
    Printf((CONST_STRPTR) "%ld:%02lx.%lx vendor=%04lx device=%04lx class=%02lx:%02lx:%02lx owner=%s",
           bus_num,
           (ULONG)((dev->devfn >> 3) & 0x1f),
           (ULONG)(dev->devfn & 0x07),
           (ULONG)dev->vendor, (ULONG)dev->device,
           (ULONG)((dev->devclass >> 16) & 0xff),
           (ULONG)((dev->devclass >> 8) & 0xff),
           (ULONG)(dev->devclass & 0xff),
           (ULONG)owner_name);
    ULONG sz0 = size0 ? (~size0 + 1u) : 0u;
    if (addr0 != 0 || sz0 != 0)
        Printf((CONST_STRPTR) " bar0=0x%08lx,0x%08lx", addr0, sz0);
    ULONG sz1 = size1 ? (~size1 + 1u) : 0u;
    if (addr1 != 0 || sz1 != 0)
        Printf((CONST_STRPTR) " bar1=0x%08lx,0x%08lx", addr1, sz1);
    ULONG sz2 = size2 ? (~size2 + 1u) : 0u;
    if (addr2 != 0 || sz2 != 0)
        Printf((CONST_STRPTR) " bar2=0x%08lx,0x%08lx", addr2, sz2);
    ULONG sz3 = size3 ? (~size3 + 1u) : 0u;
    if (addr3 != 0 || sz3 != 0)
        Printf((CONST_STRPTR) " bar3=0x%08lx,0x%08lx", addr3, sz3);
    ULONG sz4 = size4 ? (~size4 + 1u) : 0u;
    if (addr4 != 0 || sz4 != 0)
        Printf((CONST_STRPTR) " bar4=0x%08lx,0x%08lx", addr4, sz4);
    ULONG sz5 = size5 ? (~size5 + 1u) : 0u;
    if (addr5 != 0 || sz5 != 0)
        Printf((CONST_STRPTR) " bar5=0x%08lx,0x%08lx", addr5, sz5);
    Printf((CONST_STRPTR) "\n");
}

int main(void)
{
    SysBase = *(struct ExecBase **)4;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR) "dos.library", 0);
    if (!DOSBase)
        return 50;

    LONG rda_args[1] = {0};
    struct RDArgs *rda = ReadArgs((CONST_STRPTR) "QUIET/S", rda_args, NULL);
    if (rda) {
        quiet_mode = (rda_args[0] != 0) ? TRUE : FALSE;
        FreeArgs(rda);
    }

    BCMPCIEBase = OpenLibrary((CONST_STRPTR) "bcmpcie.library", 1);
    if (!BCMPCIEBase)
    {
        Printf((CONST_STRPTR) "Failed to open bcmpcie.library\n");
        CloseLibrary((struct Library *)DOSBase);
        return 10;
    }
    if (!quiet_mode)
        Printf((CONST_STRPTR) "bcmpcie.library opened successfully\n");

    static struct TagItem empty_tags[] = { {TAG_DONE, 0} };
    struct pci_dev *dev = FindBoardA(NULL, empty_tags);
    if (!quiet_mode)
        Printf((CONST_STRPTR) "Starting PCIe device enumeration:\n");
    while (dev != NULL)
    {
        if (quiet_mode) {
            print_device_machine(dev);
        } else {
            print_device(dev);
            probe_pci_device(dev);
        }
        dev = FindBoardA(dev, empty_tags);
    }

    CloseLibrary(BCMPCIEBase);
    CloseLibrary((struct Library *)DOSBase);

    return 0;
}
