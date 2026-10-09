/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712 SDIO cfg80211 integration. Included after the SDIO device state.
 * Limited WPA2-PSK/CCMP station profile with passive scans and host crypto.
 */
#include <linux/etherdevice.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>
#include <net/regulatory.h>
#include "h432b-wifi-h2c.h"
#include <crypto/aead.h>
#include <linux/scatterlist.h>
#include "h432b-wifi-ccmp.h"

#define WIFI_BSS_CACHE 32
struct h432b_wifi_bss { u8 data[884]; unsigned long seen; bool valid; };

struct h432b_wifi_net {
	struct h432b_wifi_sample *owner;
	struct sdio_func *func;
	struct wiphy *wiphy;
	struct net_device *dev;
	struct wireless_dev wdev;
	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[13];
	struct ieee80211_rate rates[12];
	struct mutex lock;
	struct work_struct scan_work;
	struct delayed_work event_work;
	struct workqueue_struct *workqueue;
	u8 *event_buffer;
	unsigned int idle_batches, idle_empty, consecutive_empty;
	struct cfg80211_scan_request *request;
	bool stopping, faulted, enabled_func;
	unsigned int saved_block_size;
	unsigned int scans, reports;
	int last_error;
	struct h432b_wifi_bss cache[WIFI_BSS_CACHE];
	struct delayed_work join_timeout;
	u8 bssid[ETH_ALEN], join_ie[256];
	unsigned int join_ie_len;
	bool connecting, associated, authorized;
	int join_result;
	struct h432b_wifi_key keys[5]; /* four GTK slots, one pairwise key */
	struct sk_buff_head tx_queue;
	struct work_struct tx_work;
	u16 tx_sequence;
	unsigned int rx_replay, rx_mic, rx_rejected, tx_failed;

};

static void wifi_net_link_down(struct h432b_wifi_net *net, u16 reason, bool local);

/* Firmware BSSID_EX uses channel numbers and a 12-byte fixed beacon header. */
static int wifi_net_report(void *context, const u8 *bss, unsigned int length)
{
	struct h432b_wifi_net *net = context;
	struct cfg80211_inform_bss info = {};
	struct cfg80211_bss *entry;
	const u8 *fixed, *ies;
	unsigned int channel, ie_len, pos, slot = 0, i;

	if (length < 128 || !is_valid_ether_addr(bss + 4))
		return -EBADMSG;
	ie_len = get_unaligned_le32(bss + 112);
	if (ie_len < 12 || ie_len > length - 116)
		return -EBADMSG;
	channel = get_unaligned_le32(bss + 72);
	if (channel < 1 || channel > ARRAY_SIZE(net->channels))
		return -EBADMSG;
	info.chan = &net->channels[channel - 1];
	if (info.chan->flags & IEEE80211_CHAN_DISABLED)
		return 0;
	info.boottime_ns = ktime_get_boottime_ns();
	/* RSSI units are not yet qualified: do not invent a dBm conversion. */
	fixed = bss + 116;
	ies = fixed + 12;
	ie_len -= 12;
	for (pos = 0; pos < ie_len; pos += 2 + ies[pos + 1]) {
		if (ie_len - pos < 2 || ies[pos + 1] > ie_len - pos - 2)
			return -EBADMSG;
	}
	/* Retain the firmware's fixed fields for its JoinBss ABI. */
	for (i = 0; i < WIFI_BSS_CACHE; i++) {
		if (net->cache[i].valid && ether_addr_equal(net->cache[i].data + 4, bss + 4)) {
			slot = i;
			break;
		}
		if (!net->cache[i].valid || (net->cache[slot].valid &&
		    time_before(net->cache[i].seen, net->cache[slot].seen)))
			slot = i;
	}
	memset(net->cache[slot].data, 0, sizeof(net->cache[slot].data));
	memcpy(net->cache[slot].data, bss, min_t(unsigned int, length, 884));
	net->cache[slot].seen = jiffies;
	net->cache[slot].valid = true;
	entry = cfg80211_inform_bss_data(net->wiphy, &info,
			CFG80211_BSS_FTYPE_BEACON, bss + 4,
			get_unaligned_le64(fixed), get_unaligned_le16(fixed + 10),
			get_unaligned_le16(fixed + 8), ies, ie_len, GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	cfg80211_put_bss(net->wiphy, entry);
	net->reports++;
	return 0;
}

static void wifi_net_finish_scan(struct h432b_wifi_net *net, bool aborted)
{
	struct cfg80211_scan_request *request;
	struct cfg80211_scan_info info = { .aborted = aborted };

	mutex_lock(&net->lock);
	request = net->request;
	net->request = NULL;
	mutex_unlock(&net->lock);
	if (request)
		cfg80211_scan_done(request, &info);
}

/* All queue consumers run on one ordered worker and take owner->lock.
 * The IRQ callback only masks the device, records status and wakes consumers.
 */
static void wifi_net_event_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(to_delayed_work(work),
						 struct h432b_wifi_net, event_work);
	struct h432b_command_result *r = &net->owner->command;
	unsigned int i;
	int error = 0, events, packets;
	bool progress = false;

	mutex_lock(&net->owner->lock);
	if (READ_ONCE(net->stopping) || READ_ONCE(net->faulted))
		goto unlock;
	sdio_claim_host(net->func);
	if (r->irq_error) {
		error = r->irq_error;
		goto release;
	}
	for (i = 0; i < 64; i++) {
		events = wifi_command_drain(net->func, r, net->event_buffer);
		if (events < 0) {
			error = events;
			goto release;
		}
		packets = wifi_rx_drain(net->func, &r->rx, net->event_buffer);
		if (packets < 0) {
			error = packets;
			goto release;
		}
		if (!events && !packets)
			break;
		progress = true;
		net->idle_batches++;
	}
	if (i == 64)
		queue_delayed_work(net->workqueue, &net->event_work, 0);
	if (progress)
		net->consecutive_empty = 0;
	else {
		net->idle_empty++;
		/* A permanently asserted source must not spin the MMC host. */
		if (++net->consecutive_empty > 128)
			error = -ELOOP;
	}
	if (!error)
		error = wifi_command_arm(net->func, r);
release:
	if (error) {
		int mask_error = 0;

		sdio_writew(net->func, 0, WIFI_HIMR, &mask_error);
		WRITE_ONCE(net->faulted, true);
		net->last_error = error;
		r->error = error;
		dev_err(&net->func->dev, "receive/event service failed: %d\n", error);
		wifi_net_link_down(net, WLAN_REASON_UNSPECIFIED, true);
	}
	sdio_release_host(net->func);
unlock:
	mutex_unlock(&net->owner->lock);
}

