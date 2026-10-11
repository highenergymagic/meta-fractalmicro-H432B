/* SPDX-License-Identifier: GPL-2.0-only */
/* Standard netdev statistics; never expose keys, frame contents or SSIDs. */
#include <linux/ethtool.h>

static const char wifi_stat_names[][ETH_GSTRING_LEN] = {
	"rx_ht_authenticated",
	"rx_amsdu_authenticated",
	"rx_addba_reports",
	"rx_reorder_dropped",
	"rx_replay_rejected",
	"rx_mic_errors",
	"rx_frames_rejected",
	"tx_failed",
	"recovery_attempts",
	"recovery_successes",
	"rx_ba_tid_mask",
	"tx_addba_tid_mask",
	"ht_requested",
	"qos_requested",
	"rx_pending_high_water_bytes",
	"scan_reports_dropped",
	"rx_ht_mcs0", "rx_ht_mcs1", "rx_ht_mcs2", "rx_ht_mcs3",
	"rx_ht_mcs4", "rx_ht_mcs5", "rx_ht_mcs6", "rx_ht_mcs7",
	"rx_ht_mcs8", "rx_ht_mcs9", "rx_ht_mcs10", "rx_ht_mcs11",
	"rx_ht_mcs12", "rx_ht_mcs13", "rx_ht_mcs14", "rx_ht_mcs15",
};

static int wifi_net_stat_count(struct net_device *dev, int set)
{
	return set == ETH_SS_STATS ? ARRAY_SIZE(wifi_stat_names) : -EOPNOTSUPP;
}

static void wifi_net_stat_names(struct net_device *dev, u32 set, u8 *data)
{
	if (set == ETH_SS_STATS)
		memcpy(data, wifi_stat_names, sizeof(wifi_stat_names));
}

static void wifi_net_stats(struct net_device *dev, struct ethtool_stats *stats,
			   u64 *data)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);
	unsigned int i, index = 0;
	u16 rx_ba = 0;

	mutex_lock(&net->owner->lock);
	for (i = 0; i < ARRAY_SIZE(net->reorder); i++)
		if (net->reorder[i].enabled)
			rx_ba |= BIT(i);
	data[index++] = net->rx_ht_authenticated;
	data[index++] = net->rx_amsdu_authenticated;
	data[index++] = net->rx_addba_reports;
	data[index++] = net->rx_reorder_dropped;
	data[index++] = net->rx_replay;
	data[index++] = net->rx_mic;
	data[index++] = net->rx_rejected;
	data[index++] = net->tx_failed;
	data[index++] = READ_ONCE(net->recovery_attempts);
	data[index++] = READ_ONCE(net->recovery_successes);
	data[index++] = rx_ba;
	data[index++] = net->tx_ba_requested;
	data[index++] = net->ht.ht;
	data[index++] = net->ht.qos;
	data[index++] = net->owner->command.rx.high_water * 512;
	data[index++] = net->scan_reports_dropped;
	for (i = 0; i < ARRAY_SIZE(net->rx_ht_mcs); i++)
		data[index++] = net->rx_ht_mcs[i];
	mutex_unlock(&net->owner->lock);
}

static const struct ethtool_ops wifi_net_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_sset_count = wifi_net_stat_count,
	.get_strings = wifi_net_stat_names,
	.get_ethtool_stats = wifi_net_stats,
};
