// SPDX-License-Identifier: GPL-2.0-only
/*
 * H432B RTL8712 SDIO transport qualification, not a network driver.
 * Local window offset zero is TX_CTRL; offsets 1..3 are free-page counters.
 * Reference: Realtek rtl8712_sdio_regdef.h, vendor tree commit
 * 2237e98dacd8421b38beb2d1aad88aa2b9f79dd8.
 * Power initialization is a separate explicit one-shot action after sampling.
 * Firmware memory upload is separately requested; no IRQ or network interface.
 */
#include <linux/device.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "h432b-wifi-power.h"
#include "h432b-wifi-firmware.h"
#include "h432b-wifi-irq.h"
#include "h432b-wifi-events.h"
#include "h432b-wifi-command.h"

#define SAMPLE_BYTES 4

struct h432b_wifi_sample {
	struct mutex lock;
	bool attempted;
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

static ssize_t power_init_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);
	struct h432b_power_result *r = &sample->power;
	int error;
	u8 ioex;
	bool enable_attempted = false;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&sample->lock);
	if (r->attempted) {
		mutex_unlock(&sample->lock);
		return -EALREADY;
	}
	if (!sample->attempted || sample->error || sample->cleanup_error) {
		mutex_unlock(&sample->lock);
		return -EAGAIN;
	}
	r->attempted = true;
	sdio_claim_host(func);
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;
	if (!(ioex & BIT(func->num))) {
		enable_attempted = true;
		error = sdio_enable_func(func);
		if (error)
			goto out;
	}
	error = wifi_power_sequence(func, r);
out:
	if (enable_attempted)
		r->cleanup = sdio_disable_func(func);
	sdio_release_host(func);
	r->error = error;
	dev_info(dev, "power test: error=%d cleanup=%d warm=%d step=%u\n",
		 r->error, r->cleanup, r->warm, r->step);
	mutex_unlock(&sample->lock);
	/* Register changes persist; no unsupported rollback sequence is attempted. */
	return error ? error : r->cleanup ? r->cleanup : count;
}
static DEVICE_ATTR_WO(power_init);

static ssize_t power_result_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_power_result *r = &sample->power;
	ssize_t size;

	mutex_lock(&sample->lock);
	if (!r->attempted)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d cleanup=%d warm=%d step=%u before=%04x,%04x,%04x after=%04x,%04x,%04x command=%04x verify=%04x\n",
			r->error, r->cleanup, r->warm, r->step,
			r->before[0], r->before[1], r->before[2],
			r->after[0], r->after[1], r->after[2],
			r->command, r->verify);
	mutex_unlock(&sample->lock);
	return size;
}
static DEVICE_ATTR_RO(power_result);

static ssize_t firmware_load_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);
	struct h432b_fw_result *r = &sample->firmware;
	const struct firmware *fw;
	u32 imem, emem;
	int error;

	if (!sysfs_streq(buf, "memory") && !sysfs_streq(buf, "full"))
		return -EINVAL;
	mutex_lock(&sample->lock);
	if (r->attempted) {
		error = -EALREADY;
		goto unlock;
	}
	if (!sample->power.attempted || sample->power.error || sample->power.cleanup) {
		error = -EAGAIN;
		goto unlock;
	}
	/* No userspace fallback, network fetch or firmware embedded in the kernel. */
	error = request_firmware_direct(&fw, WIFI_FW_NAME, dev);
	if (error)
		goto unlock;
	error = wifi_fw_validate(fw, &imem, &emem);
	if (!error) {
		r->attempted = true;
		r->version = get_unaligned_le16(fw->data + 2);
		error = wifi_fw_memory(func, fw, r, imem, emem,
				       sysfs_streq(buf, "full"));
		r->error = error;
		dev_info(dev, "firmware memory: error=%d cleanup=%d stage=%u bytes=%u cpu=%04x\n",
			 error, r->cleanup, r->stage, r->bytes, r->cpu);
	}
	release_firmware(fw);
	if (!error)
		error = r->cleanup;
unlock:
	mutex_unlock(&sample->lock);
	if (error)
		return error;
	return count;
}
static DEVICE_ATTR_WO(firmware_load);

