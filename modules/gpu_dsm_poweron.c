// SPDX-License-Identifier: GPL-2.0
/*
 * gpu_dsm_poweron.c — Power on GPU via ACPI power resource management
 *
 * GPUP (CIXH5001) has PowerResource PPRS with _ON that:
 *   1. Sets RCSU MSK0 (0x15000218) bits 0x1000|0x0FFC
 *   2. Calls DMRP(1, 4, 0x15000000, 1)
 *
 * We trigger this by setting the ACPI device to D0 power state,
 * which evaluates _PR0 → PPRS → _ON.
 */
#include <linux/module.h>
#include <linux/acpi.h>

static int __init gpu_dsm_poweron_init(void)
{
	struct acpi_device *gpup_adev;
	struct acpi_device *gpu_adev;
	int ret;

	/* Find GPUP (power supply device) */
	gpup_adev = acpi_dev_get_first_match_dev("CIXH5001", NULL, -1);
	if (!gpup_adev) {
		pr_err("gpu_poweron: CIXH5001 (GPUP) not found\n");
		return -ENODEV;
	}
	pr_info("gpu_poweron: found GPUP\n");

	/* Set GPUP to D0 (full power) — this triggers _PR0 → PPRS._ON */
	pr_info("gpu_poweron: setting GPUP to D0...\n");
	ret = acpi_device_set_power(gpup_adev, ACPI_STATE_D0);
	pr_info("gpu_poweron: set_power(D0) returned %d\n", ret);

	acpi_dev_put(gpup_adev);

	/* Also try setting GPU device (CIXH5000) to D0 */
	gpu_adev = acpi_dev_get_first_match_dev("CIXH5000", NULL, -1);
	if (gpu_adev) {
		pr_info("gpu_poweron: setting GPU to D0...\n");
		ret = acpi_device_set_power(gpu_adev, ACPI_STATE_D0);
		pr_info("gpu_poweron: GPU set_power(D0) returned %d\n", ret);
		acpi_dev_put(gpu_adev);
	}

	pr_info("gpu_poweron: done\n");
	return 0;
}

static void __exit gpu_dsm_poweron_exit(void)
{
	pr_info("gpu_poweron: unloaded\n");
}

module_init(gpu_dsm_poweron_init);
module_exit(gpu_dsm_poweron_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Power on GPU via ACPI device power state management");