/* Called with the MMC host held, never the owner mutex. */
static void wifi_net_irq_notify(struct h432b_wifi_net *net)
{
	if (!READ_ONCE(net->stopping))
		queue_delayed_work(net->workqueue, &net->event_work, 0);
}

static void wifi_net_scan_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(work, struct h432b_wifi_net, scan_work);
	struct h432b_wifi_sample *owner = net->owner;
	struct h432b_command_result *r = &owner->command;
	int error;

	mutex_lock(&owner->lock);
	if (READ_ONCE(net->stopping) || READ_ONCE(r->cancelled))
		error = -ECANCELED;
	else
		error = wifi_command_test(net->func, r, owner->firmware.c2h_base,
					  h432b_wifi_irq);
	if (!error)
		error = r->cleanup;
	/* A partial command/scan cannot safely be retried on this firmware stream. */
	if (error) {
		int mask_error = 0;

		WRITE_ONCE(net->faulted, true);
		sdio_claim_host(net->func);
		sdio_writew(net->func, 0, WIFI_HIMR, &mask_error);
		sdio_release_host(net->func);
	}
	r->error = error;
	net->last_error = error;
	net->scans++;
	net->consecutive_empty = 0;
	mutex_unlock(&owner->lock);
	dev_dbg(&net->func->dev, "cfg80211 scan: error=%d completed=%u reports=%u\n",
		 error, net->scans, net->reports);
	wifi_net_finish_scan(net, error || READ_ONCE(net->stopping));
	if (!error && !READ_ONCE(net->stopping))
		queue_delayed_work(net->workqueue, &net->event_work, 0);
}

static int wifi_net_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	struct h432b_command_result *r = &net->owner->command;
	unsigned int i;
	int error = 0;

	/* iw defaults to colocated discovery without an explicit frequency list.
	 * It has no effect on this 2.4 GHz-only wiphy; no probe is requested.
	 */
	if (request->n_ssids || request->ie_len > 512 ||
	    (request->flags & ~(NL80211_SCAN_FLAG_COLOCATED_6GHZ | NL80211_SCAN_FLAG_FLUSH)))
		return -EOPNOTSUPP;
	if (!request->n_channels || request->n_channels > ARRAY_SIZE(r->survey_channels))
		return -EINVAL;
	mutex_lock(&net->lock);
	if (net->request || net->connecting || net->associated) {
		error = -EBUSY;
		goto out;
	}
	if (READ_ONCE(net->stopping) || !netif_running(net->dev)) {
		error = -ENETDOWN;
		goto out;
	}
	if (READ_ONCE(net->faulted)) {
		error = -EIO;
		goto out;
	}
	for (i = 0; i < request->n_channels; i++) {
		struct ieee80211_channel *chan = request->channels[i];

		if (chan->band != NL80211_BAND_2GHZ ||
		    chan->hw_value < 1 || chan->hw_value > 13 ||
		    chan->flags & IEEE80211_CHAN_DISABLED) {
			error = -EINVAL;
			goto out;
		}
		r->survey_channels[i] = chan->hw_value;
	}
	r->survey_nchannels = request->n_channels;
	WRITE_ONCE(r->cancelled, false);
	net->request = request;
	queue_work(net->workqueue, &net->scan_work);