static ssize_t firmware_result_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_fw_result *r = &sample->firmware;
	ssize_t size;

	mutex_lock(&sample->lock);
	if (!r->attempted)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d cleanup=%d stage=%u bytes=%u packets=%u version=%04x initial=%04x imem=%04x emem=%04x cpu=%04x dmem=%04x ready=%04x boot_source=%02x\n",
			r->error, r->cleanup, r->stage, r->bytes, r->packets,
			r->version, r->initial, r->imem, r->emem, r->cpu,
			r->dmem, r->ready, r->boot_source);
	mutex_unlock(&sample->lock);
	return size;
}
static DEVICE_ATTR_RO(firmware_result);

static void h432b_wifi_irq(struct sdio_func *func)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);

	if (sample->command.irq_mode)
		wifi_command_irq(func, &sample->command);
	else
		wifi_ack_irq(func, &sample->ack);
}

static ssize_t power_ack_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);
	struct h432b_ack_result *r = &sample->ack;
	int error;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&sample->lock);
	if (r->attempted) {
		error = -EALREADY;
		goto out;
	}
	if (sample->firmware.stage != 12 || sample->firmware.error ||
	    sample->firmware.cleanup) {
		error = -EAGAIN;
		goto out;
	}
	r->attempted = true;
	error = wifi_ack_test(func, r, h432b_wifi_irq);
	r->error = error;
	if (!error)
		error = r->cleanup;
out:
	mutex_unlock(&sample->lock);
	return error ? error : count;
}
static DEVICE_ATTR_WO(power_ack);

static ssize_t power_ack_result_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_ack_result *r = &sample->ack;
	ssize_t size;

	mutex_lock(&sample->lock);
	if (!r->attempted)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d cleanup=%d irq_error=%d native=%d callbacks=%u saved_mask=%04x armed_mask=%04x status=%04x final_status=%04x old_request=%02x request=%02x before=%02x reply=%02x final_reply=%02x\n",
			r->error, r->cleanup, r->irq_error, r->native, r->callbacks,
			r->saved_mask, r->armed_mask, r->status, r->final_status,
			r->old_request, r->request, r->before, r->reply, r->final_reply);
	mutex_unlock(&sample->lock);
	return size;
}
static DEVICE_ATTR_RO(power_ack_result);

static ssize_t event_read_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);
	struct h432b_event_result *r = &sample->event;
	int error;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&sample->lock);
	if (r->attempted || sample->command.attempted) {
		error = -EALREADY;
		goto out;
	}
	if (sample->firmware.stage != 12 || sample->firmware.error ||
	    sample->firmware.cleanup || sample->power.warm) {
		error = -EAGAIN;
		goto out;
	}
	r->attempted = true;
	error = wifi_event_test(func, r, sample->firmware.c2h_base);
	r->error = error;
	if (!error)
		error = r->cleanup;
out:
	mutex_unlock(&sample->lock);
	return error ? error : count;
}
static DEVICE_ATTR_WO(event_read);

static ssize_t event_result_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_event_result *r = &sample->event;
	ssize_t size;

	mutex_lock(&sample->lock);
	if (!r->attempted)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d cleanup=%d baseline=%04x blocks=%04x after=%04x status=%04x final_status=%04x bytes=%u events=%u first_code=%u first_seq=%u first_length=%u head=%*ph\n",
			r->error, r->cleanup, r->baseline, r->blocks, r->after,
			r->status, r->final_status, r->bytes, r->events,
			r->first_code, r->first_seq, r->first_length, 64, r->head);
	mutex_unlock(&sample->lock);
	return size;
}
static DEVICE_ATTR_RO(event_result);

