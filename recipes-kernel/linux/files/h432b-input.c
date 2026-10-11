// SPDX-License-Identifier: GPL-2.0-only
/* H432B physically qualified matrix and direct inputs. No lock policy. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/workqueue.h>

struct h432_sample {
	u16 row[3];
	u8 direct, selector;
};

struct h432_input {
	struct gpio_descs *rows, *columns, *direct, *selectors;
	struct input_dev *keys, *routing, *switches;
	struct delayed_work work;
	struct h432_sample candidate;
	bool valid;
};

static const unsigned short matrix_codes[15] = {
	KEY_BACKSPACE, KEY_BRL_DOT3, KEY_BRL_DOT2, KEY_BRL_DOT1,
	KEY_SPACE, KEY_BRL_DOT4, KEY_BRL_DOT5, KEY_BRL_DOT6,
	KEY_ENTER, KEY_F1, KEY_F2, KEY_F3, KEY_F4,
	BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY3
};

static const unsigned short direct_codes[7] = {
	BTN_0, BTN_1, BTN_2, BTN_3, BTN_4,
	BTN_TRIGGER_HAPPY2, BTN_TRIGGER_HAPPY4
};

static void idle_rows(struct h432_input *h)
{
	int i;

	for (i = 0; i < 3; i++)
		gpiod_set_value_cansleep(h->rows->desc[i], 0);
}

static int read_bits(struct gpio_descs *gpios, unsigned int *bits)
{
	int i, value;

	*bits = 0;
	for (i = 0; i < gpios->ndescs; i++) {
		value = gpiod_get_value_cansleep(gpios->desc[i]);
		if (value < 0)
			return value;
		*bits |= value << i;
	}
	return 0;
}

static int scan(struct h432_input *h, struct h432_sample *s)
{
	unsigned int bits;
	int row, ret;

	for (row = 2; row >= 0; row--) {
		/* Active-low descriptors: logical 1 selects, 0 releases. */
		gpiod_set_value_cansleep(h->rows->desc[row], 1);
		usleep_range(50, 100);
		ret = read_bits(h->columns, &bits);
		gpiod_set_value_cansleep(h->rows->desc[row], 0);
		if (ret)
			return ret;
		s->row[row] = bits;
	}
	ret = read_bits(h->direct, &bits);
	if (ret)
		return ret;
	s->direct = bits;
	ret = read_bits(h->selectors, &bits);
	s->selector = bits;
	return ret;
}

static bool same(const struct h432_sample *a, const struct h432_sample *b)
{
	return a->row[0] == b->row[0] && a->row[1] == b->row[1] &&
	       a->row[2] == b->row[2] && a->direct == b->direct &&
	       a->selector == b->selector;
}

static void report_keys(struct h432_input *h, const struct h432_sample *s)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(matrix_codes); i++)
		input_report_key(h->keys, matrix_codes[i], !!(s->row[2] & BIT(i)));
	for (i = 0; i < ARRAY_SIZE(direct_codes); i++)
		input_report_key(h->keys, direct_codes[i], !!(s->direct & BIT(i)));
	for (i = 0; i < 32; i++)
		input_report_key(h->routing, BTN_TRIGGER_HAPPY1 + i,
				 !!(s->row[i / 16] & BIT(i % 16)));
	input_sync(h->keys);
	input_sync(h->routing);
}

static void poll_work(struct work_struct *work)
{
	struct h432_input *h = container_of(to_delayed_work(work), struct h432_input, work);
	struct h432_sample s = {};
	unsigned int front, lock;
	int ret;

	ret = scan(h, &s);
	if (ret) {
		/* Never leave a held key stuck after a transport failure. */
		struct h432_sample released = {};

		dev_err_ratelimited(h->keys->dev.parent, "key scan failed: %d\n", ret);
		report_keys(h, &released);
		h->valid = false;
		goto next;
	}
	if (h->valid && same(&s, &h->candidate)) {
		report_keys(h, &s);
		front = s.selector & 3;
		lock = (s.selector >> 2) & 3;
		/* Contact gaps (00) are not a fourth switch position. */
		if (front)
			input_report_abs(h->switches, ABS_MISC,
					 front == 1 ? 0 : front == 3 ? 1 : 2);
		if (lock)
			input_report_abs(h->switches, ABS_RZ,
					 lock == 1 ? 0 : lock == 3 ? 1 : 2);
		input_sync(h->switches);
	}
	h->candidate = s;
	h->valid = true;
next:
	schedule_delayed_work(&h->work, msecs_to_jiffies(10));
}

