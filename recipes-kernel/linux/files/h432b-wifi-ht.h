/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712S WMM/HT firmware ABI. The JoinBss and AddBA conventions follow
 * the Realtek RTL8712 driver, Copyright(c) 2007-2010 Realtek Corporation.
 */
#define WIFI_HT_CAP_LEN 26
#define WIFI_HT_IE_LEN (2 + WIFI_HT_CAP_LEN)
#define WIFI_WMM_IE_LEN 9
#define WIFI_HT_MAX_AMSDU 3839
#define WIFI_ASSOC_IE_MAX 256

/* SDIO word 3 bit 6 identifies HT; rate indices 12..27 are MCS 0..15.
 * Width and guard interval are deliberately not inferred from these bits.
 */
static int wifi_rx_ht_mcs(u32 descriptor)
{
	unsigned int rate = descriptor & 0x3f;

	if (!(descriptor & BIT(6)) || rate < 12 || rate > 27)
		return -1;
	return rate - 12;
}

struct wifi_ht_profile {
	bool qos;
	bool ht;
	u8 capability[WIFI_HT_CAP_LEN];
};

/* HT20 with the factory 1T2R profile: two RX streams and one TX stream.
 * Firmware builds its on-air MCS bitmap from RFConfig, overriding JoinBss.
 * The existing firmware bandwidth input must also disable 40 MHz.
 */
static void wifi_ht_capability(u8 *capability)
{
	memset(capability, 0, WIFI_HT_CAP_LEN);
	put_unaligned_le16(0x0020, capability); /* Short GI at 20 MHz. */
	capability[2] = 3; /* Maximum RX A-MPDU 65535 bytes, no density constraint. */
	capability[3] = 0xff;
	capability[4] = 0xff;
	capability[15] = 3; /* TX defined, RX/TX differ, TX max streams minus 1 = 0. */
}

/* Select only capabilities offered by the AP. Reject malformed IE streams
 * rather than allowing firmware and host to parse different boundaries.
 */
static int wifi_ht_select(const u8 *ies, unsigned int length,
			  struct wifi_ht_profile *profile)
{
	static const u8 wmm[] = { 0x00, 0x50, 0xf2, 0x02 };
	const u8 *ht = NULL;
	unsigned int pos, size;
	u16 caps;

	memset(profile, 0, sizeof(*profile));
	for (pos = 0; pos < length; pos += size + 2) {
		if (length - pos < 2)
			return -EBADMSG;
		size = ies[pos + 1];
		if (size > length - pos - 2)
			return -EBADMSG;
		if (ies[pos] == 45) {
			if (size != WIFI_HT_CAP_LEN || ht)
				return -EBADMSG;
			ht = ies + pos + 2;
		}
		if (ies[pos] == 221 && size >= 7 &&
		    !memcmp(ies + pos + 2, wmm, sizeof(wmm)) &&
		    (ies[pos + 6] == 0 || (ies[pos + 6] == 1 && size >= 24)) &&
		    ies[pos + 7] == 1)
			profile->qos = true;
	}
	/* HT requires WMM. A malformed/non-WMM AP falls back to legacy rates. */
	if (!ht || !profile->qos)
		return 0;
	wifi_ht_capability(profile->capability);
	caps = get_unaligned_le16(profile->capability);
	caps &= get_unaligned_le16(ht);
	put_unaligned_le16(caps, profile->capability);
	profile->ht = true;
	return 0;
}

static unsigned int wifi_ht_join_ies(u8 *output,
				     const struct wifi_ht_profile *profile)
{
	static const u8 wmm[] = { 221, 7, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00 };
	unsigned int length = 0;

	if (profile->qos) {
		memcpy(output, wmm, sizeof(wmm));
		length = sizeof(wmm);
	}
	if (profile->ht) {
		output[length++] = 45;
		output[length++] = WIFI_HT_CAP_LEN;
		memcpy(output + length, profile->capability, WIFI_HT_CAP_LEN);
		length += WIFI_HT_CAP_LEN;
	}
	return length;
}

/* cfg80211's additional association IEs may carry RSN/extended capabilities;
 * WMM and HT are constructed by this driver and must not occur twice.
 */
static bool wifi_ht_assoc_ies_valid(const u8 *ies, unsigned int length)
{
	unsigned int pos, size;

	if (length > WIFI_ASSOC_IE_MAX)
		return false;
	for (pos = 0; pos < length; pos += size + 2) {
		if (length - pos < 2)
			return false;
		size = ies[pos + 1];
		if (size > length - pos - 2 || ies[pos] == 45 || ies[pos] == 61)
			return false;
		if (ies[pos] == 221 && size >= 4 &&
		    !memcmp(ies + pos + 2, "\x00\x50\xf2\x02", 4))
			return false;
	}
	return true;
}

/* Exact legacy RX rates, in 100 kbit/s. Factory SDIO descriptor word 3
 * uses bits 5:0 for rate and bit 6 for HT (unlike the USB bit-14 layout).
 * HT width/GI are not inferred from association capabilities.
 */
static unsigned int wifi_rx_legacy_rate(u32 word3)
{
	static const u16 rates[] = { 10, 20, 55, 110, 60, 90, 120, 180,
				    240, 360, 480, 540 };
	unsigned int index = word3 & 0x3f;

	if ((word3 & BIT(6)) || index >= ARRAY_SIZE(rates))
		return 0;
	return rates[index];
}
