// SPDX-License-Identifier: GPL-2.0-only
/*
 * H432B read-only battery telemetry, not a charger-control driver.
 * Polls verified capacity/status every five seconds through power_supply.
 * GPC0[4] high pulls the external 1-Wire bus low; low releases it.
 * GPC0[3] samples the bus. These are not one bidirectional GPIO.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include "h432b-battery-policy.h"

struct battery_sample {
	int error, ac, secondary, charge;
	u8 family, capacity;
};

struct h432b_battery_inventory {
	struct gpio_desc *pull_low, *rx, *charge, *usb, *ac;
	struct mutex transaction;
	raw_spinlock_t slot;
	struct power_supply *psy;
	struct delayed_work poll;
	struct battery_sample cached;
	unsigned long sampled;
	bool have_sample;
};

/* No sleeps or sleeping GPIO controllers are permitted inside a timed slot. */
static int battery_reset(struct h432b_battery_inventory *b)
{
	unsigned long flags;
	int present;

	if (!gpiod_get_value(b->rx))
		return -EBUSY;
	raw_spin_lock_irqsave(&b->slot, flags);
	gpiod_set_value(b->pull_low, 1);
	udelay(480);
	gpiod_set_value(b->pull_low, 0);
	udelay(70);
	present = !gpiod_get_value(b->rx);
	raw_spin_unlock_irqrestore(&b->slot, flags);
	usleep_range(410, 500);
	if (!present)
		return -ENODEV;
	return gpiod_get_value(b->rx) ? 0 : -EIO;
}

static void battery_write_byte(struct h432b_battery_inventory *b, u8 value)
{
	unsigned long flags;
	int bit;

	for (bit = 0; bit < 8; bit++, value >>= 1) {
		raw_spin_lock_irqsave(&b->slot, flags);
		gpiod_set_value(b->pull_low, 1);
		udelay(6);
		if (value & 1)
			gpiod_set_value(b->pull_low, 0);
		udelay(54);
		gpiod_set_value(b->pull_low, 0);
		udelay(10);
		raw_spin_unlock_irqrestore(&b->slot, flags);
	}
}

static u8 battery_read_byte(struct h432b_battery_inventory *b)
{
	unsigned long flags;
	u8 value = 0;
	int bit;

	for (bit = 0; bit < 8; bit++) {
		raw_spin_lock_irqsave(&b->slot, flags);
		gpiod_set_value(b->pull_low, 1);
		udelay(5);
		gpiod_set_value(b->pull_low, 0);
		udelay(8);
		if (gpiod_get_value(b->rx))
			value |= BIT(bit);
		udelay(55);
		raw_spin_unlock_irqrestore(&b->slot, flags);
	}
	return value;
}

static u8 battery_crc(const u8 *data, size_t len)
{
	u8 crc = 0;
	int bit;

	while (len--) {
		crc ^= *data++;
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0x8c : 0);
	}
	return crc;
}

static int battery_read_rom(struct h432b_battery_inventory *b, u8 rom[8])
{
	int ret, i;

	ret = battery_reset(b);
	if (ret)
		return ret;
	/* Stock firmware uses Skip ROM: this is a single-drop inventory. */
	battery_write_byte(b, 0x33);
	for (i = 0; i < 8; i++)
		rom[i] = battery_read_byte(b);
	if (!rom[0] || rom[0] == 0xff || battery_crc(rom, 8))
		return -EBADMSG;
	return 0;
}

static int battery_read_capacity(struct h432b_battery_inventory *b,
				const u8 rom[8], u8 *capacity)
{
	int ret, i;

	ret = battery_reset(b);
	if (ret)
		return ret;
	/* Select the CRC-checked device. No broadcast function commands. */
	battery_write_byte(b, 0x55);
	for (i = 0; i < 8; i++)
		battery_write_byte(b, rom[i]);
	battery_write_byte(b, 0x69); /* Read Data, not Write Data. */
	battery_write_byte(b, 0x06); /* Stock remaining active relative capacity. */
	*capacity = battery_read_byte(b);
	return *capacity <= 100 ? 0 : -ERANGE;
}

