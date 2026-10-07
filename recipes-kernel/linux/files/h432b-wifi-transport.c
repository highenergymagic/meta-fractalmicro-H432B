// SPDX-License-Identifier: GPL-2.0-only
/*
 * H432B RTL8712 SDIO transport qualification, not a network driver.
 * Local window offset zero is TX_CTRL; offsets 1..3 are free-page counters.
 * Reference: Realtek rtl8712_sdio_regdef.h, vendor tree commit
 * 2237e98dacd8421b38beb2d1aad88aa2b9f79dd8.
 * Never access FIFO, interrupt status, efuse, firmware or RF registers here.
 */
#include <linux/device.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>

#define SAMPLE_BYTES 4

struct h432b_wifi_sample {
	struct mutex lock;
	bool attempted;
	int error;
	int cleanup_error;
	u8 before[SAMPLE_BYTES];
	u8 bulk[SAMPLE_BYTES];
	u8 after[SAMPLE_BYTES];
};

static int read_cmd52(struct sdio_func *func, u8 *bytes)
{
	int i, error = 0;

	for (i = 0; i < SAMPLE_BYTES; i++) {
		bytes[i] = sdio_readb(func, i, &error);
		if (error)
			return error;
	}
	return 0;
}

static void take_sample(struct sdio_func *func, struct h432b_wifi_sample *sample)
{
	int error, cleanup = 0;
	unsigned int saved_blksize = func->cur_blksize;
	u8 ioex;
	bool enable_attempted = false, change_block_attempted = false;

	sdio_claim_host(func);
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;

	/* Preserve firmware's function-enable state, including on failed enable. */
	if (!(ioex & BIT(func->num))) {
		enable_attempted = true;
		error = sdio_enable_func(func);
		if (error)
			goto restore;
	}
	/* Do not modify a block-size setting that cannot be restored. */
	if (!saved_blksize) {
		error = -EINVAL;
		goto restore;
	}
	change_block_attempted = true;
	error = sdio_set_block_size(func, 512);
	if (error)
		goto restore;
	error = read_cmd52(func, sample->before);
	if (error)
		goto restore;
	/* Embedded in a kmalloc allocation: no stack buffer supplied to DMA. */
	error = sdio_memcpy_fromio(func, sample->bulk, 0, SAMPLE_BYTES);
	if (error)
		goto restore;
	error = read_cmd52(func, sample->after);
restore:
	if (change_block_attempted)
		cleanup = sdio_set_block_size(func, saved_blksize);
	if (enable_attempted) {
		int disable_error = sdio_disable_func(func);

		if (!cleanup)
			cleanup = disable_error;
	}
out:
	sdio_release_host(func);
	sample->error = error;
	sample->cleanup_error = cleanup;
	dev_info(&func->dev, "transport sample complete: error=%d cleanup=%d\n",
		 error, cleanup);
}

static ssize_t sample_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&sample->lock);
	if (sample->attempted) {
		mutex_unlock(&sample->lock);
		return -EALREADY;
	}
	sample->attempted = true;
	take_sample(func, sample);
	mutex_unlock(&sample->lock);
	return count;
}
static DEVICE_ATTR_WO(sample);

static ssize_t result_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(dev_to_sdio_func(dev));
	ssize_t size;

	mutex_lock(&sample->lock);
	if (!sample->attempted)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d cleanup=%d cmd52_before=%*ph cmd53=%*ph cmd52_after=%*ph\n",
			sample->error, sample->cleanup_error,
			SAMPLE_BYTES, sample->before, SAMPLE_BYTES, sample->bulk,
			SAMPLE_BYTES, sample->after);
	mutex_unlock(&sample->lock);
	return size;
}
static DEVICE_ATTR_RO(result);

static struct attribute *h432b_wifi_attrs[] = {
	&dev_attr_sample.attr,
	&dev_attr_result.attr,
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
	dev_info(&func->dev, "transport test bound; awaiting explicit sample request\n");
	return device_add_group(&func->dev, &h432b_wifi_group);
}

static void h432b_wifi_remove(struct sdio_func *func)
{
	/* Removing sysfs waits for any active sample before devres frees data. */
	device_remove_group(&func->dev, &h432b_wifi_group);
}

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
};
module_sdio_driver(h432b_wifi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H432B RTL8712 explicit SDIO transport qualification");
