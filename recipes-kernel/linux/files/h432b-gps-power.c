// SPDX-License-Identifier: GPL-2.0-only
/* Board GPS power/reset sequencing. Supply is retained across system sleep. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

struct h432b_gps {
	struct device *dev;
	struct gpio_desc *reset;
	struct regulator *supply;
	bool powered;
};

static void h432b_gps_off(void *data)
{
	struct h432b_gps *gps = data;
	int ret;

	gpiod_set_value_cansleep(gps->reset, 1);
	if (!gps->powered)
		return;
	ret = regulator_disable(gps->supply);
	if (ret)
		dev_err(gps->dev, "failed to disable GPS supply: %d\n", ret);
	else
		gps->powered = false;
}

static int h432b_gps_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h432b_gps *gps;
	int ret;

	gps = devm_kzalloc(dev, sizeof(*gps), GFP_KERNEL);
	if (!gps)
		return -ENOMEM;
	gps->dev = dev;
	gps->supply = devm_regulator_get(dev, "vcc");
	if (IS_ERR(gps->supply))
		return dev_err_probe(dev, PTR_ERR(gps->supply), "GPS supply\n");
	gps->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gps->reset))
		return dev_err_probe(dev, PTR_ERR(gps->reset), "GPS reset\n");
	ret = regulator_enable(gps->supply);
	if (ret)
		return dev_err_probe(dev, ret, "cannot enable GPS supply\n");
	gps->powered = true;
	ret = devm_add_action_or_reset(dev, h432b_gps_off, gps);
	if (ret)
		return ret;
	fsleep(100000);
	gpiod_set_value_cansleep(gps->reset, 0);
	platform_set_drvdata(pdev, gps);
	return 0;
}

static void h432b_gps_shutdown(struct platform_device *pdev)
{
	h432b_gps_off(platform_get_drvdata(pdev));
}

static const struct of_device_id h432b_gps_match[] = {
	{ .compatible = "hims,h432b-gps-power" },
	{ .compatible = "fractal,h432b-gps-power" },
	{ }
};
MODULE_DEVICE_TABLE(of, h432b_gps_match);

static struct platform_driver h432b_gps_driver = {
	.probe = h432b_gps_probe,
	.shutdown = h432b_gps_shutdown,
	.driver = {
		.name = "h432b-gps-power",
		.of_match_table = h432b_gps_match,
	},
};
module_platform_driver(h432b_gps_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H432B GPS power and reset");
