// Register a platform device to trigger scmi_transport_smc probe on ACPI.
// Also provide an "arm,scmi-shmem" provider via software nodes so
// shmem_setup_iomap() can map the SMC shared memory.
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define SMC_SCMI_SHMEM_BASE 0x84380000ULL
#define SMC_SCMI_SHMEM_SIZE 0x1000ULL

static struct platform_device *shmem_pdev;
static struct platform_device *smc_pdev;

static const struct property_entry shmem_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "arm,scmi-shmem"),
	{ }
};

static struct software_node shmem_swnode = {
	.name = "scmi-smc-shmem",
	.properties = shmem_props,
};

static const struct software_node_ref_args shmem_ref = {
	.node = &shmem_swnode,
};

static const struct property_entry smc_props[] = {
	PROPERTY_ENTRY_REF("shmem", &shmem_swnode),
	{ }
};

static struct software_node smc_swnode = {
	.name = "arm-scmi-smc",
	.properties = smc_props,
};

static int __init scmi_smc_pdev_init(void)
{
	int ret;
	struct resource res = {
		.start = SMC_SCMI_SHMEM_BASE,
		.end = SMC_SCMI_SHMEM_BASE + SMC_SCMI_SHMEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	};

	/* SHMEM provider device */
	shmem_pdev = platform_device_alloc("scmi-smc-shmem", PLATFORM_DEVID_AUTO);
	if (!shmem_pdev)
		return -ENOMEM;
	ret = platform_device_add_resources(shmem_pdev, &res, 1);
	if (ret)
		goto err_put_shmem;
	ret = device_add_software_node(&shmem_pdev->dev, &shmem_swnode);
	if (ret)
		goto err_put_shmem;
	ret = platform_device_add(shmem_pdev);
	if (ret)
		goto err_rm_shmem_node;

	/* SCMI SMC transport supplier device */
	smc_pdev = platform_device_alloc("arm-scmi-smc", PLATFORM_DEVID_AUTO);
	if (!smc_pdev) {
		ret = -ENOMEM;
		goto err_unregister_shmem;
	}
	/* Add a software node with a "shmem" reference to the provider */
	ret = device_add_software_node(&smc_pdev->dev, &smc_swnode);
	if (ret)
		goto err_put_smc;
	ret = platform_device_add(smc_pdev);
	if (ret)
		goto err_rm_smc_node;

	pr_info("scmi_smc_pdev: registered %s and %s\n",
		dev_name(&shmem_pdev->dev), dev_name(&smc_pdev->dev));
	return 0;

err_rm_smc_node:
	device_remove_software_node(&smc_pdev->dev);
err_put_smc:
	platform_device_put(smc_pdev);
err_unregister_shmem:
	platform_device_unregister(shmem_pdev);
	shmem_pdev = NULL;
	return ret;
err_rm_shmem_node:
	device_remove_software_node(&shmem_pdev->dev);
err_put_shmem:
	platform_device_put(shmem_pdev);
	return ret;
}

static void __exit scmi_smc_pdev_exit(void)
{
	if (smc_pdev) {
		platform_device_unregister(smc_pdev);
		smc_pdev = NULL;
	}
	if (shmem_pdev) {
		platform_device_unregister(shmem_pdev);
		shmem_pdev = NULL;
	}
	pr_info("scmi_smc_pdev: unregistered\n");
}

module_init(scmi_smc_pdev_init);
module_exit(scmi_smc_pdev_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Create arm-scmi-smc + shmem devices for ACPI SCMI-over-SMC testing");