/* Caller holds transaction. No stored capacity is substituted on errors. */
static void battery_sample_read(struct h432b_battery_inventory *b,
				struct battery_sample *sample)
{
	u8 rom[8], again[8], capacity = 0, confirm = 0;
	int ret;

	memset(sample, 0, sizeof(*sample));
	sample->ac = gpiod_get_value(b->ac);
	sample->secondary = gpiod_get_value(b->usb);
	sample->charge = gpiod_get_value(b->charge);
	ret = battery_read_rom(b, rom);
	if (!ret)
		ret = battery_read_rom(b, again);
	if (!ret && memcmp(rom, again, sizeof(rom)))
		ret = -EBADMSG;
	if (!ret) {
		sample->family = rom[0];
		if (!(rom[0] == 0x32 || rom[0] == 0x3d))
			ret = -EOPNOTSUPP;
	}
	if (!ret)
		ret = battery_read_capacity(b, rom, &capacity);
	if (!ret)
		ret = battery_read_capacity(b, rom, &confirm);
	if (!ret && capacity != confirm)
		ret = -EAGAIN;
	gpiod_set_value(b->pull_low, 0);
	sample->error = ret;
	sample->capacity = capacity;
}

static void battery_poll(struct work_struct *work)
{
	struct h432b_battery_inventory *b =
		container_of(to_delayed_work(work), struct h432b_battery_inventory, poll);
	struct battery_sample next;
	bool changed;

	mutex_lock(&b->transaction);
	battery_sample_read(b, &next);
	changed = !b->have_sample || next.error != b->cached.error ||
		  next.capacity != b->cached.capacity || next.family != b->cached.family ||
		  next.ac != b->cached.ac || next.secondary != b->cached.secondary ||
		  next.charge != b->cached.charge;
	b->cached = next;
	b->sampled = jiffies;
	b->have_sample = true;
	mutex_unlock(&b->transaction);
	if (changed)
		power_supply_changed(b->psy);
	queue_delayed_work(system_power_efficient_wq, &b->poll, 5 * HZ);
}

static void battery_stop_poll(void *data)
{
	struct h432b_battery_inventory *b = data;

	cancel_delayed_work_sync(&b->poll);
	gpiod_set_value(b->pull_low, 0);
}

static enum power_supply_property battery_properties[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_STATUS,
};

static int battery_get_property(struct power_supply *psy,
				enum power_supply_property property,
				union power_supply_propval *value)
{
	struct h432b_battery_inventory *b = power_supply_get_drvdata(psy);
	struct battery_sample sample;
	bool fresh;
	enum h432b_charge_state state;

	mutex_lock(&b->transaction);
	sample = b->cached;
	fresh = b->have_sample && time_before(jiffies, b->sampled + 15 * HZ);
	mutex_unlock(&b->transaction);
	switch (property) {
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!h432b_capacity_usable(sample.error, sample.capacity, fresh))
			return -ENODATA;
		value->intval = sample.capacity;
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		state = h432b_charge_state(sample.error, sample.capacity, fresh,
					   sample.charge, sample.ac, sample.secondary);
		switch (state) {
		case H432B_CHARGING: value->intval = POWER_SUPPLY_STATUS_CHARGING; break;
		case H432B_DISCHARGING: value->intval = POWER_SUPPLY_STATUS_DISCHARGING; break;
		case H432B_NOT_CHARGING: value->intval = POWER_SUPPLY_STATUS_NOT_CHARGING; break;
		default: value->intval = POWER_SUPPLY_STATUS_UNKNOWN; break;
		}
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct power_supply_desc battery_supply = {
	.name = "h432b-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = battery_properties,
	.num_properties = ARRAY_SIZE(battery_properties),
	.get_property = battery_get_property,
};

static ssize_t snapshot_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct h432b_battery_inventory *b = dev_get_drvdata(dev);
	struct battery_sample sample;

