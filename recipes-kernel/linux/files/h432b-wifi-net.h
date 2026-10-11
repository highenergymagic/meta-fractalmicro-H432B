/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712 SDIO cfg80211 integration. Included after the SDIO device state.
 * Firmware-assisted station operation, with CCMP authenticated by the host.
 */
#include <linux/etherdevice.h>
#include <linux/in.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>
#include <net/regulatory.h>
#include "h432b-wifi-h2c.h"
#include <crypto/aead.h>
#include <linux/scatterlist.h>
#include "h432b-wifi-ccmp.h"
#include "h432b-wifi-ht.h"
#include "h432b-wifi-reorder.h"
#include "h432b-wifi-scan.h"

#define WIFI_BSS_CACHE 32
struct h432b_wifi_bss {
	u8 data[884];
	unsigned long seen;
	bool valid;
};

struct h432b_wifi_net {
	struct h432b_wifi_sample *owner;
	struct sdio_func *func;
	struct wiphy *wiphy;
	struct net_device *dev;
	struct wireless_dev wdev;
	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[13];
	struct ieee80211_rate rates[12];
	struct work_struct scan_work;
	struct delayed_work event_work;
	struct delayed_work recovery_work;
	struct workqueue_struct *workqueue;
	u8 *event_buffer;
	unsigned int idle_batches, idle_empty, consecutive_empty;
	struct cfg80211_scan_request *request;
	struct wifi_scan_plan scan_plan;
	bool stopping, faulted, enabled_func, removing, needs_restart, scan_aborted;
	unsigned int recovery_attempts, recovery_successes;
	unsigned int saved_block_size;
	unsigned int scans, reports, scan_reports_dropped;
	int last_error;
	struct h432b_wifi_bss cache[WIFI_BSS_CACHE];
	struct delayed_work join_timeout;
	u8 bssid[ETH_ALEN], join_ie[WIFI_ASSOC_IE_MAX + WIFI_HT_IE_LEN + WIFI_WMM_IE_LEN];
	unsigned int join_ie_len;
	bool connecting, associated, authorized;
	int join_result;
	struct h432b_wifi_key keys[5]; /* four GTK slots, one pairwise key */
	struct sk_buff_head tx_queue;
	struct work_struct tx_work;
	struct delayed_work reorder_work;
	struct wifi_reorder reorder[16];
	unsigned long reorder_deadline[16];
	struct wifi_ht_profile ht;
	u16 tx_sequence[17];
	u16 tx_ba_requested;
	u16 rx_legacy_rate;
	u64 rx_ht_authenticated, rx_ht_mcs[16], rx_amsdu_authenticated, rx_addba_reports;
	unsigned int rx_reorder_dropped;
	unsigned int rx_replay, rx_mic, rx_rejected, tx_failed;

};

static void wifi_net_link_down(struct h432b_wifi_net *net, u16 reason, bool local);

/* Memory pressure and an administratively cancelled operation are not faults
 * in the device command stream. Retry only failed transport/protocol service.
 */
static bool wifi_net_recoverable(int error)
{
	return error == -EIO || error == -ETIMEDOUT || error == -EILSEQ ||
	       error == -EPROTO || error == -ELOOP;
}

/* Owner lock held. IRQ masking remains with the caller holding the host. */
static void wifi_net_fault(struct h432b_wifi_net *net, int error)
{
	WRITE_ONCE(net->faulted, true);
	net->last_error = error;
	wifi_net_link_down(net, WLAN_REASON_UNSPECIFIED, true);
	if (wifi_net_recoverable(error) && !READ_ONCE(net->stopping) &&
	    !READ_ONCE(net->removing) && READ_ONCE(net->recovery_attempts) < 3)
		queue_delayed_work(system_long_wq, &net->recovery_work,
				   msecs_to_jiffies(200));
}

