// SPDX-License-Identifier: GPL-2.0
/*
 * gpu_clk_enable.c — Enable GPU clocks via raw SCMI mailbox commands
 *
 * Sends CLOCK_CONFIG_SET (msg 0x07) to SCP via mailbox SCMI to enable
 * GPU clock IDs: 30 (gpuclk_400M), 31 (gpu_top), 32 (gpu_core),
 * 230 (gpupll_top), 231 (gpupll_core), 272 (gpuclk_200M)
 *
 * Mailbox SCMI shmem at 0x06590000:
 *   +0x00: reserved (4B)
 *   +0x04: channel_status (4B)
 *   +0x08: reserved (8B)
 *   +0x10: flags (4B)
 *   +0x14: length (4B)
 *   +0x18: header (4B)
 *   +0x1C: payload start
 *   +0x80: doorbell
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>

#define SHMEM_PHYS	0x06590000UL
#define SHMEM_SIZE	0x100

/* SCMI shmem offsets */
#define SHMEM_CHAN_STATUS	0x04
#define SHMEM_FLAGS		0x10
#define SHMEM_LENGTH		0x14
#define SHMEM_HEADER		0x18
#define SHMEM_PAYLOAD		0x1C

/* Doorbell: write to shmem+0x80 to ring */
#define SHMEM_DOORBELL		0x80

/* SCMI header: protocol_id(8) | msg_type(2) | unused(8) | msg_id(8) | seq(4) | token(10) */
/* Simplified: (proto << 10) | msg_id */
#define SCMI_HDR(proto, msg_id, seq) \
	(((proto) << 10) | ((msg_id) << 0) | ((seq) << 18))

/* Clock protocol */
#define SCMI_PROTO_CLOCK	0x14
#define SCMI_CLK_CONFIG_SET	0x07
#define SCMI_CLK_RATE_GET	0x06
#define SCMI_CLK_ATTRIBS	0x03

/* GPU clock IDs */
static const u32 gpu_clk_ids[] = { 30, 31, 32, 230, 231, 272 };
static const char *gpu_clk_names[] = {
	"gpuclk_400M", "gpu_top", "gpu_core",
	"gpupll_top", "gpupll_core", "gpuclk_200M"
};

static void __iomem *shmem;

static int scmi_send(u8 proto, u8 msg_id, u32 *payload, int payload_words,
		     u32 *response, int *resp_words)
{
	u32 hdr, status;
	int timeout;

	/* Wait for channel free */
	timeout = 10000;
	while ((readl(shmem + SHMEM_CHAN_STATUS) & 1) && --timeout)
		udelay(10);
	if (!timeout) {
		pr_err("gpu_clk: channel busy timeout\n");
		return -ETIMEDOUT;
	}

	/* Set up message */
	hdr = SCMI_HDR(proto, msg_id, 0);
	writel(0, shmem + SHMEM_FLAGS);  /* no interrupt */
	writel(4 + payload_words * 4, shmem + SHMEM_LENGTH);  /* header + payload */
	writel(hdr, shmem + SHMEM_HEADER);
	for (int i = 0; i < payload_words; i++)
		writel(payload[i], shmem + SHMEM_PAYLOAD + i * 4);

	/* Ring doorbell */
	writel(1, shmem + SHMEM_DOORBELL);

	/* Wait for response */
	timeout = 100000;
	while (!(readl(shmem + SHMEM_CHAN_STATUS) & 1) && --timeout)
		udelay(10);
	if (!timeout) {
		pr_err("gpu_clk: response timeout\n");
		return -ETIMEDOUT;
	}

	/* Read response */
	status = readl(shmem + SHMEM_PAYLOAD);  /* first word is status */
	if (response && resp_words) {
		int len = (readl(shmem + SHMEM_LENGTH) - 4) / 4;  /* words minus header */
		if (len > *resp_words)
			len = *resp_words;
		for (int i = 0; i < len; i++)
			response[i] = readl(shmem + SHMEM_PAYLOAD + i * 4);
		*resp_words = len;
	}

	return status;
}

static int __init gpu_clk_enable_init(void)
{
	int i, ret;
	u32 payload[2], resp[4];
	int resp_words;

	shmem = ioremap(SHMEM_PHYS, SHMEM_SIZE);
	if (!shmem) {
		pr_err("gpu_clk: failed to map shmem\n");
		return -EIO;
	}

	pr_info("gpu_clk: shmem mapped, chan_status=0x%x\n",
		readl(shmem + SHMEM_CHAN_STATUS));

	for (i = 0; i < ARRAY_SIZE(gpu_clk_ids); i++) {
		/* CLOCK_CONFIG_SET: clock_id, attributes (bit0=enable) */
		payload[0] = gpu_clk_ids[i];
		payload[1] = 1;  /* enable */

		ret = scmi_send(SCMI_PROTO_CLOCK, SCMI_CLK_CONFIG_SET,
				payload, 2, resp, &(int){4});

		pr_info("gpu_clk: clock %u (%s) CONFIG_SET → status=%d\n",
			gpu_clk_ids[i], gpu_clk_names[i], ret);

		if (ret != 0)
			pr_warn("gpu_clk:   FAILED (status=%d)\n", ret);

		udelay(100);
	}

	/* Read back rates to verify */
	for (i = 0; i < ARRAY_SIZE(gpu_clk_ids); i++) {
		payload[0] = gpu_clk_ids[i];
		resp_words = 4;
		ret = scmi_send(SCMI_PROTO_CLOCK, SCMI_CLK_RATE_GET,
				payload, 1, resp, &resp_words);
		if (ret == 0 && resp_words >= 3) {
			u64 rate = ((u64)resp[2] << 32) | resp[1];
			pr_info("gpu_clk: clock %u (%s) rate = %llu Hz\n",
				gpu_clk_ids[i], gpu_clk_names[i], rate);
		} else {
			pr_info("gpu_clk: clock %u rate_get status=%d\n",
				gpu_clk_ids[i], ret);
		}
	}

	iounmap(shmem);
	pr_info("gpu_clk: done\n");
	return 0;
}

static void __exit gpu_clk_enable_exit(void)
{
	pr_info("gpu_clk: unloaded\n");
}

module_init(gpu_clk_enable_init);
module_exit(gpu_clk_enable_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Enable GPU SCMI clocks via raw mailbox commands");