	if (mutex_lock_interruptible(&b->transaction))
		return -ERESTARTSYS;
	battery_sample_read(b, &sample);
	mutex_unlock(&b->transaction);
	if (sample.error)
		return sysfs_emit(buf,
			"sample_error=%d ac_input=%d secondary_input=%d charging_input=%d\n",
			sample.error, sample.ac, sample.secondary, sample.charge);
	/* Deliberately do not export a pack's unique serial number. */
	return sysfs_emit(buf,
		"family=0x%02x rom_crc=ok capacity_percent=%u ac_input=%d secondary_input=%d charging_input=%d\n",
		sample.family, sample.capacity, sample.ac, sample.secondary, sample.charge);
}
static DEVICE_ATTR(snapshot, 0400, snapshot_show, NULL);
static struct attribute *battery_attrs[] = { &dev_attr_snapshot.attr, NULL };
ATTRIBUTE_GROUPS(battery);

static int battery_inventory_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h432b_battery_inventory *b;
	struct power_supply_config config = {};
	int ret;

	if (!of_machine_is_compatible("hims,braillesense-u2"))
		return -ENODEV;
	b = devm_kzalloc(dev, sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	b->rx = devm_gpiod_get(dev, "rx", GPIOD_IN);
	if (IS_ERR(b->rx))
		return dev_err_probe(dev, PTR_ERR(b->rx), "RX GPIO\n");
	b->charge = devm_gpiod_get(dev, "charging", GPIOD_IN);
	if (IS_ERR(b->charge))
		return dev_err_probe(dev, PTR_ERR(b->charge), "charging input\n");
	b->usb = devm_gpiod_get(dev, "secondary-present", GPIOD_IN);
	if (IS_ERR(b->usb))
		return dev_err_probe(dev, PTR_ERR(b->usb), "secondary power input\n");
	b->ac = devm_gpiod_get(dev, "ac-present", GPIOD_IN);
	if (IS_ERR(b->ac))
		return dev_err_probe(dev, PTR_ERR(b->ac), "AC input\n");
	/* Inverting external stage: physical LOW is released/idle. */
	b->pull_low = devm_gpiod_get(dev, "pull-low", GPIOD_OUT_LOW);
	if (IS_ERR(b->pull_low))
		return dev_err_probe(dev, PTR_ERR(b->pull_low), "TX GPIO\n");
	if (gpiod_cansleep(b->rx) || gpiod_cansleep(b->pull_low) ||
	    gpiod_cansleep(b->charge) || gpiod_cansleep(b->usb) ||
	    gpiod_cansleep(b->ac))
		return dev_err_probe(dev, -EINVAL, "requires on-SoC GPIOs\n");
	mutex_init(&b->transaction);
	raw_spin_lock_init(&b->slot);
	platform_set_drvdata(pdev, b);
	b->cached.error = -ENODATA;
	INIT_DELAYED_WORK(&b->poll, battery_poll);
	config.drv_data = b;
	config.of_node = dev->of_node;
	b->psy = devm_power_supply_register(dev, &battery_supply, &config);
	if (IS_ERR(b->psy))
		return dev_err_probe(dev, PTR_ERR(b->psy), "battery power_supply\n");
	/* Devres stops work before unregistering the supply or freeing GPIOs. */
	ret = devm_add_action_or_reset(dev, battery_stop_poll, b);
	if (ret)
		return ret;
	queue_delayed_work(system_power_efficient_wq, &b->poll, 0);
	dev_info(dev, "read-only power_supply ready; five-second telemetry polling\n");
	return 0;
}

static const struct of_device_id battery_inventory_of_match[] = {
	{ .compatible = "fractal,h432b-battery-inventory" },
	{ }
};
MODULE_DEVICE_TABLE(of, battery_inventory_of_match);
static struct platform_driver battery_inventory_driver = {
	.probe = battery_inventory_probe,
	.driver = {
		.name = "h432b-battery-inventory",
		.of_match_table = battery_inventory_of_match,
		.dev_groups = battery_groups,
	},
};
module_platform_driver(battery_inventory_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only H432B battery transport inventory");