/* Firmware BSSID_EX uses channel numbers and a 12-byte fixed beacon header. */
static int wifi_net_report(void *context, const u8 *bss, unsigned int length)
{
	struct h432b_wifi_net *net = context;
	struct cfg80211_inform_bss info = {};
	struct cfg80211_bss *entry;
	const u8 *fixed, *ies;
	unsigned int channel, ie_len, pos, slot = 0, i;

	if (length < 128 || length > sizeof(net->cache[0].data) ||
	    !is_valid_ether_addr(bss + 4))
		goto dropped;
	ie_len = get_unaligned_le32(bss + 112);
	if (ie_len < 12 || ie_len > length - 116)
		goto dropped;
	channel = get_unaligned_le32(bss + 72);
	if (channel < 1 || channel > ARRAY_SIZE(net->channels))
		goto dropped;
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
			goto dropped;
	}
	/* Retain the firmware's fixed fields for its JoinBss ABI. */
	for (i = 0; i < WIFI_BSS_CACHE; i++) {
		if (net->cache[i].valid && ether_addr_equal(net->cache[i].data + 4, bss + 4)) {
			slot = i;
			break;
		}
		if (!net->cache[i].valid)
			slot = i;
		else if (net->cache[slot].valid &&
			 time_before(net->cache[i].seen, net->cache[slot].seen))
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
		goto dropped;
	cfg80211_put_bss(net->wiphy, entry);
	net->reports++;
	return 0;
dropped:
	/* A bad beacon or failed BSS allocation is not a broken C2H stream.
	 * The enclosing event is already bounded; continue the scan.
	 */
	net->scan_reports_dropped++;
	return 0;
}

