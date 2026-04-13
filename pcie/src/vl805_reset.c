/* SPDX-License-Identifier: GPL-2.0-only */

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

	ULONG command[] = {
		7 * sizeof(u32), /* buffer size */
		MAILBOX_PROP_REQ_CODE, /* request code */
		MAILBOX_TAG_NOTIFY_XHCI_RESET, /* tag id */
		sizeof(u32), /* value buffer size */
		sizeof(u32), /* value length */
		0x100000, /* Hardwired RPi4 VL805 PCI address. */
		0, /* end tag */
	};

	MB_RawCommand(command);
	if (command[0] == 0xffffffff || command[1] != MAILBOX_PROP_RESP_CODE_SUCCESS)
	{
		Kprintf("[mailbox] Failed to load vl805's firmware, code=0x%08lx\n", command[1]);
		return -EIO;
	}

	delay_us(200);

	return 0;
}