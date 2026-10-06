// SPDX-License-Identifier: GPL-2.0-only
/*
 * H432B battery transport inventory, not a charger or production fuel-gauge
 * driver. Read-only commands, on explicit root sysfs read only.
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

struct h432b_battery_inventory {
	struct gpio_desc *pull_low, *rx, *charge, *usb, *ac;
	struct mutex transaction;
	raw_spinlock_t slot;
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

static ssize_t snapshot_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct h432b_battery_inventory *b = dev_get_drvdata(dev);
	u8 rom[8], again[8], capacity = 0, confirm = 0;
	int ret, capret = -EOPNOTSUPP, ac, usb, charge;

	if (mutex_lock_interruptible(&b->transaction))
		return -ERESTARTSYS;
	ac = gpiod_get_value(b->ac);
	usb = gpiod_get_value(b->usb);
	charge = gpiod_get_value(b->charge);
	ret = battery_read_rom(b, rom);
	if (!ret)
		ret = battery_read_rom(b, again);
	if (!ret && memcmp(rom, again, sizeof(rom)))
		ret = -EBADMSG;
	/* These family codes support the stock register, but do not prove an
	 * exact chip model. In particular 0x32 is shared by multiple parts.
	 */
	if (!ret && (rom[0] == 0x32 || rom[0] == 0x3d)) {
		capret = battery_read_capacity(b, rom, &capacity);
		if (!capret)
			capret = battery_read_capacity(b, rom, &confirm);
		if (!capret && capacity != confirm)
			capret = -EAGAIN;
	}
	gpiod_set_value(b->pull_low, 0);
	mutex_unlock(&b->transaction);
	if (ret)
		return sysfs_emit(buf,
			"rom_error=%d ac_input=%d secondary_input=%d charging_input=%d\n",
			ret, ac, usb, charge);
	/* Deliberately do not export a pack's unique serial number. */
	if (capret)
		return sysfs_emit(buf,
			"family=0x%02x rom_crc=ok capacity_error=%d ac_input=%d secondary_input=%d charging_input=%d\n",
			rom[0], capret, ac, usb, charge);
	return sysfs_emit(buf,
		"family=0x%02x rom_crc=ok capacity_percent=%u ac_input=%d secondary_input=%d charging_input=%d\n",
		rom[0], capacity, ac, usb, charge);
}
static DEVICE_ATTR(snapshot, 0400, snapshot_show, NULL);
static struct attribute *battery_attrs[] = { &dev_attr_snapshot.attr, NULL };
ATTRIBUTE_GROUPS(battery);

static int battery_inventory_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h432b_battery_inventory *b;

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
	dev_info(dev, "read-only battery inventory ready; no automatic transactions\n");
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
