// SPDX-License-Identifier: GPL-2.0
/*
 * gpu_reset_deassert.c — Directly deassert GPU reset in SRC register
 *
 * SRC register at 0x16000400 (RESET_GROUP0_S0_DOMAIN_0):
 *   Bit 6 = GPU reset (0=asserted/held, 1=deasserted/running)
 *   Vendor value: 0x07FF0FFF (all deasserted)
 *   Our value:    0x07FF07BF (GPU bit 6 = 0)
 *
 * This module reads the current value, sets bit 6, writes it back.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>

#define SRC_REG_ADDR	0x16000400
#define GPU_RESET_BIT	BIT(6)

static int __init gpu_reset_deassert_init(void)
{
	void __iomem *src;
	u32 val_before, val_after;

	src = ioremap(SRC_REG_ADDR, 4);
	if (!src) {
		pr_err("gpu_reset_deassert: failed to map SRC at 0x%x\n", SRC_REG_ADDR);
		return -EIO;
	}

	val_before = readl(src);
	pr_info("gpu_reset_deassert: SRC before = 0x%08x (GPU bit6 = %d)\n",
		val_before, !!(val_before & GPU_RESET_BIT));

	if (val_before & GPU_RESET_BIT) {
		pr_info("gpu_reset_deassert: GPU reset already deasserted, nothing to do\n");
		iounmap(src);
		return 0;
	}

	/* Deassert GPU reset */
	writel(val_before | GPU_RESET_BIT, src);
	udelay(50);

	val_after = readl(src);
	pr_info("gpu_reset_deassert: SRC after  = 0x%08x (GPU bit6 = %d)\n",
		val_after, !!(val_after & GPU_RESET_BIT));

	if (val_after & GPU_RESET_BIT)
		pr_info("gpu_reset_deassert: SUCCESS — GPU reset deasserted!\n");
	else
		pr_warn("gpu_reset_deassert: FAILED — write did not stick (secure register?)\n");

	iounmap(src);
	return 0;
}

static void __exit gpu_reset_deassert_exit(void)
{
	pr_info("gpu_reset_deassert: unloaded\n");
}

module_init(gpu_reset_deassert_init);
module_exit(gpu_reset_deassert_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Directly deassert GPU reset bit in SRC register");
