// SPDX-License-Identifier: GPL-2.0-only
/* H432B board support included by the Si470x I2C driver. */
struct h432b_fm_power {
	struct gpio_desc *enable, *reset, *mode;
};

static void h432b_fm_off(void *data)
{
	struct h432b_fm_power *fm = data;

	gpiod_set_value_cansleep(fm->reset, 1);
	gpiod_set_value_cansleep(fm->enable, 0);
}

static int h432b_fm_stc(struct si470x_device *radio, bool high)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(1000);
	int ret;

	do {
		ret = si470x_get_register(radio, STATUSRSSI);
		if (ret)
			return ret;
		if (!!(radio->registers[STATUSRSSI] & STATUSRSSI_STC) == high)
			return 0;
		msleep(10);
	} while (time_before(jiffies, deadline));
	return -ETIMEDOUT;
}

static int h432b_fm_tune(struct si470x_device *radio, unsigned short chan)
{
	int ret, cleanup;

	/* Clear a previous operation and wait for STC low before starting. */
	radio->registers[CHANNEL] &= ~CHANNEL_TUNE;
	ret = si470x_set_register(radio, CHANNEL);
	if (ret)
		return ret;
	ret = h432b_fm_stc(radio, false);
	if (ret)
		return ret;
	radio->registers[CHANNEL] =
		(radio->registers[CHANNEL] & ~CHANNEL_CHAN) | CHANNEL_TUNE | chan;
	ret = si470x_set_register(radio, CHANNEL);
	if (!ret)
		ret = h432b_fm_stc(radio, true);
	radio->registers[CHANNEL] &= ~CHANNEL_TUNE;
	cleanup = si470x_set_register(radio, CHANNEL);
	if (!cleanup)
		cleanup = h432b_fm_stc(radio, false);
	return ret ? ret : cleanup;
}

static int h432b_fm_start(struct si470x_device *radio)
{
	int ret;
	struct v4l2_ctrl *mute = v4l2_ctrl_find(&radio->hdl, V4L2_CID_AUDIO_MUTE);
	struct v4l2_ctrl *volume = v4l2_ctrl_find(&radio->hdl, V4L2_CID_AUDIO_VOLUME);

	/* Respect the control's mute state on every open. No RDS on Si4702. */
	radio->registers[POWERCFG] = POWERCFG_ENABLE |
		(mute->val ? 0 : POWERCFG_DMUTE);
	ret = si470x_set_register(radio, POWERCFG);
	if (ret)
		return ret;
	msleep(110);
	/* 50 us de-emphasis, factory GPIO2 state, no interrupt/RDS enable. */
	radio->registers[SYSCONFIG1] = SYSCONFIG1_DE | 0x0004;
	radio->registers[SYSCONFIG2] = 0x1010 |
		((radio->band << 6) & SYSCONFIG2_BAND) | volume->val;
	ret = si470x_set_register(radio, SYSCONFIG2);
	if (ret)
		return ret;
	return h432b_fm_tune(radio, radio->registers[CHANNEL] & CHANNEL_CHAN);
}

static int h432b_fm_init(struct si470x_device *radio)
{
	struct device *dev = &radio->client->dev;
	struct h432b_fm_power *fm;
	int ret;

	fm = devm_kzalloc(dev, sizeof(*fm), GFP_KERNEL);
	if (!fm)
		return -ENOMEM;
	fm->enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(fm->enable))
		return dev_err_probe(dev, PTR_ERR(fm->enable), "enable GPIO\n");
	fm->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(fm->reset))
		return dev_err_probe(dev, PTR_ERR(fm->reset), "reset GPIO\n");
	fm->mode = devm_gpiod_get(dev, "mode", GPIOD_OUT_HIGH);
	if (IS_ERR(fm->mode))
		return dev_err_probe(dev, PTR_ERR(fm->mode), "mode GPIO\n");
	ret = devm_add_action_or_reset(dev, h432b_fm_off, fm);
	if (ret)
		return ret;
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(fm->enable, 1);
	msleep(10);
	gpiod_set_value_cansleep(fm->reset, 0);
	msleep(100);
	ret = si470x_get_all_registers(radio);
	if (ret)
		return ret;
	if (radio->registers[DEVICEID] != 0x1242 ||
	    radio->registers[SI_CHIPID] != 0x1000)
		return dev_err_probe(dev, -ENODEV, "Unqualified FM chip identity\n");
	dev_info(dev, "H432B Si4702 rev C reset identity verified\n");
	/* Preserve reserved reset values. Crystal must settle before ENABLE. */
	radio->registers[7] |= 0x8000;
	ret = si470x_set_register(radio, 7);
	if (ret)
		return ret;
	msleep(500);
	radio->start_board = h432b_fm_start;
	radio->tune_board = h432b_fm_tune;
	radio->band = 0; /* 87.5--108 MHz */
	radio->videodev.device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}
