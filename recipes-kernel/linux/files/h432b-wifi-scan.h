/* SPDX-License-Identifier: GPL-2.0-only */
/* RTL8712S SiteSurvey parameters and regulatory-separated channel lists. */
#define WIFI_SCAN_PARAMETERS 84
#define WIFI_SCAN_CHANNELS 13

struct wifi_scan_plan {
	u8 channels[2][WIFI_SCAN_CHANNELS]; /* passive, then active */
	u8 count[2];
	u8 ssid[32];
	u8 ssid_length;
	bool active;
};

static int wifi_scan_plan_init(struct wifi_scan_plan *plan, bool active,
			       const u8 *ssid, unsigned int length)
{
	if (length > sizeof(plan->ssid) || (length && !ssid))
		return -EINVAL;
	memset(plan, 0, sizeof(*plan));
	plan->active = active;
	plan->ssid_length = length;
	if (length)
		memcpy(plan->ssid, ssid, length);
	return 0;
}

static int wifi_scan_add_channel(struct wifi_scan_plan *plan, unsigned int channel,
				 bool may_probe)
{
	unsigned int pass, i;

	if (!channel || channel > WIFI_SCAN_CHANNELS)
		return -EINVAL;
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < plan->count[pass]; i++)
			if (plan->channels[pass][i] == channel)
				return -EINVAL;
	/* This firmware suppresses probes on channels 12..14 independently of
	 * the regulatory domain. Keep them in the explicit passive pass.
	 */
	pass = plan->active && may_probe && channel <= 11;
	if (plan->count[pass] >= WIFI_SCAN_CHANNELS)
		return -E2BIG;
	plan->channels[pass][plan->count[pass]++] = channel;
	return 0;
}

static int wifi_scan_parameters(const struct wifi_scan_plan *plan,
				unsigned int pass, u8 *parameters)
{
	if (pass > 1 || !plan->count[pass] || plan->count[pass] > WIFI_SCAN_CHANNELS)
		return -EINVAL;
	memset(parameters, 0, WIFI_SCAN_PARAMETERS);
	put_unaligned_le32(pass, parameters);
	put_unaligned_le32(48, parameters + 4);
	if (pass) {
		put_unaligned_le32(plan->ssid_length, parameters + 8);
		memcpy(parameters + 12, plan->ssid, plan->ssid_length);
	}
	/* Packed DriverCtrl starts at 46; preserve the firmware dwell times. */
	parameters[50] = 1;
	memcpy(parameters + 51, plan->channels[pass], plan->count[pass]);
	parameters[83] = plan->count[pass];
	return 0;
}

/* Revalidate a queued pass against the current regulatory channel flags. */
static void wifi_scan_restrict(u8 *parameters, u16 allowed, u16 may_probe)
{
	unsigned int i, count = 0;

	if (parameters[0])
		allowed &= may_probe;
	for (i = 0; i < parameters[83]; i++) {
		u8 channel = parameters[51 + i];

		if (channel && channel <= WIFI_SCAN_CHANNELS &&
		    (allowed & BIT(channel - 1)))
			parameters[51 + count++] = channel;
	}
	memset(parameters + 51 + count, 0, 32 - count);
	parameters[83] = count;
}
