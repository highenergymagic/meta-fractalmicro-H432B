// SPDX-License-Identifier: GPL-2.0-only
/* H432B RTL8712 SDIO station driver. */
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "h432b-wifi-power.h"
#include "h432b-wifi-firmware.h"
#include "h432b-wifi-irq.h"
#include "h432b-wifi-events.h"
#include "h432b-wifi-command.h"

#define SAMPLE_BYTES 4

struct h432b_wifi_net;
struct h432b_wifi_led;

struct h432b_wifi_sample {
	struct h432b_wifi_led *leds;
	struct h432b_wifi_net *net;
	struct mutex lock;
	bool attempted;
	bool startup_attempted;
	int startup_error;
	bool resume_net;
	struct h432b_power_result power;
	struct h432b_fw_result firmware;
	struct h432b_ack_result ack;
	struct h432b_event_result event;
	struct h432b_command_result command;
	int error;
	int cleanup_error;
	u8 before[SAMPLE_BYTES];
	u8 bulk[SAMPLE_BYTES];
	u8 after[SAMPLE_BYTES];
};

static void wifi_net_irq_notify(struct h432b_wifi_net *net);

static void h432b_wifi_irq(struct sdio_func *func)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);

	if (sample->command.irq_mode) {
		wifi_command_irq(func, &sample->command);
		if (sample->command.irq_owned && sample->net)
			wifi_net_irq_notify(sample->net);
	}
	else
		wifi_ack_irq(func, &sample->ack);
}

#include "h432b-wifi-net.h"
#include "h432b-wifi-init.h"
#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS
#include "h432b-wifi-debug.h"
#endif

static struct attribute *h432b_wifi_attrs[] = {
	&dev_attr_initialize.attr,
	&dev_attr_network_result.attr,
#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS
	&dev_attr_network_start.attr,
	&dev_attr_command_test.attr,
	&dev_attr_command_result.attr,
	&dev_attr_event_read.attr,
	&dev_attr_event_result.attr,
	&dev_attr_power_ack.attr,
	&dev_attr_power_ack_result.attr,
	&dev_attr_sample.attr,
	&dev_attr_result.attr,
	&dev_attr_power_init.attr,
	&dev_attr_power_result.attr,
	&dev_attr_firmware_load.attr,
	&dev_attr_firmware_result.attr,
#endif
	NULL,
};
static const struct attribute_group h432b_wifi_group = {
	.attrs = h432b_wifi_attrs,
};

static int h432b_wifi_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct h432b_wifi_sample *sample;

	if (func->num != 1 ||
	    !of_machine_is_compatible("hims,braillesense-u2"))
		return -ENODEV;
	sample = devm_kzalloc(&func->dev, sizeof(*sample), GFP_KERNEL);
	if (!sample)
		return -ENOMEM;
	mutex_init(&sample->lock);
	sdio_set_drvdata(func, sample);
	return device_add_group(&func->dev, &h432b_wifi_group);
}

static void h432b_wifi_remove(struct sdio_func *func)
{
	/* Removing sysfs waits for any active sample before devres frees data. */
	device_remove_group(&func->dev, &h432b_wifi_group);
	wifi_led_unregister(sdio_get_drvdata(func));
	wifi_net_unregister(sdio_get_drvdata(func));
}

/*
 * cfg80211 suspends the child wiphy first and disconnects the station. Keep
 * firmware RAM powered, but stop traffic/work and release the SDIO IRQ. No
 * wake-on-WLAN is requested: the front power key is the sole wake source.
 */
static int h432b_wifi_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *owner = sdio_get_drvdata(func);
	struct h432b_wifi_net *net = owner->net;
	int error;

	error = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (error)
		return error;

	rtnl_lock();
	owner->resume_net = net && netif_running(net->dev);
	if (owner->resume_net) {
		netif_device_detach(net->dev);
		wifi_net_stop(net->dev);
		if (net->faulted) {
			error = net->last_error ? net->last_error : -EIO;
			owner->resume_net = false;
			netif_device_attach(net->dev);
		}
	} else if (owner->command.irq_owned) {
		/* A diagnostic IRQ owner cannot survive system sleep. */
		error = -EBUSY;
	}
	rtnl_unlock();
	return error;
}

static int h432b_wifi_resume(struct device *dev)
{
	struct h432b_wifi_sample *owner = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_wifi_net *net = owner->net;
	int error = 0;

	rtnl_lock();
	if (owner->resume_net && net && netif_running(net->dev)) {
		error = wifi_net_open(net->dev);
		if (!error)
			netif_device_attach(net->dev);
		else {
			net->last_error = error;
			WRITE_ONCE(net->faulted, true);
			dev_err(dev, "cannot restart station after sleep: %d\n", error);
		}
	}
	owner->resume_net = false;
	rtnl_unlock();
	return error;
}

static DEFINE_SIMPLE_DEV_PM_OPS(h432b_wifi_pm,
			       h432b_wifi_suspend, h432b_wifi_resume);

static const struct sdio_device_id h432b_wifi_ids[] = {
	{ SDIO_DEVICE(0x024c, 0x8712) },
	{ }
};
MODULE_DEVICE_TABLE(sdio, h432b_wifi_ids);

static struct sdio_driver h432b_wifi_driver = {
	.name = "h432b-wifi-transport",
	.id_table = h432b_wifi_ids,
	.probe = h432b_wifi_probe,
	.remove = h432b_wifi_remove,
	.drv.pm = pm_sleep_ptr(&h432b_wifi_pm),
};
module_sdio_driver(h432b_wifi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H432B RTL8712 SDIO station driver");
