// SPDX-License-Identifier: GPL-2.0
/*
 * gpu_acpi_poweron.c — Invoke GPUP ACPI power resource _ON to power the GPU
 *
 * The DSDT defines GPUP (HID "CIXH5001") with a PowerResource that:
 *   1. Sets RCSU MSK0 (0x15000218) bits to enable GPU partition
 *   2. Calls DMRP() to deassert GPU reset via the RCSU power-on sequence
 *
 * This module finds GPUP, evaluates _PR0 → PowerResource → _ON,
 * then reads back _STA to confirm.
 */
#include <linux/module.h>
#include <linux/acpi.h>

static acpi_status find_gpup(acpi_handle handle, u32 level, void *ctx, void **ret)
{
	struct acpi_device_info *info;
	acpi_status status;

	status = acpi_get_object_info(handle, &info);
	if (ACPI_FAILURE(status))
		return AE_OK;

	if (info->valid & ACPI_VALID_HID &&
	    strcmp(info->hardware_id.string, "CIXH5001") == 0) {
		*(acpi_handle *)ctx = handle;
		kfree(info);
		return AE_CTRL_TERMINATE;
	}

	kfree(info);
	return AE_OK;
}

static int __init gpu_acpi_poweron_init(void)
{
	acpi_handle gpup = NULL;
	acpi_handle pprs = NULL;
	acpi_status status;
	unsigned long long sta_val = 0;

	/* Find GPUP device */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX, find_gpup, NULL, &gpup, NULL);
	if (!gpup) {
		pr_err("gpu_acpi_poweron: GPUP (CIXH5001) not found in ACPI namespace\n");
		return -ENODEV;
	}
	pr_info("gpu_acpi_poweron: found GPUP device\n");

	/* Find PPRS power resource child */
	status = acpi_get_handle(gpup, "PPRS", &pprs);
	if (ACPI_FAILURE(status)) {
		pr_err("gpu_acpi_poweron: PPRS power resource not found under GPUP\n");
		return -ENODEV;
	}

	/* Check current power state */
	status = acpi_evaluate_integer(pprs, "_STA", NULL, &sta_val);
	if (ACPI_SUCCESS(status))
		pr_info("gpu_acpi_poweron: PPRS._STA before = %llu (%s)\n",
			sta_val, sta_val ? "ON" : "OFF");

	/* Call _ON */
	pr_info("gpu_acpi_poweron: calling PPRS._ON...\n");
	status = acpi_evaluate_object(pprs, "_ON", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("gpu_acpi_poweron: PPRS._ON failed: %s\n",
		       acpi_format_exception(status));
		return -EIO;
	}
	pr_info("gpu_acpi_poweron: PPRS._ON returned successfully\n");

	/* Verify */
	sta_val = 0;
	status = acpi_evaluate_integer(pprs, "_STA", NULL, &sta_val);
	if (ACPI_SUCCESS(status))
		pr_info("gpu_acpi_poweron: PPRS._STA after = %llu (%s)\n",
			sta_val, sta_val ? "ON" : "OFF");

	return 0;
}

static void __exit gpu_acpi_poweron_exit(void)
{
	pr_info("gpu_acpi_poweron: unloaded (GPU power state unchanged)\n");
}

module_init(gpu_acpi_poweron_init);
module_exit(gpu_acpi_poweron_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Invoke GPUP ACPI power resource to power on CIX GPU");