out:
	mutex_unlock(&net->lock);
	return error;
}

static void wifi_net_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);

	WRITE_ONCE(net->owner->command.cancelled, true);
	complete(&net->owner->command.irq_done);
}

#include "h432b-wifi-data.h"
#include "h432b-wifi-assoc.h"

static const struct cfg80211_ops wifi_net_cfg_ops = {
	.add_key = wifi_net_add_key,
	.del_key = wifi_net_del_key,
	.set_default_key = wifi_net_default_key,
	.change_station = wifi_net_change_station,
	.get_station = wifi_net_get_station,
	.connect = wifi_net_connect,
	.disconnect = wifi_net_disconnect,
	.scan = wifi_net_scan,
	.abort_scan = wifi_net_abort_scan,
};

static int wifi_net_open(struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);

	struct h432b_command_result *r = &net->owner->command;
	int error, cleanup;
	u8 ioex;

	if (READ_ONCE(net->faulted))
		return -EIO;
	mutex_lock(&net->owner->lock);
	sdio_claim_host(net->func);
	if (!(net->func->card->host->caps & MMC_CAP_SDIO_IRQ)) {
		error = -EOPNOTSUPP;
		goto release;
	}
	net->saved_block_size = net->func->cur_blksize;
	ioex = sdio_f0_readb(net->func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto release;
	if (!(ioex & BIT(net->func->num))) {
		error = sdio_enable_func(net->func);
		if (error)
			goto release;
		net->enabled_func = true;
	}
	error = sdio_set_block_size(net->func, 512);
	if (error)
		goto release;
	error = sdio_claim_irq(net->func, h432b_wifi_irq);
	if (error)
		goto release;
	r->irq_owned = true;
	r->irq_native = true;
	WRITE_ONCE(r->cancelled, false);
	WRITE_ONCE(net->stopping, false);
	error = wifi_command_arm(net->func, r);
	if (error) {
		WRITE_ONCE(net->stopping, true);
		cleanup = sdio_release_irq(net->func);
		r->cleanup = cleanup;
		r->irq_owned = false;
	}
release:
	if (error && net->enabled_func) {
		cleanup = sdio_disable_func(net->func);
		r->cleanup = r->cleanup ? r->cleanup : cleanup;
		net->enabled_func = false;
	}
	sdio_release_host(net->func);
	mutex_unlock(&net->owner->lock);
	if (error)
		return error;
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	queue_delayed_work(net->workqueue, &net->event_work, 0);
	return 0;
}

static int wifi_net_stop(struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);

	WRITE_ONCE(net->stopping, true);
	WRITE_ONCE(net->owner->command.cancelled, true);
	complete(&net->owner->command.irq_done);
	cancel_delayed_work_sync(&net->join_timeout);
	cancel_work_sync(&net->tx_work);
	skb_queue_purge(&net->tx_queue);
	cancel_work_sync(&net->scan_work);
	cancel_delayed_work_sync(&net->event_work);
	mutex_lock(&net->owner->lock);
	sdio_claim_host(net->func);
	if (net->owner->command.irq_owned) {
		int error = 0, cleanup;

		sdio_writew(net->func, 0, WIFI_HIMR, &error);
		cleanup = sdio_release_irq(net->func);
		net->owner->command.irq_owned = false;
		if (error || cleanup) {
			WRITE_ONCE(net->faulted, true);
			net->last_error = error ? error : cleanup;
		}
	}
	if (net->saved_block_size) {
		int cleanup = sdio_set_block_size(net->func, net->saved_block_size);

		if (cleanup) {
			WRITE_ONCE(net->faulted, true);
			net->last_error = cleanup;
		}
	}
	if (net->enabled_func) {
		int cleanup = sdio_disable_func(net->func);

		net->enabled_func = false;
		if (cleanup) {
			WRITE_ONCE(net->faulted, true);
			net->last_error = cleanup;
		}
	}
	sdio_release_host(net->func);
	wifi_net_clear_keys(net);
	mutex_unlock(&net->owner->lock);
	net->connecting = false;
	net->associated = false;
	net->authorized = false;
	/* A queued work item cancelled before execution still owns its request. */
	wifi_net_finish_scan(net, true);
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	return 0;
}

