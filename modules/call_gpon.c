// SPDX-License-Identifier: GPL-2.0
/*
 * call_gpon.c — Load custom SSDT with GPU power-on method, then invoke it
 *
 * Embeds AML bytecode for a method that replicates DMRP without MVCK check.
 * Uses acpi_load_table() to inject it, then acpi_evaluate_integer() to call it.
 */
#include <linux/module.h>
#include <linux/acpi.h>

/*
 * We'll take a simpler approach: just evaluate raw AML to access the
 * RCSU registers through the existing DSDT's GPUP.PPRS OperationRegion.
 *
 * Actually, let's just call DMRP directly but work around MVCK by
 * evaluating the RCSU registers through ACPI directly.
 */

/* Instead of a custom SSDT, let's just call PPRS._ON (which sets MSK0)
 * and then manually do what DMRP does via direct ACPI OperationRegion
 * evaluation from a loaded SSDT.
 */

/* Embedded AML for our custom SSDT - compiled from gpu_poweron.asl */
static unsigned char gpu_poweron_aml[] = {
#include "gpu_poweron_hex.h"
};

static int __init call_gpon_init(void)
{
	acpi_status status;
	unsigned long long result = 0xFFFFFFFF;

	/* Load our custom SSDT */
	status = acpi_load_table(
		(struct acpi_table_header *)gpu_poweron_aml, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("call_gpon: acpi_load_table failed: %s\n",
		       acpi_format_exception(status));
		return -EIO;
	}
	pr_info("call_gpon: SSDT loaded\n");

	/* Invoke our method */
	pr_info("call_gpon: invoking \\_SB.GPON...\n");
	status = acpi_evaluate_integer(NULL, "\\_SB.GPON", NULL, &result);
	if (ACPI_FAILURE(status)) {
		pr_err("call_gpon: GPON failed: %s\n",
		       acpi_format_exception(status));
		return -EIO;
	}

	pr_info("call_gpon: GPON returned 0x%llx (%s)\n",
		result, result == 0 ? "SUCCESS" : "FAILED");

	return 0;
}

static void __exit call_gpon_exit(void)
{
	pr_info("call_gpon: unloaded\n");
}

module_init(call_gpon_init);
module_exit(call_gpon_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Load SSDT and invoke GPU power-on ACPI method");
