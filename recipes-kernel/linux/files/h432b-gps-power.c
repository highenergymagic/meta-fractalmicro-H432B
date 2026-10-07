// SPDX-License-Identifier: GPL-2.0-only
/* Board GPS power/reset sequencing; no runtime or system PM contract yet. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

struct h432b_gps {
    struct gpio_desc *reset;
    struct regulator *supply;
};

static void h432b_gps_off(void *data)
{
    struct h432b_gps *gps = data;
    gpiod_set_value_cansleep(gps->reset, 1);
    regulator_disable(gps->supply);
}

static int h432b_gps_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct h432b_gps *gps;
    int ret;

    gps = devm_kzalloc(dev, sizeof(*gps), GFP_KERNEL);
    if (!gps)
        return -ENOMEM;
    gps->supply = devm_regulator_get(dev, "vcc");
    if (IS_ERR(gps->supply))
        return dev_err_probe(dev, PTR_ERR(gps->supply), "GPS supply\n");
    gps->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(gps->reset))
        return dev_err_probe(dev, PTR_ERR(gps->reset), "GPS reset\n");
    ret = regulator_enable(gps->supply);
    if (ret)
        return ret;
    ret = devm_add_action_or_reset(dev, h432b_gps_off, gps);
    if (ret)
        return ret;
    fsleep(100000);
    gpiod_set_value_cansleep(gps->reset, 0);
    dev_info(dev, "GPS power on, 100 ms reset released\n");
    return 0;
}

static const struct of_device_id h432b_gps_match[] = {
    { .compatible = "fractal,h432b-gps-power" },
    { }
};
MODULE_DEVICE_TABLE(of, h432b_gps_match);

static struct platform_driver h432b_gps_driver = {
    .probe = h432b_gps_probe,
    .driver = {
        .name = "h432b-gps-power",
        .of_match_table = h432b_gps_match,
    },
};
module_platform_driver(h432b_gps_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H432B GPS power and reset");