static netdev_tx_t wifi_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);

	if (READ_ONCE(net->stopping) || READ_ONCE(net->faulted) ||
	    skb->len < ETH_HLEN || skb->len > ETH_FRAME_LEN) {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	if (skb_queue_len(&net->tx_queue) >= 64) {
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}
	skb_queue_tail(&net->tx_queue, skb);
	queue_work(net->workqueue, &net->tx_work);
	return NETDEV_TX_OK;
}

static const struct net_device_ops wifi_net_ops = {
	.ndo_open = wifi_net_open,
	.ndo_stop = wifi_net_stop,
	.ndo_start_xmit = wifi_net_xmit,
	.ndo_validate_addr = eth_validate_addr,
};

static int wifi_net_register(struct sdio_func *func, struct h432b_wifi_sample *owner)
{
	static const unsigned int rates[] = { 10, 20, 55, 110, 60, 90, 120, 180,
					      240, 360, 480, 540 };
	struct h432b_command_result *r = &owner->command;
	struct h432b_wifi_net *net;
	struct wiphy *wiphy;
	struct net_device *dev;
	u8 mac[ETH_ALEN], ioex;
	unsigned int i;
	bool enabled = false;
	int error, cleanup = 0;

	/* All hardware access is serialized by the owner's mutex. */
	sdio_claim_host(func);
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (!error && !(ioex & BIT(func->num))) {
		enabled = true;
		error = sdio_enable_func(func);
	}
	if (!error)
		error = wifi_command_mac_init(func, r);
	for (i = 0; !error && i < ETH_ALEN; i++)
		mac[i] = wifi_read(func, 1, 0x50 + i, &error);
	if (enabled)
		cleanup = sdio_disable_func(func);
	sdio_release_host(func);
	if (error || cleanup)
		return error ? error : cleanup;
	if (!is_valid_ether_addr(mac) || (mac[0] == 0x78 && mac[1] == 0x56))
		return -EADDRNOTAVAIL;

	wiphy = wiphy_new(&wifi_net_cfg_ops, sizeof(*net));
	if (!wiphy)
		return -ENOMEM;
	net = wiphy_priv(wiphy);
	net->owner = owner;
	net->func = func;
	net->wiphy = wiphy;
	net->stopping = true;
	mutex_init(&net->lock);
	skb_queue_head_init(&net->tx_queue);
	INIT_WORK(&net->tx_work, wifi_net_tx_work);
	INIT_WORK(&net->scan_work, wifi_net_scan_work);
	INIT_DELAYED_WORK(&net->join_timeout, wifi_net_join_timeout);
	INIT_DELAYED_WORK(&net->event_work, wifi_net_event_work);
	net->event_buffer = kzalloc(max(WIFI_EVENT_MAX, WIFI_RX_MAX), GFP_KERNEL);
	if (!net->event_buffer) {
		error = -ENOMEM;
		goto free_wiphy;
	}
	net->workqueue = alloc_ordered_workqueue("h432b-wifi", WQ_MEM_RECLAIM);
	if (!net->workqueue) {
		error = -ENOMEM;
		goto free_wiphy;
	}
	/* Registration already initialized the MAC. These stream positions are
	 * shared by idle event processing and every subsequent command.
	 */
	r->consumed = owner->firmware.c2h_base;
	r->next_command = 1;
	r->stream_started = true;
	init_completion(&r->irq_done);
	r->persistent = true;
	r->irq_mode = true;
	r->opmode = true;
	r->survey = true;
	r->rx.receive = wifi_net_receive;
	r->rx.context = net;
	r->report_event = wifi_net_event;
	r->report_bss = wifi_net_report;
	r->report_context = net;
	for (i = 0; i < ARRAY_SIZE(net->channels); i++) {
		net->channels[i].band = NL80211_BAND_2GHZ;
		net->channels[i].center_freq = 2412 + 5 * i;
		net->channels[i].hw_value = i + 1;
		net->channels[i].max_power = 20;
		/* Passive-only profile; regulatory core may restrict further. */
		net->channels[i].flags = IEEE80211_CHAN_NO_IR;
	}
	for (i = 0; i < ARRAY_SIZE(net->rates); i++) {
		net->rates[i].bitrate = rates[i];
		net->rates[i].hw_value = i;
	}
	net->band.channels = net->channels;
	net->band.n_channels = ARRAY_SIZE(net->channels);
	net->band.bitrates = net->rates;
	net->band.n_bitrates = ARRAY_SIZE(net->rates);
	wiphy->bands[NL80211_BAND_2GHZ] = &net->band;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_NONE;
	/* Retain the operator's country policy instead of accepting AP hints. */
	wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	wiphy->max_scan_ssids = 0;
	wiphy->max_scan_ie_len = 512; /* ignored for passive scans: no probe TX */
	wiphy->cipher_suites = wifi_net_ciphers;
	wiphy->n_cipher_suites = ARRAY_SIZE(wifi_net_ciphers);
	memcpy(wiphy->perm_addr, mac, ETH_ALEN);
	set_wiphy_dev(wiphy, &func->dev);
	dev = alloc_netdev(sizeof(net), "wlan%d", NET_NAME_ENUM, ether_setup);
	if (!dev) {
		error = -ENOMEM;
		goto free_wiphy;
	}
	net->dev = dev;
	*(struct h432b_wifi_net **)netdev_priv(dev) = net;
	net->wdev.wiphy = wiphy;
	net->wdev.iftype = NL80211_IFTYPE_STATION;
	net->wdev.netdev = dev;
	dev->ieee80211_ptr = &net->wdev;
	dev->netdev_ops = &wifi_net_ops;
	eth_hw_addr_set(dev, mac);
	SET_NETDEV_DEV(dev, &func->dev);
	netif_carrier_off(dev);
	error = wiphy_register(wiphy);
	if (error)
		goto free_netdev;
	error = register_netdev(dev);
	if (error)
		goto unregister_wiphy;
	owner->net = net;
	dev_dbg(&func->dev, "station interface registered\n");
	return 0;

unregister_wiphy:
	wiphy_unregister(wiphy);
free_netdev:
	free_netdev(dev);
free_wiphy:
	if (net->workqueue)
		destroy_workqueue(net->workqueue);
	kfree(net->event_buffer);
	r->report_event = NULL;
	r->rx.receive = NULL;
	r->rx.context = NULL;
	r->report_bss = NULL;
	r->report_context = NULL;
	wiphy_free(wiphy);
	return error;
}

