// Standalone PCIe enumeration tool (integrated, no external library)
// SPDX-License-Identifier: GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/utility_protos.h>
#include <clib/devicetree_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/devicetree.h>
#endif

#include <exec/types.h>
#include <exec/memory.h>
#include <utility/utility.h>

#include <pcie_brcmstb.h>
#include <pci.h>
#include <compat.h>
#include <devtree.h>
#include <debug.h>
#include <minlist.h>
#include <mbox.h>
#include <msg.h>

struct ExecBase *SysBase;
struct Library *UtilityBase;
struct DosLibrary *DOSBase;

extern struct MinList pci_bus_list;

struct pci_controller *debug_pcie_ctrl = NULL;

static void print_device(struct pci_device *dev)
{
    Printf((CONST_STRPTR) "Bus %ld Dev %ld Func %ld: Vendor %04lx Device %04lx Class %02x:%02x:%02x\n",
           (LONG)PCI_BUS(dev->bdf), (LONG)PCI_DEV(dev->bdf), (LONG)PCI_FUNC(dev->bdf),
           dev->vendor, dev->device,
           (dev->class >> 16) & 0xff, (dev->class >> 8) & 0xff, dev->class & 0xff);
}

static void probe_xhci_mmio(void)
{
    struct pci_device *xhci = NULL;
    if (dm_pci_find_device(0x1106, 0x3483, 0, &xhci) != 0)
    {
        Printf((CONST_STRPTR) "VL805 xHCI controller not found.\n");
        return;
    }

    Printf((CONST_STRPTR) "Found VL805 USB 3.0 controller (VIA Labs)\n");
    
    // Check VL805 specific registers first
    ULONG vendor_device;
    UWORD status, command;
    UBYTE revision, prog_if, subclass, baseclass;
    
    dm_pci_read_config32(xhci, PCI_VENDOR_ID, &vendor_device);
    dm_pci_read_config16(xhci, PCI_STATUS, &status);
    dm_pci_read_config16(xhci, PCI_COMMAND, &command);
    dm_pci_read_config8(xhci, PCI_REVISION_ID, &revision);
    dm_pci_read_config8(xhci, PCI_CLASS_PROG, &prog_if);
    dm_pci_read_config8(xhci, PCI_CLASS_DEVICE, &subclass);
    dm_pci_read_config8(xhci, PCI_CLASS_DEVICE + 1, &baseclass);
    
    Printf((CONST_STRPTR) "VL805 Device Info:\n");
    Printf((CONST_STRPTR) "  Vendor:Device = 0x%08lx\n", vendor_device);
    Printf((CONST_STRPTR) "  Class = %02lx:%02lx:%02lx (revision %02lx)\n", baseclass, subclass, prog_if, revision);
    Printf((CONST_STRPTR) "  Status = 0x%04lx, Command = 0x%04lx\n", status, command);
    
    // Check if device is responding to config space
    if (vendor_device == 0xFFFFFFFF) {
        Printf((CONST_STRPTR) "VL805 not responding to config space reads!\n");
        return;
    }
    
    UBYTE cacheline;
    dm_pci_read_config8(xhci, PCI_CACHE_LINE_SIZE, &cacheline);
    Printf((CONST_STRPTR) "Cache Line Size: 0x%02lx\n", cacheline);

    UBYTE latency;
    dm_pci_read_config8(xhci, PCI_LATENCY_TIMER, &latency);
    Printf((CONST_STRPTR) "Latency Timer: 0x%02lx\n", latency);

    UBYTE bist;
    dm_pci_read_config8(xhci, PCI_BIST, &bist);
    Printf((CONST_STRPTR) "BIST register: 0x%02lx\n", bist);

    // Check if BAR is properly configured
    ULONG bar0_val;
    dm_pci_read_config32(xhci, PCI_BASE_ADDRESS_0, &bar0_val);
    Printf((CONST_STRPTR) "BAR0 low base address: 0x%08lx\n", bar0_val);

    ULONG bar0_high;
    dm_pci_read_config32(xhci, PCI_BASE_ADDRESS_1, &bar0_high);
    Printf((CONST_STRPTR) "BAR0 high base address: 0x%08lx\n", bar0_high);

    UBYTE capability_pointer;
    dm_pci_read_config8(xhci, PCI_CAPABILITY_LIST, (UBYTE *)&capability_pointer);
    Printf((CONST_STRPTR) "Capability Pointer: 0x%02lx (should be 0x80)\n", capability_pointer);

    UBYTE interrupt_line;
    dm_pci_read_config8(xhci, PCI_INTERRUPT_LINE, &interrupt_line);
    Printf((CONST_STRPTR) "Interrupt Line: 0x%02lx\n", interrupt_line);

    UBYTE interrupt_pin;
    dm_pci_read_config8(xhci, PCI_INTERRUPT_PIN, &interrupt_pin);
    Printf((CONST_STRPTR) "Interrupt Pin: 0x%02lx\n", interrupt_pin);

    ULONG crcr_mirror_low;
    dm_pci_read_config32(xhci, 0x48, &crcr_mirror_low);
    Printf((CONST_STRPTR) "CRCR Mirror Low: 0x%08lx\n", crcr_mirror_low);

    ULONG mcu_firmware;
    dm_pci_read_config32(xhci, 0x50, &mcu_firmware);
    Printf((CONST_STRPTR) "MCU Firmware Version: 0x%08lx\n", mcu_firmware);

    UWORD pm_status;
    dm_pci_read_config16(xhci, 0x84, &pm_status);
    Printf((CONST_STRPTR) "Power Management Status: 0x%04lx\n", pm_status);
 
    // Check bridge memory windows
    struct pci_device *bridge = xhci->bus->pci_bridge;
    if (bridge) {
        UWORD mem_base, mem_limit;
        dm_pci_read_config16(bridge, PCI_MEMORY_BASE, &mem_base);
        dm_pci_read_config16(bridge, PCI_MEMORY_LIMIT, &mem_limit);
        Printf((CONST_STRPTR) "Bridge memory window: base=0x%04lx limit=0x%04lx\n", mem_base, mem_limit);
        
        UWORD pref_base, pref_limit;
        dm_pci_read_config16(bridge, PCI_PREF_MEMORY_BASE, &pref_base);
        dm_pci_read_config16(bridge, PCI_PREF_MEMORY_LIMIT, &pref_limit);
        Printf((CONST_STRPTR) "Bridge prefetch window: base=0x%04lx limit=0x%04lx\n", pref_base, pref_limit);
    }

    // Map BAR0
    void *bar0 = dm_pci_map_bar(xhci, PCI_BASE_ADDRESS_0, 0, 0x1000, PCI_REGION_TYPE, PCI_REGION_MEM);
    if (!bar0)
    {
        Printf((CONST_STRPTR) "Failed to map xHCI BAR0.\n");
        return;
    }
    Printf((CONST_STRPTR)"xHCI BAR0 mapped at %lx\n", (ULONG)bar0);

    // Debug: Check PCIe controller outbound windows
    extern struct pci_controller *debug_pcie_ctrl;
    if (debug_pcie_ctrl && debug_pcie_ctrl->base) {
        void *base = debug_pcie_ctrl->base;
        Printf((CONST_STRPTR) "PCIe Controller Debug:\n");
        
        // Read raw register values
        ULONG win0_lo = readl(base + 0x400C);
        ULONG win0_hi = readl(base + 0x4010);
        ULONG win0_base_limit = readl(base + 0x4070);
        ULONG win0_base_hi = readl(base + 0x4080);
        ULONG win0_limit_hi = readl(base + 0x4084);
        ULONG misc_ctrl = readl(base + 0x4008);
        
        Printf((CONST_STRPTR) "  PCIE_MEM_WIN0_LO(0) = 0x%08lx\n", win0_lo);
        Printf((CONST_STRPTR) "  PCIE_MEM_WIN0_HI(0) = 0x%08lx\n", win0_hi);
        Printf((CONST_STRPTR) "  PCIE_MEM_WIN0_BASE_LIMIT(0) = 0x%08lx\n", win0_base_limit);
        Printf((CONST_STRPTR) "  PCIE_MEM_WIN0_BASE_HI(0) = 0x%08lx\n", win0_base_hi);
        Printf((CONST_STRPTR) "  PCIE_MEM_WIN0_LIMIT_HI(0) = 0x%08lx\n", win0_limit_hi);
        Printf((CONST_STRPTR) "  PCIE_MISC_MISC_CTRL = 0x%08lx\n", misc_ctrl);
        
        // Reassemble window configuration
        u64 pcie_start = ((u64)win0_hi << 32) | win0_lo;
        
        // Extract base and limit from BASE_LIMIT register (in MB)
        ULONG phys_base_mb = win0_base_limit & 0xFFF;
        ULONG phys_limit_mb = (win0_base_limit >> 20) & 0xFFF;
        
        // Add high bits (bits 31:20 of the MB address become bits 43:32 of byte address)
        u64 phys_base = ((u64)(win0_base_hi & 0xFFF) << 32) | ((u64)phys_base_mb << 20);
        u64 phys_limit = ((u64)(win0_limit_hi & 0xFFF) << 32) | ((u64)phys_limit_mb << 20) | 0xFFFFF;
        u64 window_size = phys_limit - phys_base + 1;
        
        Printf((CONST_STRPTR) "  Window 0 Translation:\n");
        Printf((CONST_STRPTR) "    PCIe Range: 0x%lx%08lx\n", (ULONG)(pcie_start >> 32), (ULONG)(pcie_start & 0xFFFFFFFF));
        Printf((CONST_STRPTR) "    Phys Range: 0x%lx%08lx - 0x%lx%08lx\n", 
            (ULONG)(phys_base >> 32), (ULONG)(phys_base & 0xFFFFFFFF),
            (ULONG)(phys_limit >> 32), (ULONG)(phys_limit & 0xFFFFFFFF));
        Printf((CONST_STRPTR) "    Window Size: 0x%lx%08lx (%ld MB)\n", 
            (ULONG)(window_size >> 32), (ULONG)(window_size & 0xFFFFFFFF), 
            (ULONG)(window_size >> 20));
        
        // Check if our BAR address falls within this window
        ULONG bar0_addr = 0x0C0000000ULL;  // From the logs
        if (bar0_addr >= pcie_start && bar0_addr < (pcie_start + window_size)) {
            u64 translated_addr = phys_base + (bar0_addr - pcie_start);
            Printf((CONST_STRPTR) "    BAR 0x%lx translates to phys 0x%lx%08lx\n", 
                bar0_addr, (ULONG)(translated_addr >> 32), (ULONG)(translated_addr & 0xFFFFFFFF));
        } else {
            Printf((CONST_STRPTR) "    BAR 0x%lx is OUTSIDE window range!\n", bar0_addr);
        }
        
        // Check MISC_CTRL bits
        Printf((CONST_STRPTR) "  MISC_CTRL Analysis:\n");
        Printf((CONST_STRPTR) "    SCB_ACCESS_EN: %s\n", (LONG)((misc_ctrl & (1<<12)) ? "YES" : "NO"));
        Printf((CONST_STRPTR) "    CFG_READ_UR_MODE: %s\n", (LONG)((misc_ctrl & (1<<13)) ? "YES" : "NO"));
        Printf((CONST_STRPTR) "    MAX_BURST_SIZE: %ld\n", (misc_ctrl >> 20) & 0x3);
        Printf((CONST_STRPTR) "    SCB_MAX_BURST_SIZE: %ld\n", (misc_ctrl >> 22) & 0x3);
    }

    volatile UBYTE *regs = (volatile UBYTE *)bar0;
    

    // UBYTE caplen = regs[0];
    // UWORD hci_version = *(UWORD*)(regs + 2);
    ULONG reg0 = *(ULONG*)regs;
    // Printf((CONST_STRPTR)"CAPLENGTH %lx\n", caplen);
    // Printf((CONST_STRPTR)"HCIVERSION %lx\n", hci_version);
    Printf((CONST_STRPTR)"REG0 %lx\n", reg0);
 }

