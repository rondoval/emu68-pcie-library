// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2012 Stephen Warren
 */

#include <compat.h>
#include <debug.h>
#include <mbox.h>
#include <devtree.h>

#define TIMEOUT 1000 /* ms */

static APTR mailbox_base;

int mbox_parse_devtree(void)
{
	DT_Init();

	CONST_STRPTR dt_node_name = DT_GetAlias((CONST_STRPTR) "mailbox");
	if (dt_node_name == NULL)
	{
		Kprintf("[mailbox] %s: Failed to get aliases from device tree\n", __func__);
		return -ENODEV;
	}

	mailbox_base = DT_GetBaseAddressVirtual(dt_node_name);
	if (mailbox_base == NULL)
	{
		Kprintf("[mailbox] %s: Failed to get mailbox base address\n", __func__);
		return -ENODEV;
	}

	Kprintf("[mailbox] %s: register base: 0x%08lx\n", __func__, mailbox_base);

	return 0;
}

int bcm2835_mbox_call_raw(ULONG chan, ULONG send, ULONG *recv)
{
	struct bcm2835_mbox_regs *regs =
		(struct bcm2835_mbox_regs *)mailbox_base;
	ULONG endtime = get_time() + TIMEOUT * 1000;
	ULONG val;

	KprintfH("time: %lu timeout: %lu\n", get_time(), endtime);

	if (send & BCM2835_CHAN_MASK)
	{
		Kprintf("mbox: Illegal mbox data 0x%08lx\n", send);
		return -1;
	}

	/* Drain any stale responses */

	for (;;)
	{
		val = readl(&regs->mail0_status);
		if (val & BCM2835_MBOX_STATUS_RD_EMPTY)
			break;
		if (get_time() >= endtime)
		{
			Kprintf("mbox: Timeout draining stale responses\n");
			return -1;
		}
		val = readl(&regs->read);
	}

	/* Wait for space to send */

	for (;;)
	{
		val = readl(&regs->mail1_status);
		if (!(val & BCM2835_MBOX_STATUS_WR_FULL))
			break;
		if (get_time() >= endtime)
		{
			Kprintf("mbox: Timeout waiting for send space\n");
			return -1;
		}
	}

	/* Send the request */

	val = BCM2835_MBOX_PACK(chan, send);
	KprintfH("mbox: TX raw: 0x%08lx\n", val);
	writel(val, &regs->write);

	/* Wait for the response */

	for (;;)
	{
		val = readl(&regs->mail0_status);
		if (!(val & BCM2835_MBOX_STATUS_RD_EMPTY))
			break;
		if (get_time() >= endtime)
		{
			Kprintf("mbox: Timeout waiting for response\n");
			return -1;
		}
	}

	/* Read the response */

	val = readl(&regs->read);
	KprintfH("mbox: RX raw: 0x%08lx\n", val);

	/* Validate the response */

	if (BCM2835_MBOX_UNPACK_CHAN(val) != chan)
	{
		Kprintf("mbox: Response channel mismatch\n");
		return -1;
	}

	*recv = BCM2835_MBOX_UNPACK_DATA(val);

	return 0;
}

#ifdef DEBUG_HIGH
void dump_buf(struct bcm2835_mbox_hdr *buffer)
{
	ULONG *p;
	ULONG words;
	int i;

	p = (ULONG *)buffer;
	words = buffer->buf_size / 4;
	for (i = 0; i < words; i++)
		Kprintf("    0x%04lx: 0x%08lx\n", i * 4, p[i]);
}
#endif

int bcm2835_mbox_call_prop(ULONG chan, struct bcm2835_mbox_hdr *buffer)
{
#ifdef DEBUG_HIGH
	KprintfH("mbox: TX buffer\n");
	dump_buf(buffer);
#endif
	ULONG size_aligned = roundup(buffer->buf_size, ARCH_DMA_MINALIGN);

	/* Video Core is little endian, so we need to swap the bytes */
	const ULONG size = buffer->buf_size;
	ULONG *p = (ULONG *)buffer;
	for (int i = 0; i < size / 4; i++)
	{
		p[i] = __builtin_bswap32(p[i]);
	}

	CachePreDMA(buffer, &size_aligned, 0);

	ULONG rbuffer;
	int ret = bcm2835_mbox_call_raw(chan,
									(ULONG)buffer,
									&rbuffer);
	if (ret)
		return ret;

	CachePostDMA(buffer, &size_aligned, 0);

	/* Video Core is little endian, so we need to swap the bytes */
	for (int i = 0; i < size / 4; i++)
	{
		p[i] = __builtin_bswap32(p[i]);
	}

	if (rbuffer != (ULONG)buffer)
	{
		Kprintf("mbox: Response buffer mismatch\n");
		return -1;
	}

#ifdef DEBUG_HIGH
	KprintfH("mbox: RX buffer\n");
	dump_buf(buffer);
#endif

	/* Validate overall response status */

	if (buffer->code != BCM2835_MBOX_RESP_CODE_SUCCESS)
	{
		Kprintf("mbox: Header response code invalid\n");
		return -1;
	}

	/* Validate each tag's response status */

	struct bcm2835_mbox_tag_hdr *tag = (void *)(buffer + 1);
	int tag_index = 0;
	while (tag->tag)
	{
		if (!(tag->val_len & BCM2835_MBOX_TAG_VAL_LEN_RESPONSE))
		{
			Kprintf("mbox: Tag %ld missing val_len response bit\n", tag_index);
			return -1;
		}
		/*
		 * Clear the reponse bit so clients can just look right at the
		 * length field without extra processing
		 */
		tag->val_len &= ~BCM2835_MBOX_TAG_VAL_LEN_RESPONSE;
		tag = (void *)(((UBYTE *)tag) + sizeof(*tag) + tag->val_buf_size);
		tag_index++;
	}

	return 0;
}