static void wifi_net_unregister(struct h432b_wifi_sample *owner)
{
	struct h432b_wifi_net *net = owner->net;

	if (!net)
		return;
	WRITE_ONCE(net->stopping, true);
	unregister_netdev(net->dev);
	cancel_work_sync(&net->scan_work);
	cancel_delayed_work_sync(&net->event_work);
	destroy_workqueue(net->workqueue);
	kfree(net->event_buffer);
	wifi_net_finish_scan(net, true);
	wiphy_unregister(net->wiphy);
	free_netdev(net->dev);
	owner->command.rx.receive = NULL;
	owner->command.rx.context = NULL;
	owner->command.report_event = NULL;
	owner->command.report_context = NULL;
	owner->command.report_bss = NULL;
	owner->net = NULL;
	wiphy_free(net->wiphy);
}

#include "h432b-wifi-led.h"

/* Counters only: never publish received packet contents or nearby identities. */
static ssize_t network_result_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct h432b_wifi_sample *owner = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct h432b_wifi_net *net;
	struct h432b_command_result *r = &owner->command;
	ssize_t size;

	mutex_lock(&owner->lock);
	net = owner->net;
	if (!net)
		size = sysfs_emit(buf, "idle\n");
	else
		size = sysfs_emit(buf,
			"error=%d faulted=%d irq_owned=%d callbacks=%u idle_batches=%u idle_empty=%u c2h_events=%u rx_batches=%u rx_bytes=%u rx_frames=%u rx_crc=%u rx_icv=%u rx_high_water=%u associated=%d authorized=%d join=%d rx_replay=%u rx_mic=%u rx_rejected=%u tx_failed=%u\n",
			net->last_error, net->faulted, r->irq_owned, r->irq_callbacks,
			net->idle_batches, net->idle_empty, r->events, r->rx.batches,
			r->rx.bytes, r->rx.frames, r->rx.crc_errors, r->rx.icv_errors, r->rx.high_water, net->associated, net->authorized,
			net->join_result, net->rx_replay, net->rx_mic, net->rx_rejected, net->tx_failed);
	mutex_unlock(&owner->lock);
	return size;
}
static DEVICE_ATTR_RO(network_result);
