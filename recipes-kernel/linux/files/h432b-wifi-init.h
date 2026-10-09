/* SPDX-License-Identifier: GPL-2.0-only */
/* Firmware lives on systembase, so initialization runs after root handoff. */
static int wifi_runtime_power(struct sdio_func *func,
			      struct h432b_power_result *r)
{
	bool enabled = false;
	u8 ioex;
	int error;

	r->attempted = true;
	sdio_claim_host(func);
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;
	if (!(ioex & BIT(func->num))) {
		enabled = true;
		error = sdio_enable_func(func);
		if (error)
			goto out;
	}
	error = wifi_power_sequence(func, r);
out:
	if (enabled)
		r->cleanup = sdio_disable_func(func);
	sdio_release_host(func);
	r->error = error;
	return error ? error : r->cleanup;
}

static ssize_t initialize_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct h432b_wifi_sample *owner = sdio_get_drvdata(func);
	const struct firmware *fw = NULL;
	const char *stage = "firmware";
	bool registered = false;
	u32 imem, emem;
	int error;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&owner->lock);
	if (owner->net) {
		error = owner->net->faulted ? -EIO : 0;
		goto unlock;
	}
	/* A failed hardware sequence needs a reset, not a partial replay. */
	if (owner->startup_attempted) {
		error = owner->startup_error;
		goto unlock;
	}
	if (owner->attempted) {
		error = -EBUSY;
		goto unlock;
	}
	error = request_firmware_direct(&fw, WIFI_FW_NAME, dev);
	if (error)
		goto report;
	error = wifi_fw_validate(fw, &imem, &emem);
	if (error)
		goto report;

	owner->startup_attempted = true;
	stage = "power";
	error = wifi_runtime_power(func, &owner->power);
	if (error)
		goto report;
	if (owner->power.warm) {
		error = -EOPNOTSUPP;
		goto report;
	}
	stage = "firmware upload";
	owner->firmware.attempted = true;
	owner->firmware.version = get_unaligned_le16(fw->data + 2);
	error = wifi_fw_memory(func, fw, &owner->firmware, imem, emem, true);
	owner->firmware.error = error;
	if (!error)
		error = owner->firmware.cleanup;
	if (!error && owner->firmware.stage != 12)
		error = -EPROTO;
	if (error)
		goto report;

	stage = "power acknowledgement";
	owner->ack.attempted = true;
	error = wifi_ack_test(func, &owner->ack, h432b_wifi_irq);
	owner->ack.error = error;
	if (!error)
		error = owner->ack.cleanup;
	if (error)
		goto report;

	stage = "network registration";
	owner->command.attempted = true;
	error = wifi_net_register(func, owner);
	owner->command.error = error;
	registered = !error;
report:
	owner->startup_error = error;
	if (error)
		dev_err(dev, "%s failed: %d\n", stage, error);
	if (fw)
		release_firmware(fw);
unlock:
	mutex_unlock(&owner->lock);
	if (registered)
		wifi_led_register(func, owner);
	return error ? error : count;
}
static DEVICE_ATTR_WO(initialize);
