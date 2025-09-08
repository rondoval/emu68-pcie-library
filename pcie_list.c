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
        Printf((CONST_STRPTR) "xHCI controller not found (class 0c:03:30).\n");
        return;
    }

    // Ensure MEM + MASTER are enabled
    dm_pci_clrset_config16(xhci, PCI_COMMAND, 0, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    // Map BAR0
    void *bar0 = dm_pci_map_bar(xhci, PCI_BASE_ADDRESS_0, 0, 0x1000, PCI_REGION_TYPE, PCI_REGION_MEM);
    if (!bar0)
    {
        Printf((CONST_STRPTR) "Failed to map xHCI BAR0.\n");
        return;
    }
    Kprintf("xHCI BAR0 mapped at %lx\n", bar0);

    volatile ULONG *regs = (volatile ULONG *)bar0;
    // dump first 32 bytes of BAR0
    for (int i = 0; i < 32; i++)
    {
        Kprintf("xHCI BAR0[%02ld] = %08lx\n", i * 4, *(regs+i));
    }

    // ULONG cap = regs[0x00 / 4];
    // UBYTE caplen = (UBYTE)(cap & 0xff);
    // UWORD hciver = (UWORD)((cap >> 16) & 0xffff);
    // ULONG hcs1 = regs[0x04 / 4];
    // ULONG hcc1 = regs[0x10 / 4];
    // ULONG dboff = regs[0x14 / 4];
    // ULONG rtsoff = regs[0x18 / 4];

    // Printf((CONST_STRPTR) "xHCI @ %08lx: CAPLENGTH=%ld, HCIVERSION=%04lx, HCS1=%08lx, HCC1=%08lx, DBOFF=%08lx, RTSOFF=%08lx\n",
    //        (ULONG)bar0, (LONG)caplen, (ULONG)hciver, hcs1, hcc1, dboff, rtsoff);

    // // Operational registers base = BAR + caplength
    // volatile ULONG *op = (volatile ULONG *)((UBYTE *)bar0 + caplen);
    // ULONG usbcmd = op[0x00 / 4];
    // ULONG usbsts = op[0x04 / 4];
    // ULONG pagesize = op[0x08 / 4];
    // Printf((CONST_STRPTR) "xHCI OP: USBCMD=%08lx, USBSTS=%08lx, PAGESIZE=%08lx\n", usbcmd, usbsts, pagesize);
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

    int ret = pci_bind_bus_devices(root_bus);
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

    probe_xhci_mmio();

    brcm_pcie_remove(pcie);

    CloseLibrary((struct Library *)UtilityBase);

    return 0;
}