static void wifi_net_finish_scan(struct h432b_wifi_net *net, bool aborted)
{
	struct cfg80211_scan_request *request;
	struct cfg80211_scan_info info = { .aborted = aborted };

	mutex_lock(&net->owner->lock);
	request = net->request;
	net->request = NULL;
	mutex_unlock(&net->owner->lock);
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
	if (progress) {
		net->consecutive_empty = 0;
	} else {
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
		r->error = error;
		dev_err(&net->func->dev, "receive/event service failed: %d\n", error);
		wifi_net_fault(net, error);
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

/* Owner lock held. Runtime scans use the normal asynchronous firmware ABI,
 * never loopback commands or a particular firmware debug-string response.
 */
static int wifi_net_run_scan(struct h432b_wifi_net *net, unsigned int pass)
{
	struct h432b_command_result *r = &net->owner->command;
	unsigned long deadline = jiffies + msecs_to_jiffies(15000);
	u8 parameters[WIFI_SCAN_PARAMETERS], mode = 1;
	u16 allowed = 0, may_probe = 0;
	unsigned int i;
	bool sent = false;
	int error;

	error = wifi_scan_parameters(&net->scan_plan, pass, parameters);
	if (error)
		return error;
	for (i = 0; i < ARRAY_SIZE(net->channels); i++) {
		u32 flags = READ_ONCE(net->channels[i].flags);

		if (!(flags & IEEE80211_CHAN_DISABLED))
			allowed |= BIT(i);
		if (!(flags & IEEE80211_CHAN_NO_IR))
			may_probe |= BIT(i);
	}
	wifi_scan_restrict(parameters, allowed, may_probe);
	if (!parameters[83])
		return 0;
	r->scanning = true;
	r->survey_done = false;
	r->survey_events = 0;
	r->survey_count = 0;
	sdio_claim_host(net->func);
	error = wifi_h2c_send(net->func, r, 17, &mode, sizeof(mode));
	if (error)
		goto out;
	error = wifi_h2c_send(net->func, r, 18, parameters, sizeof(parameters));
	if (error)
		goto out;
	sent = true;
	while (!r->survey_done) {
		if (READ_ONCE(r->cancelled)) {
			error = -ECANCELED;
			break;
		}
		error = wifi_command_drain(net->func, r, net->event_buffer);
		if (error < 0)
			break;
		error = wifi_rx_drain(net->func, &r->rx, net->event_buffer);
		if (error < 0)
			break;
		if (r->survey_done)
			break;
		error = wifi_command_arm(net->func, r);
		if (error)
			break;
		error = wifi_command_wait(net->func, r, deadline);
		if (error)
			break;
	}
	if (error >= 0) {
		r->survey_runs++;
		r->survey_total += r->survey_count;
		error = wifi_command_arm(net->func, r);
	}
out:
	if (sent && error)
		net->needs_restart = true;
	r->scanning = false;
	sdio_release_host(net->func);
	return error;
}

static void wifi_net_scan_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(work, struct h432b_wifi_net, scan_work);
	struct h432b_wifi_sample *owner = net->owner;
	struct h432b_command_result *r = &owner->command;
	unsigned int pass;
	int error = 0;

	mutex_lock(&owner->lock);
	if (READ_ONCE(net->stopping) || READ_ONCE(r->cancelled))
		error = -ECANCELED;
	else
		for (pass = 0; pass < 2; pass++) {
			if (READ_ONCE(net->scan_aborted))
				break;
			if (!net->scan_plan.count[pass])
				continue;
			error = wifi_net_run_scan(net, pass);
			if (error)
				break;
		}
	if (error && error != -ECANCELED &&
	    (net->needs_restart || wifi_net_recoverable(error))) {
		int mask_error = 0;

		wifi_net_fault(net, error);
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
	wifi_net_finish_scan(net, error || READ_ONCE(net->stopping) ||
			     READ_ONCE(net->scan_aborted));
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
	 * It has no effect on this 2.4 GHz-only wiphy or the active/passive choice.
	 */
	if (request->n_ssids > 1 || request->ie_len ||
	    (request->flags & ~(NL80211_SCAN_FLAG_COLOCATED_6GHZ | NL80211_SCAN_FLAG_FLUSH)))
		return -EOPNOTSUPP;
	if (!request->n_channels || request->n_channels > WIFI_SCAN_CHANNELS)
		return -EINVAL;
	mutex_lock(&net->owner->lock);
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
	error = wifi_scan_plan_init(&net->scan_plan, request->n_ssids,
				    request->n_ssids ? request->ssids[0].ssid : NULL,
				    request->n_ssids ? request->ssids[0].ssid_len : 0);
	if (error)
		goto out;
	for (i = 0; i < request->n_channels; i++) {
		struct ieee80211_channel *chan = request->channels[i];

		if (chan->band != NL80211_BAND_2GHZ ||
		    chan->hw_value < 1 || chan->hw_value > 13 ||
		    chan->flags & IEEE80211_CHAN_DISABLED) {
			error = -EINVAL;
			goto out;
		}
		error = wifi_scan_add_channel(&net->scan_plan, chan->hw_value,
					      !(chan->flags & IEEE80211_CHAN_NO_IR));
		if (error)
			goto out;
	}
	WRITE_ONCE(r->cancelled, false);
	WRITE_ONCE(net->scan_aborted, false);
	net->request = request;
	queue_work(net->workqueue, &net->scan_work);
out:
	mutex_unlock(&net->owner->lock);
	return error;
}

static void wifi_net_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);

	/* Firmware has no proved scan-abort command. Consume its bounded survey
	 * to completion, report aborted, and leave the command stream usable.
	 */
	WRITE_ONCE(net->scan_aborted, true);
}

#include "h432b-wifi-data.h"
#include "h432b-wifi-assoc.h"
#include "h432b-wifi-stats.h"

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

static int wifi_net_start(struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);

	struct h432b_command_result *r = &net->owner->command;
	int error, cleanup;
	u8 ioex;

	mutex_lock(&net->owner->lock);
	if (READ_ONCE(net->faulted) || net->needs_restart) {
		error = wifi_runtime_restart(net);
		if (error)
			goto unlock;
		WRITE_ONCE(net->faulted, false);
		net->needs_restart = false;
		net->last_error = 0;
	}
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
	if (error) {
		/* Claim may have enabled CCCR IEN before host setup failed. */
		sdio_release_irq(net->func);
		goto release;
	}
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
	if (error && net->saved_block_size) {
		cleanup = sdio_set_block_size(net->func, net->saved_block_size);
		r->cleanup = r->cleanup ? r->cleanup : cleanup;
	}
	if (error && net->enabled_func) {
		cleanup = sdio_disable_func(net->func);
		r->cleanup = r->cleanup ? r->cleanup : cleanup;
		net->enabled_func = false;
	}
	sdio_release_host(net->func);
