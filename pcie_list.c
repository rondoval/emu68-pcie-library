// Standalone PCIe enumeration tool (integrated, no external library)
// SPDX-License-Identifier: GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/devicetree_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/devicetree.h>
#endif

#include <exec/types.h>
#include <exec/memory.h>

#include <pcie_brcmstb.h>
#include <pci.h>
#include <compat.h>
#include <devtree.h>
#include <debug.h>
#include <minlist.h>

struct ExecBase *SysBase;
extern struct MinList pci_bus_list;

static void print_device(struct pci_device *dev)
{
    Printf((CONST_STRPTR) "Bus %ld Dev %ld Func %ld: Vendor %04lx Device %04lx Class %02x:%02x:%02x\n",
           (LONG)PCI_BUS(dev->bdf), (LONG)PCI_DEV(dev->bdf), (LONG)PCI_FUNC(dev->bdf),
           dev->vendor, dev->device,
           (dev->class >> 16) & 0xff, (dev->class >> 8) & 0xff, dev->class & 0xff);
}

int main(void)
{
    SysBase = *(struct ExecBase **)4;
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

    struct pci_bus *root_bus;
    struct pci_device *root_bridge;
    _NewMinList(&pci_bus_list);
    pci_create_device(NULL, PCI_BDF(0, 0, 0), 0x14e4, 0x43a0, 0x060400, &root_bridge);
    pci_create_bus(&root_bus, NULL, root_bridge, pcie);
    pci_probe_bus(root_bus);

    //TODO enumerate all buses and print devices on them
    int bus_max = pci_get_bus_max();
    Printf((CONST_STRPTR) "Max bus number: %ld\n", bus_max);
    for(int i = 0; i <= bus_max; i++)
    {
        struct pci_bus *bus;
        if(pci_get_bus(i, &bus) == 0)
        {
            Printf((CONST_STRPTR) "Bus %ld:\n", i);
            struct MinNode *node;
            for (node = bus->devices.mlh_Head; node->mln_Succ; node = node->mln_Succ)
            {
                struct pci_device *dev = (struct pci_device *)node;
                print_device(dev);
            }
        }
    }

    // /* List root complex (bus 0) */
    // print_device(pcie, PCI_BDF(0, 0, 0));

    // /* Configure bus number registers */
    // brcm_pcie_write_config(pcie, PCI_BDF(0, 0, 0), PCI_PRIMARY_BUS, 0, PCI_SIZE_8);
    // brcm_pcie_write_config(pcie, PCI_BDF(0, 0, 0), PCI_SECONDARY_BUS, 1, PCI_SIZE_8);
    // brcm_pcie_write_config(pcie, PCI_BDF(0, 0, 0), PCI_SUBORDINATE_BUS, 0xff, PCI_SIZE_8);

    // Printf((CONST_STRPTR) "Set bridge bus numbers: 0 -> 1\n");

    // /* Small delay so that secondary bus becomes active */
    // delay_us(100 * 1000);

    // /* Scan bus 1 */
    // for (int dev = 0; dev < 32; ++dev)
    // {
    //     ULONG vendor;
    //     pci_dev_t bdf0 = PCI_BDF(1, dev, 0);
    //     if (brcm_pcie_read_config(pcie, bdf0, PCI_VENDOR_ID, &vendor, PCI_SIZE_16))
    //         continue;
    //     if (vendor == 0xffff || vendor == 0x0000)
    //     {
    //         /* No device present at this slot */
    //         continue;
    //     }
    //     /* We found a device; print function 0 then check multi-function */
    //     print_device(pcie, bdf0);
    //     ULONG header_type;
    //     if (!brcm_pcie_read_config(pcie, bdf0, PCI_HEADER_TYPE, &header_type, PCI_SIZE_8))
    //     {
    //         if (header_type & 0x80)
    //         {
    //             for (int fn = 1; fn < 8; ++fn)
    //             {
    //                 pci_dev_t bdf = PCI_BDF(1, dev, fn);
    //                 ULONG v2;
    //                 if (brcm_pcie_read_config(pcie, bdf, PCI_VENDOR_ID, &v2, PCI_SIZE_16))
    //                     continue;
    //                 if (v2 == 0xffff || v2 == 0x0000)
    //                     continue;
    //                 print_device(pcie, bdf);
    //             }
    //         }
    //     }
    // }

    brcm_pcie_remove(pcie);

    return 0;
}
