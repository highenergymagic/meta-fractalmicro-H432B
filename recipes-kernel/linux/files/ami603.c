// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aichi Steel AMI603 six-axis sensor, direct-mode IIO interface.
 * Register protocol: AMI603 Specifications Ver.111207C.
 * Raw axes are sensor-package axes, not a calibrated compass heading.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/unaligned.h>

#define AMI603_DATA       0x06
#define AMI603_STATUS     0x18
#define AMI603_CTRL1      0x1b
#define AMI603_CTRL2      0x1c
#define AMI603_CTRL3      0x1d
#define AMI603_PAGE       0x3f
#define AMI603_ACCEL_CTRL 0xb4
#define AMI603_WIA        0xba
#define AMI603_ID         0x45

struct ami603 {
	struct i2c_client *client;
	struct gpio_desc *enable;
	struct mutex lock;
	bool powered;
	u16 sensitivity[6];
	s16 origin[3];
};

/* TYPE3/4 registers must be read as complete little-endian words. */
static int ami603_read(struct ami603 *s, u8 reg, void *buf, u16 len)
{
	struct i2c_msg msgs[] = {
		{ .addr = s->client->addr, .len = 1, .buf = &reg },
		{ .addr = s->client->addr, .flags = I2C_M_RD,
		  .len = len, .buf = buf },
	};
	int ret = i2c_transfer(s->client->adapter, msgs, ARRAY_SIZE(msgs));

	return ret == ARRAY_SIZE(msgs) ? 0 : ret < 0 ? ret : -EIO;
}

static int ami603_write(struct ami603 *s, u8 reg, u8 val)
{
	u8 buf[] = { reg, val };
	int ret = i2c_master_send(s->client, buf, sizeof(buf));

	return ret == sizeof(buf) ? 0 : ret < 0 ? ret : -EIO;
}

static int ami603_update(struct ami603 *s, u8 reg, u8 mask, u8 val)
{
	u8 old;
	int ret = ami603_read(s, reg, &old, 1);

	return ret ?: ami603_write(s, reg, (old & ~mask) | val);
}

static int ami603_standby(struct ami603 *s)
{
	int first, ret;

	first = ami603_update(s, AMI603_ACCEL_CTRL, 1, 0);
	ret = ami603_write(s, AMI603_CTRL3, 0);
	if (!first)
		first = ret;
	ret = ami603_write(s, AMI603_CTRL1, 0x40);
	return first ?: ret;
}

static int ami603_power_on(struct ami603 *s)
{
	u8 id;
	int ret;

	gpiod_set_value_cansleep(s->enable, 1);
	/* Datasheet requires at least 300 us after supply application. */
	usleep_range(1000, 2000);
	ret = ami603_read(s, AMI603_WIA, &id, 1);
	if (!ret && id != AMI603_ID)
		ret = -ENODEV;
	if (!ret)
		ret = ami603_standby(s);
	if (ret) {
		gpiod_set_value_cansleep(s->enable, 0);
		return ret;
	}
	s->powered = true;
	return 0;
}

static void ami603_power_off(void *arg)
{
	struct ami603 *s = arg;

	gpiod_set_value_cansleep(s->enable, 0);
	s->powered = false;
}

/* OTP is read-only. Always leave the OTP bank, including on transfer failure. */
static int ami603_parameters(struct ami603 *s)
{
	static const u8 sens_regs[] = { 0xca, 0xcc, 0xce, 0xb4, 0xb6, 0xb8 };
	u8 word[2];
	int i, ret, restore;

	ret = ami603_write(s, AMI603_PAGE, 15);
	if (ret)
		goto restore;
	for (i = 0; i < ARRAY_SIZE(sens_regs); i++) {
		ret = ami603_read(s, sens_regs[i], word, 2);
		if (ret)
			goto restore;
		s->sensitivity[i] = get_unaligned_le16(word);
		if (!s->sensitivity[i] || s->sensitivity[i] == 0xffff) {
			ret = -EINVAL;
			goto restore;
		}
	}
	for (i = 0; i < 3; i++) {
		ret = ami603_read(s, 0xc4 + 2 * i, word, 2);
		if (ret)
			goto restore;
		s->origin[i] = (s16)get_unaligned_le16(word);
	}
restore:
	restore = ami603_write(s, AMI603_PAGE, 0);
	return ret ?: restore;
}

static int ami603_sample(struct ami603 *s, unsigned int axis, int *value)
{
	u8 data[12], status;
	int ret, stop, i;

	if (!s->powered)
		return -EBUSY;
	ret = ami603_write(s, AMI603_CTRL1, 0xc0);
	if (ret)
		goto standby;
	/* Preserve reserved bits; enable active-high data-ready. No IRQ wake. */
	ret = ami603_update(s, AMI603_CTRL2, 0xc0, 0xc0);
	if (ret)
		goto standby;
	ret = ami603_update(s, AMI603_ACCEL_CTRL, 1, 1);
	if (ret)
		goto standby;
	usleep_range(10000, 12000);
	/* Clear any previous ready indication before requesting a fresh sample. */
	ret = ami603_read(s, AMI603_DATA, data, sizeof(data));
	if (ret)
		goto standby;
	ret = ami603_write(s, AMI603_CTRL3, 0x60);
	if (ret)
		goto standby;
	for (i = 0; i < 100; i++) {
		ret = ami603_read(s, AMI603_STATUS, &status, 1);
		if (ret || (status & BIT(6)))
			break;
		usleep_range(1000, 2000);
	}
	if (!ret && !(status & BIT(6)))
		ret = -ETIMEDOUT;
	if (!ret)
		ret = ami603_read(s, AMI603_DATA, data, sizeof(data));
	if (!ret)
		*value = (s16)get_unaligned_le16(data + 2 * axis);
standby:
	stop = ami603_standby(s);
	return ret ?: stop;
}