static void stop(void *data)
{
	struct h432_input *h = data;
	struct h432_sample released = {};

	cancel_delayed_work_sync(&h->work);
	idle_rows(h);
	report_keys(h, &released);
	h->valid = false;
}

static struct input_dev *new_input(struct device *dev, const char *name)
{
	struct input_dev *input = devm_input_allocate_device(dev);

	if (input) {
		input->name = name;
		input->id.bustype = BUS_HOST;
		input->dev.parent = dev;
	}
	return input;
}

static int probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h432_input *h;
	int i, ret;

	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	/* Inputs first, then scan outputs initialized electrically high. */
	h->columns = devm_gpiod_get_array(dev, "column", GPIOD_IN);
	if (IS_ERR(h->columns))
		return dev_err_probe(dev, PTR_ERR(h->columns), "columns\n");
	h->direct = devm_gpiod_get_array(dev, "direct", GPIOD_IN);
	if (IS_ERR(h->direct))
		return dev_err_probe(dev, PTR_ERR(h->direct), "direct inputs\n");
	h->selectors = devm_gpiod_get_array(dev, "selector", GPIOD_IN);
	if (IS_ERR(h->selectors))
		return dev_err_probe(dev, PTR_ERR(h->selectors), "selectors\n");
	h->rows = devm_gpiod_get_array(dev, "row", GPIOD_OUT_LOW);
	if (IS_ERR(h->rows))
		return dev_err_probe(dev, PTR_ERR(h->rows), "rows\n");
	if (h->rows->ndescs != 3 || h->columns->ndescs != 16 ||
	    h->direct->ndescs != 7 || h->selectors->ndescs != 4)
		return -EINVAL;
	h->keys = new_input(dev, "H432B keyboard and controls");
	h->routing = new_input(dev, "H432B braille routing");
	h->switches = new_input(dev, "H432B selectors");
	if (!h->keys || !h->routing || !h->switches)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(matrix_codes); i++)
		input_set_capability(h->keys, EV_KEY, matrix_codes[i]);
	for (i = 0; i < ARRAY_SIZE(direct_codes); i++)
		input_set_capability(h->keys, EV_KEY, direct_codes[i]);
	for (i = 0; i < 32; i++)
		input_set_capability(h->routing, EV_KEY, BTN_TRIGGER_HAPPY1 + i);
	input_set_abs_params(h->switches, ABS_MISC, 0, 2, 0, 0);
	input_set_abs_params(h->switches, ABS_RZ, 0, 2, 0, 0);
	ret = input_register_device(h->keys);
	if (ret)
		return ret;
	ret = input_register_device(h->routing);
	if (ret)
		return ret;
	ret = input_register_device(h->switches);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, h);
	INIT_DELAYED_WORK(&h->work, poll_work);
	ret = devm_add_action_or_reset(dev, stop, h);
	if (ret)
		return ret;
	schedule_delayed_work(&h->work, 0);
	return 0;
}

static int suspend(struct device *dev)
{
	stop(dev_get_drvdata(dev));
	return 0;
}

static int resume(struct device *dev)
{
	struct h432_input *h = dev_get_drvdata(dev);

	schedule_delayed_work(&h->work, 0);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pm_ops, suspend, resume);

static void shutdown(struct platform_device *pdev)
{
	stop(platform_get_drvdata(pdev));
}

static const struct of_device_id matches[] = {
	{ .compatible = "hims,braillesense-u2-keys" }, {}
};
MODULE_DEVICE_TABLE(of, matches);
static struct platform_driver h432_input_driver = {
	.probe = probe,
	.shutdown = shutdown,
	.driver = {
		.name = "h432b-input",
		.of_match_table = matches,
		.pm = pm_sleep_ptr(&pm_ops),
	},
};
module_platform_driver(h432_input_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H432B matrix, routing keys and physical selectors");