int main(void)
{
    SysBase = *(struct ExecBase **)4;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR) "dos.library", 0);
    if (!DOSBase)
    {
        Printf((CONST_STRPTR) "Failed to open dos.library\n");
        return 50;
    }

    UtilityBase = OpenLibrary((CONST_STRPTR) "utility.library", 0);
    if (!UtilityBase)
    {
        Printf((CONST_STRPTR) "Failed to open utility.library\n");
        return 40;
    }

    struct pci_controller *pcie = AllocMem(sizeof(struct pci_controller), MEMF_CLEAR | MEMF_PUBLIC);
    if (!pcie)
    {
        Printf((CONST_STRPTR) "Failed to allocate memory for PCIe controller\n");
        return 30;
    }

    int ret = mbox_parse_devtree();
    if (ret != 0)
    {
        Printf((CONST_STRPTR) "Failed to parse mailbox devtree\n");
        return 20;
    }

    int res = brcm_pcie_probe(pcie, /* bus number */ 0);
    if (res < 0)
    {
        Printf((CONST_STRPTR) "Failed to probe PCIe controller\n");
        return 10;
    }
    Printf((CONST_STRPTR) "PCIe controller probed successfully\n");
    debug_pcie_ctrl = pcie;  // Store for debugging

    ret = bcm2711_notify_vl805_reset();
    if (ret != 0)
    {
        Printf((CONST_STRPTR) "Failed to notify VL805 reset\n");
        // Not a fatal error, continue
    }
    else
    {
        Printf((CONST_STRPTR) "VL805 reset notified successfully\n");
    }
    // delay_us(5*1000*1000);
    
    _NewMinList(&pci_bus_list);

    struct pci_bus *root_bus = AllocMem(sizeof(*root_bus), MEMF_CLEAR);
    if (!root_bus)
    {
        Printf((CONST_STRPTR) "Failed to allocate memory for root bus\n");
        return 30;
    }

    _NewMinList(&root_bus->devices);
    root_bus->controller = pcie;
    root_bus->parent = NULL;
    root_bus->pci_bridge = NULL;
    SNPrintf(root_bus->name, sizeof(root_bus->name), (CONST_STRPTR) "pcie0");  
    root_bus->bus_number = 0;
    root_bus->bus_number_last_sub = 0;
    AddTailMinList(&pci_bus_list, (struct MinNode *)root_bus);

    ret = pci_bind_bus_devices(root_bus);
    if (ret)
    {
        Printf((CONST_STRPTR) "Failed to bind devices on bus 0\n");
        return 20;
    }

    ret = pci_auto_config_devices(root_bus);
    if (ret < 0)
    {
        Printf((CONST_STRPTR) "Failed to configure devices on bus 0\n");
        return 10;
    }

    int bus_max = pci_get_bus_max();
    for(int i = 0; i <= bus_max; i++)
    {
        struct pci_bus *bus;
        if(pci_get_bus(i, &bus) == 0)
        {
            struct MinNode *node;
            for (node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
            {
                struct pci_device *dev = (struct pci_device *)node;
                print_device(dev);
            }
        }
    }
 
    delay_us(1000 * 1000);

    probe_xhci_mmio();

    brcm_pcie_remove(pcie);

    CloseLibrary((struct Library *)UtilityBase);

    return 0;
}