static int ami603_read_raw(struct iio_dev *indio,
			  const struct iio_chan_spec *chan,
			  int *val, int *val2, long mask)
{
	struct ami603 *s = iio_priv(indio);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&s->lock);
		ret = ami603_sample(s, chan->address, val);
		mutex_unlock(&s->lock);
		return ret ?: IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_ACCEL) {
			/* Factory sensitivity is counts per 2 g, IIO uses m/s^2. */
			*val = 196133;
			*val2 = 10000 * (int)s->sensitivity[chan->address];
		} else {
			/* Factory sensitivity is counts per 0.1 mT = 1 gauss. */
			*val = 1;
			*val2 = s->sensitivity[chan->address];
		}
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_OFFSET:
		*val = -s->origin[chan->address];
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

#define AMI603_CHANNEL(_type, _axis, _index, _extra) { \
	.type = _type, .modified = 1, .channel2 = IIO_MOD_##_axis, \
	.address = _index, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE) | (_extra), \
}
static const struct iio_chan_spec ami603_channels[] = {
	AMI603_CHANNEL(IIO_ACCEL, X, 0, BIT(IIO_CHAN_INFO_OFFSET)),
	AMI603_CHANNEL(IIO_ACCEL, Y, 1, BIT(IIO_CHAN_INFO_OFFSET)),
	AMI603_CHANNEL(IIO_ACCEL, Z, 2, BIT(IIO_CHAN_INFO_OFFSET)),
	AMI603_CHANNEL(IIO_MAGN, X, 3, 0),
	AMI603_CHANNEL(IIO_MAGN, Y, 4, 0),
	AMI603_CHANNEL(IIO_MAGN, Z, 5, 0),
};

static const struct iio_info ami603_info = {
	.read_raw = ami603_read_raw,
};

static int ami603_probe(struct i2c_client *client)
{
	struct iio_dev *indio;
	struct ami603 *s;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	indio = devm_iio_device_alloc(&client->dev, sizeof(*s));
	if (!indio)
		return -ENOMEM;
	s = iio_priv(indio);
	s->client = client;
	mutex_init(&s->lock);
	s->enable = devm_gpiod_get(&client->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(s->enable))
		return dev_err_probe(&client->dev, PTR_ERR(s->enable),
				     "cannot acquire sensor supply\n");
	/* Datasheet requires >300 ms off before a fresh power-on. */
	msleep(310);
	ret = ami603_power_on(s);
	if (ret)
		return dev_err_probe(&client->dev, ret, "sensor identification failed\n");
	ret = devm_add_action_or_reset(&client->dev, ami603_power_off, s);
	if (ret)
		return ret;
	ret = ami603_parameters(s);
	if (ret)
		return dev_err_probe(&client->dev, ret, "invalid factory calibration\n");
	indio->name = "ami603";
	indio->info = &ami603_info;
	indio->modes = INDIO_DIRECT_MODE;
	indio->channels = ami603_channels;
	indio->num_channels = ARRAY_SIZE(ami603_channels);
	i2c_set_clientdata(client, indio);
	return devm_iio_device_register(&client->dev, indio);
}

static int ami603_suspend(struct device *dev)
{
	struct ami603 *s = iio_priv(dev_get_drvdata(dev));

	mutex_lock(&s->lock);
	ami603_power_off(s);
	mutex_unlock(&s->lock);
	return 0;
}

static int ami603_resume(struct device *dev)
{
	struct ami603 *s = iio_priv(dev_get_drvdata(dev));
	int ret;

	mutex_lock(&s->lock);
	/* Also safe for an aborted/very short suspend. */
	msleep(310);
	ret = ami603_power_on(s);
	mutex_unlock(&s->lock);
	return ret;
}
static DEFINE_SIMPLE_DEV_PM_OPS(ami603_pm, ami603_suspend, ami603_resume);

static const struct of_device_id ami603_of_match[] = {
	{ .compatible = "aichi,ami603" },
	{ }
};
MODULE_DEVICE_TABLE(of, ami603_of_match);

static struct i2c_driver ami603_driver = {
	.driver = {
		.name = "ami603",
		.of_match_table = ami603_of_match,
		.pm = pm_sleep_ptr(&ami603_pm),
	},
	.probe = ami603_probe,
};
module_i2c_driver(ami603_driver);
MODULE_DESCRIPTION("Aichi AMI603 magnetometer and accelerometer");
MODULE_LICENSE("GPL");
