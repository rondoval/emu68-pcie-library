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

    UBYTE capability_pointer;
    dm_pci_read_config8(xhci, PCI_CAPABILITY_LIST, (UBYTE *)&capability_pointer);
    Printf((CONST_STRPTR) "Capability Pointer: 0x%02lx (should be 0x80)\n", capability_pointer);

    UBYTE interrupt_line;
    dm_pci_read_config8(xhci, PCI_INTERRUPT_LINE, &interrupt_line);
    Printf((CONST_STRPTR) "Interrupt Line: 0x%02lx\n", interrupt_line);

    UBYTE interrupt_pin;
    dm_pci_read_config8(xhci, PCI_INTERRUPT_PIN, &interrupt_pin);
    Printf((CONST_STRPTR) "Interrupt Pin: 0x%02lx\n", interrupt_pin);

    ULONG mcu_firmware;
    dm_pci_read_config32(xhci, 0x50, &mcu_firmware);
    Printf((CONST_STRPTR) "MCU Firmware Version: 0x%08lx\n", mcu_firmware);

    UWORD pm_status;
    dm_pci_read_config16(xhci, 0x84, &pm_status);
    Printf((CONST_STRPTR) "Power Management Status: 0x%04lx\n", pm_status);

    // Map BAR0
    void *bar0 = dm_pci_map_bar(xhci, PCI_BASE_ADDRESS_0, 0, 0x1000, PCI_REGION_TYPE, PCI_REGION_MEM);
    if (!bar0)
    {
        Printf((CONST_STRPTR) "Failed to map xHCI BAR0.\n");
        return;
    }
    Printf((CONST_STRPTR)"xHCI BAR0 mapped at %lx\n", (ULONG)bar0);

    volatile UBYTE *regs = (volatile UBYTE *)bar0;   
    volatile ULONG *regs32 = (volatile ULONG *)bar0;

    // Capability Registers (Section 5.3 of xHCI spec)
    UBYTE caplen = regs[0];
    UWORD hci_version = *(UWORD*)(regs + 2);
    ULONG hcs_params1 = regs32[1];
    ULONG hcs_params2 = regs32[2]; 
    ULONG hcs_params3 = regs32[3];
    ULONG hcc_params1 = regs32[4];
    ULONG db_offset = regs32[5];
    ULONG rts_offset = regs32[6];
    ULONG hcc_params2 = regs32[7];
    
    Printf((CONST_STRPTR)"=== xHCI Capability Registers ===\n");
    Printf((CONST_STRPTR)"CAPLENGTH: 0x%02lx\n", caplen);
    Printf((CONST_STRPTR)"HCIVERSION: 0x%04lx\n", hci_version);
    Printf((CONST_STRPTR)"HCSPARAMS1: 0x%08lx (MaxSlots=%ld, MaxIntrs=%ld, MaxPorts=%ld)\n", 
           hcs_params1, hcs_params1 & 0xFF, (hcs_params1 >> 8) & 0x7FF, (hcs_params1 >> 24) & 0xFF);
    Printf((CONST_STRPTR)"HCSPARAMS2: 0x%08lx (IST=%ld, ERST_Max=%ld, MaxScratch=%ld)\n", 
           hcs_params2, hcs_params2 & 0xF, (hcs_params2 >> 4) & 0xF, (hcs_params2 >> 27) & 0x1F);
    Printf((CONST_STRPTR)"HCSPARAMS3: 0x%08lx (U1_Exit_Latency=%ld, U2_Exit_Latency=%ld)\n", 
           hcs_params3, hcs_params3 & 0xFF, (hcs_params3 >> 16) & 0xFFFF);
    Printf((CONST_STRPTR)"HCCPARAMS1: 0x%08lx\n", hcc_params1);
    Printf((CONST_STRPTR)"DBOFF: 0x%08lx (Doorbell Array Offset)\n", db_offset);
    Printf((CONST_STRPTR)"RTSOFF: 0x%08lx (Runtime Registers Offset)\n", rts_offset);
    Printf((CONST_STRPTR)"HCCPARAMS2: 0x%08lx\n", hcc_params2);

    // Operational Registers (Section 5.4 of xHCI spec)
    volatile ULONG *op_regs = (volatile ULONG *)(regs + caplen);
    ULONG usbcmd = op_regs[0];
    ULONG usbsts = op_regs[1]; 
    ULONG pagesize = op_regs[2];
    ULONG dnctrl = op_regs[3];
    ULONG crcr_lo = op_regs[4];
    ULONG crcr_hi = op_regs[5];
    ULONG dcbaap_lo = op_regs[6];
    ULONG dcbaap_hi = op_regs[7];
    ULONG config = op_regs[8];
    
    Printf((CONST_STRPTR)"\n=== xHCI Operational Registers (offset 0x%02lx) ===\n", caplen);
    Printf((CONST_STRPTR)"USBCMD: 0x%08lx (Run=%ld, Reset=%ld, IntEn=%ld, HSErr=%ld)\n", 
           usbcmd, usbcmd & 1, (usbcmd >> 1) & 1, (usbcmd >> 2) & 1, (usbcmd >> 3) & 1);
    Printf((CONST_STRPTR)"USBSTS: 0x%08lx (HCHalted=%ld, HSE=%ld, EINT=%ld, PCD=%ld, CNR=%ld)\n", 
           usbsts, usbsts & 1, (usbsts >> 2) & 1, (usbsts >> 3) & 1, (usbsts >> 4) & 1, (usbsts >> 11) & 1);
    Printf((CONST_STRPTR)"PAGESIZE: 0x%08lx (Page Size = %ld bytes)\n", pagesize, pagesize << 12);
    Printf((CONST_STRPTR)"DNCTRL: 0x%08lx (Notification Enable)\n", dnctrl);
    Printf((CONST_STRPTR)"CRCR: 0x%08lx%08lx (Command Ring Control)\n", crcr_hi, crcr_lo);
    Printf((CONST_STRPTR)"DCBAAP: 0x%08lx%08lx (Device Context Base Address Array Pointer)\n", dcbaap_hi, dcbaap_lo);
    Printf((CONST_STRPTR)"CONFIG: 0x%08lx (Max Device Slots Enabled = %ld)\n", config, config & 0xFF);

    // Port Status and Control Registers
    volatile ULONG *port_regs = (volatile ULONG *)(regs + caplen + 0x400);
    ULONG max_ports = (hcs_params1 >> 24) & 0xFF;
    
    Printf((CONST_STRPTR)"\n=== xHCI Port Registers (offset 0x%02lx) ===\n", caplen + 0x400);
    for (ULONG port = 0; port < max_ports && port < 4; port++) {
        ULONG portsc = port_regs[port * 4];
        ULONG portpmsc = port_regs[port * 4 + 1];
        ULONG portli = port_regs[port * 4 + 2];
        ULONG porthlpmc = port_regs[port * 4 + 3];
        
        Printf((CONST_STRPTR)"Port %ld:\n", port + 1);
        Printf((CONST_STRPTR)"  PORTSC: 0x%08lx (CCS=%ld, PED=%ld, PP=%ld, Speed=%ld, PIC=%ld)\n", 
               portsc, portsc & 1, (portsc >> 1) & 1, (portsc >> 9) & 1, 
               (portsc >> 10) & 0xF, (portsc >> 14) & 3);
        Printf((CONST_STRPTR)"  PORTPMSC: 0x%08lx\n", portpmsc);
        Printf((CONST_STRPTR)"  PORTLI: 0x%08lx\n", portli);
        Printf((CONST_STRPTR)"  PORTHLPMC: 0x%08lx\n", porthlpmc);
    }

    // Runtime Registers
    volatile ULONG *rt_regs = (volatile ULONG *)(regs + (rts_offset & 0xFFFF));
    ULONG mfindex = rt_regs[0];
    
    Printf((CONST_STRPTR)"\n=== xHCI Runtime Registers (offset 0x%04lx) ===\n", rts_offset & 0xFFFF);
    Printf((CONST_STRPTR)"MFINDEX: 0x%08lx (Microframe Index)\n", mfindex);

    // Show first few interrupter registers
    volatile ULONG *iman = &rt_regs[8];  // First interrupter at offset 0x20
    Printf((CONST_STRPTR)"Interrupter 0:\n");
    Printf((CONST_STRPTR)"  IMAN: 0x%08lx (IP=%ld, IE=%ld)\n", iman[0], iman[0] & 1, (iman[0] >> 1) & 1);
    Printf((CONST_STRPTR)"  IMOD: 0x%08lx\n", iman[1]);
    Printf((CONST_STRPTR)"  ERSTSZ: 0x%08lx (Table Size = %ld)\n", iman[2], iman[2] & 0xFFFF);
    Printf((CONST_STRPTR)"  ERSTBA: 0x%08lx%08lx\n", iman[4], iman[3]);
    Printf((CONST_STRPTR)"  ERDP: 0x%08lx%08lx\n", iman[6], iman[5]);

    // Doorbell Array  
    volatile ULONG *db_regs = (volatile ULONG *)(regs + (db_offset & 0xFFFF));
    Printf((CONST_STRPTR)"\n=== xHCI Doorbell Array (offset 0x%04lx) ===\n", db_offset & 0xFFFF);
    Printf((CONST_STRPTR)"DB[0] (Host Controller): 0x%08lx\n", db_regs[0]);
    for (ULONG i = 1; i <= 4 && i <= max_ports; i++) {
        Printf((CONST_STRPTR)"DB[%ld]: 0x%08lx\n", i, db_regs[i]);
    }


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
 

    ret = bcm2711_notify_vl805_reset();
    if (ret != 0)
    {
        Printf((CONST_STRPTR) "Failed to notify VL805 reset\n");
        // Not a fatal error, continue
    }
    delay_us(1000);

    probe_xhci_mmio();

    brcm_pcie_remove(pcie);

    CloseLibrary((struct Library *)UtilityBase);
    CloseLibrary((struct Library *)DOSBase);

    return 0;
}
