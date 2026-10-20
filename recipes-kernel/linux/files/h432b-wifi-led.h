/* SPDX-License-Identifier: GPL-2.0-only */
/* RTL8712 LED pins; board indicator names and colours are not established. */
#define WIFI_LED_CFG 0x2f2

struct h432b_wifi_led {
	struct led_classdev cdev;
	struct sdio_func *func;
	struct h432b_wifi_sample *owner;
	unsigned int pin;
	enum led_brightness requested;
	bool configured;
};

/* Caller holds the owner mutex and SDIO host. */
static int wifi_led_write(struct h432b_wifi_led *led,
			  enum led_brightness brightness)
{
	unsigned int shift = led->pin * 4;
	u8 before, value, after;
	int error = 0;

	before = wifi_read(led->func, 1, WIFI_LED_CFG, &error);
	if (error)
		return error;
	/* Software drive is active-low. Preserve the other LED's whole nibble. */
	value = (before & ~(0xf << shift)) |
		(brightness == LED_OFF ? 0x8 << shift : 0);
	wifi_write(led->func, 1, WIFI_LED_CFG, value, &error);
	if (error)
		return error;
	after = wifi_read(led->func, 1, WIFI_LED_CFG, &error);
	if (!error && after != value)
		error = -EIO;
	return error;
}

static int wifi_led_set(struct led_classdev *cdev,
			enum led_brightness brightness)
{
	struct h432b_wifi_led *led =
		container_of(cdev, struct h432b_wifi_led, cdev);
	struct h432b_wifi_sample *owner = led->owner;
	int error = 0;

	mutex_lock(&owner->lock);
	led->requested = brightness;
	led->configured = true;
	/* Cache requests while SDIO is unavailable, including LED-core PM calls. */
	if (!owner->net || !netif_running(owner->net->dev) ||
	    READ_ONCE(owner->net->stopping) ||
	    READ_ONCE(owner->net->faulted) || owner->radio_off)
		goto unlock;
	sdio_claim_host(led->func);
	error = wifi_led_write(led, brightness);
	sdio_release_host(led->func);
unlock:
	mutex_unlock(&owner->lock);
	return error;
}

/*
 * Replay only explicitly configured outputs after the radio is initialized.
 * Caller holds the owner mutex and SDIO host; LED callbacks cannot race us.
 */
static int wifi_led_restore(struct h432b_wifi_sample *owner)
{
	unsigned int i;
	int error;

	if (!owner->leds)
		return 0;
	for (i = 0; i < 2; i++) {
		struct h432b_wifi_led *led = &owner->leds[i];

		if (!led->configured)
			continue;
		error = wifi_led_write(led, led->requested);
		if (error)
			return error;
	}
	return 0;
}

static void wifi_led_unregister(struct h432b_wifi_sample *owner)
{
	unsigned int i;

	if (!owner->leds)
		return;
	for (i = 0; i < 2; i++)
		led_classdev_unregister(&owner->leds[i].cdev);
	mutex_lock(&owner->lock);
	kfree(owner->leds);
	owner->leds = NULL;
	mutex_unlock(&owner->lock);
}

static void wifi_led_register(struct sdio_func *func,
			      struct h432b_wifi_sample *owner)
{
	static const char * const names[] = { "rtl8712::led0", "rtl8712::led1" };
	struct h432b_wifi_led *leds;
	unsigned int i;
	int error;

	leds = kcalloc(2, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return;
	for (i = 0; i < 2; i++) {
		leds[i].func = func;
		leds[i].owner = owner;
		leds[i].pin = i;
		leds[i].cdev.name = names[i];
		leds[i].cdev.max_brightness = 1;
		leds[i].cdev.brightness_set_blocking = wifi_led_set;
		leds[i].cdev.flags = LED_CORE_SUSPENDRESUME;
		error = led_classdev_register(&func->dev, &leds[i].cdev);
		if (error)
			goto failed;
	}
	mutex_lock(&owner->lock);
	owner->leds = leds;
	mutex_unlock(&owner->lock);
	return;
failed:
	while (i--)
		led_classdev_unregister(&leds[i].cdev);
	kfree(leds);
	dev_warn(&func->dev, "LED class registration failed: %d\n", error);
}