unlock:
	if (error) {
		WRITE_ONCE(net->stopping, true);
		WRITE_ONCE(net->faulted, true);
		net->last_error = error;
	}
	mutex_unlock(&net->owner->lock);
	if (error)
		return error;
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	queue_delayed_work(net->workqueue, &net->event_work, 0);
	return 0;
}

static int wifi_net_open(struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);
	int error;

	/* A new administrative up is an explicit request for a fresh retry budget. */
	net->recovery_attempts = 0;
	error = wifi_net_start(dev);
	if (!error)
		netif_device_attach(dev);
	return error;
}

static int wifi_net_stop(struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);

	WRITE_ONCE(net->stopping, true);
	/* The recovery worker uses rtnl_trylock, never waits behind ndo_stop. */
	cancel_delayed_work(&net->recovery_work);
	WRITE_ONCE(net->owner->command.cancelled, true);
	complete(&net->owner->command.irq_done);
	cancel_delayed_work_sync(&net->join_timeout);
	cancel_delayed_work_sync(&net->reorder_work);
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
	net->connecting = false;
	net->associated = false;
	net->authorized = false;
	mutex_unlock(&net->owner->lock);
	/* A queued work item cancelled before execution still owns its request. */
	wifi_net_finish_scan(net, true);
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	return 0;
}

/* Separate from the ordered packet queue: stopping drains that entire queue.
 * RTNL serializes administrative down/up and system sleep against recovery.
 */
static void wifi_net_recovery_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(to_delayed_work(work),
						 struct h432b_wifi_net, recovery_work);
	int error;

	if (READ_ONCE(net->removing))
		return;
	if (!rtnl_trylock()) {
		if (!READ_ONCE(net->removing) && netif_running(net->dev) &&
		    READ_ONCE(net->faulted) && READ_ONCE(net->recovery_attempts) < 3)
			queue_delayed_work(system_long_wq, &net->recovery_work,
					   msecs_to_jiffies(100));
		return;
	}
	if (net->removing || !netif_running(net->dev) || !net->faulted ||
	    net->recovery_attempts >= 3)
		goto unlock;
	net->recovery_attempts++;
	netif_device_detach(net->dev);
	wifi_net_stop(net->dev);
	error = wifi_net_start(net->dev);
	if (!error) {
		net->recovery_successes++;
		netif_device_attach(net->dev);
		dev_info(&net->func->dev, "radio recovered; reassociation required\n");
	} else {
		WRITE_ONCE(net->faulted, true);
		net->last_error = error;
		dev_err(&net->func->dev, "radio recovery attempt %u failed: %d\n",
			net->recovery_attempts, error);
		if (wifi_net_recoverable(error) && net->recovery_attempts < 3) {
			queue_delayed_work(system_long_wq, &net->recovery_work,
					   msecs_to_jiffies(1000 * net->recovery_attempts));
		}
	}
unlock:
	rtnl_unlock();
}

static netdev_tx_t wifi_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct h432b_wifi_net *net = *(struct h432b_wifi_net **)netdev_priv(dev);
	unsigned long flags;

	if (READ_ONCE(net->stopping) || READ_ONCE(net->faulted) ||
	    skb->len < ETH_HLEN || skb->len > ETH_FRAME_LEN) {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	skb->priority = cfg80211_classify8021d(skb, NULL);
	/* Serialize queue-stop with the worker's wake to avoid a lost wakeup. */
	spin_lock_irqsave(&net->tx_queue.lock, flags);
	if (skb_queue_len(&net->tx_queue) >= 64) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&net->tx_queue.lock, flags);
		return NETDEV_TX_BUSY;
	}
	__skb_queue_tail(&net->tx_queue, skb);
	if (skb_queue_len(&net->tx_queue) >= 64)
		netif_stop_queue(dev);
	spin_unlock_irqrestore(&net->tx_queue.lock, flags);
	queue_work(net->workqueue, &net->tx_work);
	return NETDEV_TX_OK;
}

static const struct net_device_ops wifi_net_ops = {
	.ndo_open = wifi_net_open,
	.ndo_stop = wifi_net_stop,
	.ndo_start_xmit = wifi_net_xmit,
	.ndo_validate_addr = eth_validate_addr,
};

