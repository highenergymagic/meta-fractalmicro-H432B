/* SPDX-License-Identifier: GPL-2.0-only */
/* Firmware lives on systembase, so initialization runs after root handoff. */
static int wifi_runtime_power(struct sdio_func *func,
			      struct h432b_power_result *r, bool reset)
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
	if (reset) {
		/* A previous release may have cleared the callback before an I/O
		 * error left the CCCR enable bit set. The core handles a NULL
		 * callback without decrementing its IRQ-owner count again.
		 */
		error = sdio_release_irq(func);
		if (error)
			goto out;
		error = wifi_power_off(func);
		if (error)
			goto out;
	}
	error = wifi_power_sequence(func, r);
	if (!error && r->warm) {
		/* A previous binding may have left live firmware in device RAM. */
		error = wifi_power_off(func);
		if (!error) {
			memset(r, 0, sizeof(*r));
			r->attempted = true;
			error = wifi_power_sequence(func, r);
		}
	}
out:
	if (enabled)
		r->cleanup = sdio_disable_func(func);
	sdio_release_host(func);
	r->error = error;
	return error ? error : r->cleanup;
}

/* Owner lock held; all packet work stopped and the SDIO IRQ released. */
static int wifi_runtime_boot(struct sdio_func *func,
			     struct h432b_wifi_sample *owner, bool reset)
{
	struct device *dev = &func->dev;
	const struct firmware *fw = NULL;
	const char *stage = "firmware";
	u32 imem, emem;
	int error;

	if (owner->command.irq_owned || func->irq_handler)
		return -EBUSY;
	error = request_firmware_direct(&fw, WIFI_FW_NAME, dev);
	if (error)
		goto report;
	error = wifi_fw_validate(fw, &imem, &emem);
	if (error)
		goto report;

	owner->startup_attempted = true;
	memset(&owner->power, 0, sizeof(owner->power));
	memset(&owner->firmware, 0, sizeof(owner->firmware));
	memset(&owner->ack, 0, sizeof(owner->ack));
	memset(&owner->command, 0, sizeof(owner->command));
	init_completion(&owner->command.irq_done);
	stage = "power";
	error = wifi_runtime_power(func, &owner->power, reset);
	if (error)
		goto report;
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

	stage = "power acknowledgment";
	owner->ack.attempted = true;
	error = wifi_ack_test(func, &owner->ack, h432b_wifi_irq);
	owner->ack.error = error;
	if (!error)
		error = owner->ack.cleanup;
report:
	owner->startup_error = error;
	if (error)
		dev_err(dev, "%s failed: %d\n", stage, error);
	if (fw)
		release_firmware(fw);
	return error;
}

static int wifi_runtime_restart(struct h432b_wifi_net *net)
{
	u8 mac[ETH_ALEN];
	int error;

	error = wifi_runtime_boot(net->func, net->owner, true);
	if (!error)
		error = wifi_net_read_mac(net->func, net->owner, mac);
	if (!error && !ether_addr_equal(mac, net->dev->dev_addr))
		error = -EADDRNOTAVAIL;
	if (!error)
		wifi_net_stream_init(net);
	return error;
}

/* Called after packet work and IRQ ownership have ended. */
static int wifi_runtime_shutdown(struct sdio_func *func)
{
	bool enabled = false;
	u8 ioex;
	int error, cleanup;

	sdio_claim_host(func);
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;
	if (!(ioex & BIT(func->num))) {
		error = sdio_enable_func(func);
		if (error)
			goto out;
		enabled = true;
	}
	error = wifi_power_off(func);
	if (enabled) {
		cleanup = sdio_disable_func(func);
		if (!error)
			error = cleanup;
	}
out:
	sdio_release_host(func);
	return error;
}

static int wifi_runtime_initialize(struct sdio_func *func)
{
	struct device *dev = &func->dev;
	struct h432b_wifi_sample *owner = sdio_get_drvdata(func);
	bool registered = false;
	int error;

	mutex_lock(&owner->lock);
	if (owner->net) {
		error = owner->net->faulted ? -EIO : 0;
		goto unlock;
	}
	if (owner->attempted) {
		error = -EBUSY;
		goto unlock;
	}
	error = wifi_runtime_boot(func, owner, owner->startup_attempted);
	if (error)
		goto unlock;
	owner->command.attempted = true;
	error = wifi_net_register(func, owner);
	owner->command.error = error;
	registered = !error;
	if (error)
		dev_err(dev, "network registration failed: %d\n", error);
unlock:
	mutex_unlock(&owner->lock);
	if (registered)
		wifi_led_register(func, owner);
	return error;
}

#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS
static ssize_t initialize_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int error;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	error = wifi_runtime_initialize(dev_to_sdio_func(dev));
	return error ? error : count;
}
static DEVICE_ATTR_WO(initialize);
#endif
