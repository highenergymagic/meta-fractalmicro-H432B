// SPDX-License-Identifier: GPL-2.0-only
/* H432B 32-cell GPIO shift display. Wire sequence recovered from stock driver.
 * Individual GPIO descriptors preserve adjacent keyboard and USB controls.
 * Power is inherited; no PMIC or undocumented rail sequencing is performed.
 */
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include "h432b-braille-wire.h"

struct h432_braille {
	struct miscdevice misc;
	struct gpio_desc *data, *clock, *latch, *enable;
	struct mutex lock;
	struct kref refs;
	bool opened, dead;
};
static void free_display(struct kref *ref)
{
	kfree(container_of(ref, struct h432_braille, refs));
}
static void shift_frame(struct h432_braille *h, const u8 cells[32])
{
	int cell, bit;
	gpiod_set_value_cansleep(h->latch, 0);
	for (cell = 31; cell >= 0; cell--) {
		u8 wire = h432_braille_wire(cells[cell]);
		for (bit = 7; bit >= 0; bit--) {
			gpiod_set_value_cansleep(h->data, !!(wire & BIT(bit)));
			udelay(1);
			gpiod_set_value_cansleep(h->clock, 1);
			udelay(1);
			gpiod_set_value_cansleep(h->clock, 0);
		}
	}
	gpiod_set_value_cansleep(h->latch, 1);
	udelay(1);
	gpiod_set_value_cansleep(h->latch, 0);
}
static int display_open(struct inode *inode, struct file *file)
{
	struct h432_braille *h = container_of(file->private_data,
					     struct h432_braille, misc);
	int ret = 0;
	mutex_lock(&h->lock);
	if (h->dead)
		ret = -ENODEV;
	else if (h->opened)
		ret = -EBUSY;
	else {
		h->opened = true;
		kref_get(&h->refs);
		file->private_data = h;
		nonseekable_open(inode, file);
	}
	mutex_unlock(&h->lock);
	return ret;
}
static int display_release(struct inode *inode, struct file *file)
{
	struct h432_braille *h = file->private_data;
	mutex_lock(&h->lock);
	h->opened = false;
	mutex_unlock(&h->lock);
	kref_put(&h->refs, free_display);
	return 0;
}
static ssize_t display_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *offset)
{
	struct h432_braille *h = file->private_data;
	u8 cells[32];
	int ret;
	if (count != sizeof(cells))
		return -EINVAL;
	if (copy_from_user(cells, buf, sizeof(cells)))
		return -EFAULT;
	ret = mutex_lock_interruptible(&h->lock);
	if (ret)
		return ret;
	if (h->dead)
		ret = -ENODEV;
	else {
		ret = gpiod_get_value_cansleep(h->enable);
		if (ret == 0)
			ret = -EHOSTDOWN;
		else if (ret > 0) {
			shift_frame(h, cells);
			ret = sizeof(cells);
		}
	}
	mutex_unlock(&h->lock);
	return ret;
}
static const struct file_operations display_fops = {
	.owner = THIS_MODULE,
	.open = display_open,
	.release = display_release,
	.write = display_write,
};
static int display_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h432_braille *h;
	int ret;
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	mutex_init(&h->lock);
	kref_init(&h->refs);
	h->enable = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(h->enable)) {
		ret = PTR_ERR(h->enable);
		goto fail;
	}
	/* Do not turn an unknown/off display on. EBOOT supplies the live state. */
	ret = gpiod_get_direction(h->enable);
	if (ret != 0) {
		ret = ret < 0 ? ret : -EHOSTDOWN;
		goto fail;
	}
	ret = gpiod_get_value_cansleep(h->enable);
	if (ret <= 0) {
		ret = ret < 0 ? ret : -EHOSTDOWN;
		goto fail;
	}
	h->latch = devm_gpiod_get(dev, "latch", GPIOD_OUT_LOW);
	if (IS_ERR(h->latch)) {
		ret = PTR_ERR(h->latch);
		goto fail;
	}
	h->clock = devm_gpiod_get(dev, "clock", GPIOD_OUT_LOW);
	if (IS_ERR(h->clock)) {
		ret = PTR_ERR(h->clock);
		goto fail;
	}
	h->data = devm_gpiod_get(dev, "data", GPIOD_OUT_LOW);
	if (IS_ERR(h->data)) {
		ret = PTR_ERR(h->data);
		goto fail;
	}
	h->misc.minor = MISC_DYNAMIC_MINOR;
	h->misc.name = "h432b-braille";
	h->misc.fops = &display_fops;
	h->misc.parent = dev;
	h->misc.mode = 0600;
	ret = misc_register(&h->misc);
	if (ret)
		goto fail;
	platform_set_drvdata(pdev, h);
	return 0;
fail:
	kref_put(&h->refs, free_display);
	return dev_err_probe(dev, ret, "display GPIO setup\n");
}
static void display_remove(struct platform_device *pdev)
{
	struct h432_braille *h = platform_get_drvdata(pdev);
	/* Stop new opens before removing the provider's lifetime reference. */
	misc_deregister(&h->misc);
	mutex_lock(&h->lock);
	h->dead = true;
	mutex_unlock(&h->lock);
	kref_put(&h->refs, free_display);
}
static const struct of_device_id display_matches[] = {
	{ .compatible = "fractal,h432b-braille" }, {}
};
MODULE_DEVICE_TABLE(of, display_matches);
static struct platform_driver display_driver = {
	.probe = display_probe,
	.remove = display_remove,
	.driver = {
		.name = "h432b-braille",
		.of_match_table = display_matches,
	},
};
module_platform_driver(display_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H432B 32-cell GPIO braille display");
