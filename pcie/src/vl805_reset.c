/* SPDX-License-Identifier: GPL-2.0-only */

#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>      /* OpenResource */

#include <debug.h>
#include <pcie_brcmstb.h>

#include <errors.h>
#include <timing.h>

#ifdef __INTELLISENSE__
#include <clib/mailbox_protos.h>
#else
#include <proto/mailbox.h>
#endif

#define MAILBOX_PROP_REQ_CODE 0x00000000UL
#define MAILBOX_PROP_RESP_CODE_SUCCESS 0x80000000UL
#define MAILBOX_TAG_NOTIFY_XHCI_RESET 0x00030058UL

/* Ask VideoCore to reload VL805 firmware after PCI reset. */
s32 bcm2711_reload_vl805_firmware(void)
{
	APTR MailboxBase = OpenResource((CONST_STRPTR) "mailbox.resource");
	if (!MailboxBase)
	{
		Kprintf("[mailbox] Failed to open mailbox.resource\n");
		return -EIO;
	}

	ULONG command[7];
	command[0] = 7 * sizeof(u32);                /* buffer size */
	command[1] = MAILBOX_PROP_REQ_CODE;          /* request code */
	command[2] = MAILBOX_TAG_NOTIFY_XHCI_RESET;  /* tag id */
	command[3] = sizeof(u32);                    /* value buffer size */
	command[4] = sizeof(u32);                    /* value length */
	command[5] = 0x100000;                       /* Hardwired RPi4 VL805 PCI address. */
	command[6] = 0;                              /* end tag */

	MB_RawCommand(command);
	if (command[0] == 0xffffffff || command[1] != MAILBOX_PROP_RESP_CODE_SUCCESS)
	{
		Kprintf("[mailbox] Failed to load vl805's firmware, code=0x%08lx\n", command[1]);
		return -EIO;
	}

	delay_us(200);

	return 0;
}