static ssize_t command_test_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *sample = sdio_get_drvdata(func);
	struct h432b_command_result *r = &sample->command;
	int error;

	if (!sysfs_streq(buf, "loopback") && !sysfs_streq(buf, "opmode") &&
	    !sysfs_streq(buf, "survey") && !sysfs_streq(buf, "opmode-irq") &&
	    !sysfs_streq(buf, "survey-irq") && !sysfs_streq(buf, "stress-irq") &&
	    !sysfs_streq(buf, "survey-repeat-irq"))
		return -EINVAL;
	mutex_lock(&sample->lock);
	if (r->attempted || sample->event.attempted) {
		error = -EALREADY;
		goto out;
	}
	if (sample->firmware.stage != 12 || sample->firmware.error ||
	    sample->firmware.cleanup || sample->power.warm ||
	    !sample->ack.attempted || sample->ack.error || sample->ack.cleanup) {
		error = -EAGAIN;
		goto out;
	}
	r->attempted = true;
	r->stress = sysfs_streq(buf, "stress-irq");
	r->survey_repeat = sysfs_streq(buf, "survey-repeat-irq");
	r->irq_mode = r->stress || r->survey_repeat ||
		      sysfs_streq(buf, "opmode-irq") || sysfs_streq(buf, "survey-irq");
	r->survey = r->survey_repeat || sysfs_streq(buf, "survey") ||
		    sysfs_streq(buf, "survey-irq");
	r->opmode = r->survey || r->irq_mode || sysfs_streq(buf, "opmode");
	error = wifi_command_test(func, r, sample->firmware.c2h_base, h432b_wifi_irq);
	r->error = error;
	if (!error)
		error = r->cleanup;
out:
	mutex_unlock(&sample->lock);
	return error ? error : count;
}
static DEVICE_ATTR_WO(command_test);

static ssize_t command_result_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct h432b_wifi_sample *sample = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_command_result *r = &sample->command;
	unsigned int i;
	ssize_t size;

	mutex_lock(&sample->lock);
	if (!r->attempted)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d cleanup=%d sent=%d matched=%d replies=%u reply_length=%u batches=%u events=%u debug=%u bytes=%u consumed=%04x port_seq=%u event_seq=%u pages=%u,%u status=%04x,%04x reply=%*ph debug_head=%*ph\n",
			r->error, r->cleanup, r->sent, r->matched, r->replies,
			r->reply_length, r->batches,
			r->events, r->debug_events, r->bytes, r->consumed,
			r->port_seq, r->event_seq, r->public_pages, r->command_pages,
			r->status_before, r->status_after, 28, r->reply, 32, r->debug_head);
	if (r->attempted)
		size += sysfs_emit_at(buf, size,
			"mac_stage=%u rcr=%08x,%08x cr=%08x,%08x pmc_status=%02x pause=%02x debug_select=%02x\n",
			r->mac_stage, r->mac_before[0], r->mac_after[0],
			r->mac_before[1], r->mac_after[1], r->pmc_after,
			r->pause_after, r->debug_after);
	if (r->attempted)
		size += sysfs_emit_at(buf, size,
			"commands_done=%u survey_runs=%u survey_total=%u\n",
			r->commands_done, r->survey_runs, r->survey_total);
	if (r->irq_mode)
		size += sysfs_emit_at(buf, size,
			"irq_native=%d irq_callbacks=%u irq_empty=%u irq_error=%d irq_status=%04x\n",
			r->irq_native, r->irq_callbacks, r->irq_empty,
			r->irq_error, r->irq_status);
	if (r->survey)
		size += sysfs_emit_at(buf, size,
			"survey_sent=%d survey_done=%d survey_events=%u survey_count=%u\n",
			r->scanning, r->survey_done, r->survey_events, r->survey_count);
	for (i = 0; i < r->snapshots; i++) {
		struct h432b_command_snapshot *s = &r->snapshot[i];

		size += sysfs_emit_at(buf, size,
			"phase=%u error=%d status=%04x rx=%04x c2h=%04x txctrl=%02x pages=%u,%u bus_errors=%*ph\n",
			s->phase, s->error, s->status, s->rx_blocks, s->c2h_blocks,
			s->tx_ctrl, s->public_pages, s->command_pages, 3, s->errors);
	}
	mutex_unlock(&sample->lock);
	return size;
}
static DEVICE_ATTR_RO(command_result);

static struct attribute *h432b_wifi_attrs[] = {
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