static int wifi_net_read_mac(struct sdio_func *func, struct h432b_wifi_sample *owner,
			     u8 *mac)
{
	struct h432b_command_result *r = &owner->command;
	u8 ioex;
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
	return 0;
}

static void wifi_net_stream_init(struct h432b_wifi_net *net)
{
	struct h432b_command_result *r = &net->owner->command;

	r->consumed = net->owner->firmware.c2h_base;
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
	net->consecutive_empty = 0;
	memset(net->cache, 0, sizeof(net->cache));
}

static int wifi_net_register(struct sdio_func *func, struct h432b_wifi_sample *owner)
{
	static const unsigned int rates[] = { 10, 20, 55, 110, 60, 90, 120, 180,
					      240, 360, 480, 540 };
	struct h432b_command_result *r = &owner->command;
	struct h432b_wifi_net *net;
	struct wiphy *wiphy;
	struct net_device *dev;
	u8 mac[ETH_ALEN];
	unsigned int i;
	int error;

	error = wifi_net_read_mac(func, owner, mac);
	if (error)
		return error;
	wiphy = wiphy_new(&wifi_net_cfg_ops, sizeof(*net));
	if (!wiphy)
		return -ENOMEM;
	net = wiphy_priv(wiphy);
	net->owner = owner;
	net->func = func;
	net->wiphy = wiphy;
	net->stopping = true;
	skb_queue_head_init(&net->tx_queue);
	INIT_WORK(&net->tx_work, wifi_net_tx_work);
	INIT_WORK(&net->scan_work, wifi_net_scan_work);
	INIT_DELAYED_WORK(&net->join_timeout, wifi_net_join_timeout);
	INIT_DELAYED_WORK(&net->reorder_work, wifi_net_reorder_work);
	INIT_DELAYED_WORK(&net->event_work, wifi_net_event_work);
	INIT_DELAYED_WORK(&net->recovery_work, wifi_net_recovery_work);
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
	wifi_net_stream_init(net);
	for (i = 0; i < ARRAY_SIZE(net->channels); i++) {
		net->channels[i].band = NL80211_BAND_2GHZ;
		net->channels[i].center_freq = 2412 + 5 * i;
		net->channels[i].hw_value = i + 1;
		net->channels[i].max_power = 20;
	}
	for (i = 0; i < ARRAY_SIZE(net->rates); i++) {
		net->rates[i].bitrate = rates[i];
		net->rates[i].hw_value = i;
	}
	net->band.channels = net->channels;
	net->band.n_channels = ARRAY_SIZE(net->channels);
	net->band.bitrates = net->rates;
	net->band.n_bitrates = ARRAY_SIZE(net->rates);
	net->band.ht_cap.ht_supported = true;
	net->band.ht_cap.cap = IEEE80211_HT_CAP_SGI_20;
	net->band.ht_cap.ampdu_factor = 3;
	net->band.ht_cap.mcs.rx_mask[0] = 0xff;
	net->band.ht_cap.mcs.rx_mask[1] = 0xff;
	net->band.ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED |
		IEEE80211_HT_MCS_TX_RX_DIFF;
	wiphy->bands[NL80211_BAND_2GHZ] = &net->band;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_NONE;
	/* Retain the operator's country policy instead of accepting AP hints. */
	wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	wiphy->max_scan_ssids = 1;
	wiphy->max_scan_ie_len = 0;
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
	dev->ethtool_ops = &wifi_net_ethtool_ops;
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
	dev_info(&func->dev, "RTL8712S SDIO station interface registered\n");
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
	WRITE_ONCE(net->removing, true);
	WRITE_ONCE(net->stopping, true);
	cancel_delayed_work_sync(&net->recovery_work);
	unregister_netdev(net->dev);
	cancel_delayed_work_sync(&net->reorder_work);
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
#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS
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
			r->rx.bytes, r->rx.frames, r->rx.crc_errors, r->rx.icv_errors,
			r->rx.high_water, net->associated, net->authorized,
			net->join_result, net->rx_replay, net->rx_mic,
			net->rx_rejected, net->tx_failed);
	mutex_unlock(&owner->lock);
	return size;
}
static DEVICE_ATTR_RO(network_result);
#endif
