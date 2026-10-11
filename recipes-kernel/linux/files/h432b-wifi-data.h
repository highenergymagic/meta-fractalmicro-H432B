/* SPDX-License-Identifier: GPL-2.0-only */
/* Station data path: bounded SDIO queues and host-verified CCMP. */
#include "h432b-wifi-tx.h"
static const u32 wifi_net_ciphers[] = { WLAN_CIPHER_SUITE_CCMP };

struct wifi_rx_frame {
	u64 pn;
	u8 addresses[2 * ETH_ALEN];
	u8 tid;
	u8 key;
	bool encrypted;
	bool amsdu;
};

static void wifi_net_deliver(struct h432b_wifi_net *net, struct sk_buff *skb)
{
	net->dev->stats.rx_packets++;
	net->dev->stats.rx_bytes += skb->len;
	skb->protocol = eth_type_trans(skb, net->dev);
	netif_rx(skb);
}

/* Called under owner->lock, either directly or by the reorder window. */
static void wifi_net_rx_release(void *context, void *frame)
{
	struct h432b_wifi_net *net = context;
	struct sk_buff *skb = frame;
	struct wifi_rx_frame metadata = *(struct wifi_rx_frame *)skb->cb;
	struct h432b_wifi_key *key = &net->keys[metadata.key];
	struct sk_buff_head list;
	u16 protocol;

	if (metadata.encrypted && (!key->tfm || metadata.pn <= key->rx_pn[metadata.tid])) {
		net->rx_replay++;
		goto reject;
	}
	if (metadata.amsdu) {
		if (!metadata.encrypted || !net->authorized || skb->len > WIFI_HT_MAX_AMSDU)
			goto reject;
		__skb_queue_head_init(&list);
		/* Includes the RFC1042 aggregation-injection mitigation. The outer
		 * SA identifies the AP, not every bridged station inside the A-MSDU.
		 */
		ieee80211_amsdu_to_8023s(skb, &list, net->dev->dev_addr,
					 NL80211_IFTYPE_STATION, 0,
					net->dev->dev_addr, NULL, 0);
		if (skb_queue_empty(&list)) {
			net->dev->stats.rx_dropped++;
			net->rx_rejected++;
			return;
		}
		key->rx_pn[metadata.tid] = metadata.pn;
		while ((skb = __skb_dequeue(&list)))
			wifi_net_deliver(net, skb);
		return;
	}
	if (skb->len < 8 || memcmp(skb->data, "\xaa\xaa\x03\x00\x00\x00", 6))
		goto reject;
	protocol = get_unaligned_be16(skb->data + 6);
	if ((!metadata.encrypted || !net->authorized) && protocol != ETH_P_PAE)
		goto reject;
	if (metadata.encrypted)
		key->rx_pn[metadata.tid] = metadata.pn;
	skb_pull(skb, 6);
	skb_push(skb, 2 * ETH_ALEN);
	memcpy(skb->data, metadata.addresses, sizeof(metadata.addresses));
	wifi_net_deliver(net, skb);
	return;
reject:
	net->dev->stats.rx_dropped++;
	net->rx_rejected++;
	dev_kfree_skb(skb);
}

static void wifi_net_rx_discard(void *context, void *frame)
{
	struct h432b_wifi_net *net = context;

	net->rx_reorder_dropped++;
	net->dev->stats.rx_dropped++;
	dev_kfree_skb(frame);
}

static void wifi_net_reorder_clear(struct h432b_wifi_net *net, bool disable)
{
	unsigned int tid;

	cancel_delayed_work(&net->reorder_work);
	for (tid = 0; tid < ARRAY_SIZE(net->reorder); tid++) {
		bool enabled = net->reorder[tid].enabled;

		wifi_reorder_clear(&net->reorder[tid], wifi_net_rx_discard, net);
		net->reorder[tid].enabled = enabled && !disable;
		net->reorder_deadline[tid] = 0;
	}
}

static void wifi_net_reorder_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(to_delayed_work(work),
						struct h432b_wifi_net, reorder_work);
	unsigned long deadline = 0, next;
	unsigned int tid;

	mutex_lock(&net->owner->lock);
	if (net->stopping || !net->associated)
		goto out;
	for (tid = 0; tid < ARRAY_SIZE(net->reorder); tid++) {
		struct wifi_reorder *window = &net->reorder[tid];

		if (!window->pending) {
			net->reorder_deadline[tid] = 0;
			continue;
		}
		next = net->reorder_deadline[tid];
		if (time_after_eq(jiffies, next)) {
			wifi_reorder_expire(window, wifi_net_rx_release, net);
			next = jiffies + msecs_to_jiffies(WIFI_REORDER_TIMEOUT_MS);
			net->reorder_deadline[tid] = next;
		}
		if (window->pending && (!deadline || time_before(next, deadline)))
			deadline = next;
	}
	if (deadline)
		queue_delayed_work(net->workqueue, &net->reorder_work,
				   time_after(deadline, jiffies) ? deadline - jiffies : 0);
out:
	mutex_unlock(&net->owner->lock);
}

static void wifi_net_clear_keys(struct h432b_wifi_net *net)
{
	unsigned int i;

	wifi_net_reorder_clear(net, true);
	for (i = 0; i < ARRAY_SIZE(net->keys); i++) {
		if (net->keys[i].tfm)
			crypto_free_aead(net->keys[i].tfm);
		memzero_explicit(&net->keys[i], sizeof(net->keys[i]));
	}
}

static int wifi_net_add_key(struct wiphy *wiphy, struct net_device *dev,
			    int link_id, u8 index, bool pairwise, const u8 *mac,
			    struct key_params *params)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	struct h432b_wifi_key *key;
	struct crypto_aead *tfm;
	u64 initial = 0;
	unsigned int i;
	int error;

	if (link_id >= 0 || index > 3 || (pairwise && index) ||
	    params->cipher != WLAN_CIPHER_SUITE_CCMP || params->key_len != 16 ||
	    (params->seq_len && params->seq_len != 6))
		return -EOPNOTSUPP;
	mutex_lock(&net->owner->lock);
	if (!net->associated || (pairwise && (!mac || !ether_addr_equal(mac, net->bssid)))) {
		error = -ENOTCONN;
		goto out;
	}
	key = &net->keys[pairwise ? 4 : index];
	/* Reinstalling the same key must NEVER reset replay or transmit PNs. */
	if (key->tfm && !memcmp(key->material, params->key, 16)) {
		error = 0;
		goto out;
	}
	tfm = crypto_alloc_aead("ccm(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		error = PTR_ERR(tfm);
		goto out;
	}
	error = crypto_aead_setkey(tfm, params->key, 16);
	if (!error)
		error = crypto_aead_setauthsize(tfm, 8);
	if (error) {
		crypto_free_aead(tfm);
		goto out;
	}
	/* A queued plaintext frame must not outlive the key that authenticated it. */
	wifi_net_reorder_clear(net, false);
	if (key->tfm)
		crypto_free_aead(key->tfm);
	memzero_explicit(key, sizeof(*key));
	key->tfm = tfm;
	memcpy(key->material, params->key, 16);
	for (i = 0; i < params->seq_len; i++)
		initial |= (u64)params->seq[i] << (8 * i);
	for (i = 0; i < ARRAY_SIZE(key->rx_pn); i++)
		key->rx_pn[i] = initial;
out:
	mutex_unlock(&net->owner->lock);
	return error;
}

static int wifi_net_del_key(struct wiphy *wiphy, struct net_device *dev,
			    int link_id, u8 index, bool pairwise, const u8 *mac)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	struct h432b_wifi_key *key;

	if (link_id >= 0 || index > 3 || (pairwise && index))
		return -EINVAL;
	mutex_lock(&net->owner->lock);
	key = &net->keys[pairwise ? 4 : index];
	wifi_net_reorder_clear(net, false);
	if (key->tfm)
		crypto_free_aead(key->tfm);
	memzero_explicit(key, sizeof(*key));
	if (pairwise)
		net->authorized = false;
	mutex_unlock(&net->owner->lock);
	return 0;
}

static int wifi_net_default_key(struct wiphy *wiphy, struct net_device *dev,
				int link_id, u8 index, bool unicast, bool multicast)
{
	/* Station transmits even multicast MSDUs with the AP pairwise key. */
	return link_id < 0 && index < 4 ? 0 : -EINVAL;
}

static int wifi_net_change_station(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *mac, struct station_parameters *params)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	int error = 0;

	if (params->sta_flags_mask & ~BIT(NL80211_STA_FLAG_AUTHORIZED))
		return -EOPNOTSUPP;
	mutex_lock(&net->owner->lock);
	if (!net->associated || !ether_addr_equal(mac, net->bssid)) {
		error = -ENOTCONN;
	} else if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		if ((params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) && !net->keys[4].tfm)
			error = -ENOKEY;
		else
			net->authorized = params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED);
	}
	mutex_unlock(&net->owner->lock);
	return error;
}

static int wifi_net_get_station(struct wiphy *wiphy, struct net_device *dev,
				const u8 *mac, struct station_info *info)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	int error = 0;

	mutex_lock(&net->owner->lock);
	if (!net->associated || !ether_addr_equal(mac, net->bssid)) {
		error = -ENOENT;
		goto out;
	}
	info->filled = BIT_ULL(NL80211_STA_INFO_RX_PACKETS) |
		BIT_ULL(NL80211_STA_INFO_TX_PACKETS);
	info->rx_packets = dev->stats.rx_packets;
	info->tx_packets = dev->stats.tx_packets;
	if (net->rx_legacy_rate) {
		info->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);
		info->rxrate.legacy = net->rx_legacy_rate;
	}
out:
	mutex_unlock(&net->owner->lock);
	return error;
}

/* Owner mutex and MMC host held; callback data lifetime is one FIFO drain. */
static void wifi_net_receive(void *context, const u8 *input, unsigned int length,
			     u32 rate)
{
	struct h432b_wifi_net *net = context;
	struct h432b_wifi_key *key = NULL;
	struct wifi_rx_frame *metadata;
	struct wifi_reorder *window;
	struct sk_buff *skb;
	u8 *frame, *iv;
	unsigned int header, size, tid, index;
	u16 fc, sequence;
	u64 pn = 0;
	bool encrypted, multicast, amsdu;
	int error;

	BUILD_BUG_ON(sizeof(*metadata) > sizeof(skb->cb));
	if (!net->associated || length < 32 || length > WIFI_HT_MAX_AMSDU + 42)
		goto reject;
	fc = get_unaligned_le16(input);
	/* Three-address data only; no null, fragmented or HT-control frames. */
	if ((fc & 0x000f) != 8 || (fc & 0x0300) != 0x0200 ||
	    (fc & (0x0040 | 0x0400 | 0x8000)) ||
	    (input[22] & 15) || !ether_addr_equal(input + 10, net->bssid))
		goto reject;
	header = fc & 0x0080 ? 26 : 24;
	tid = header == 26 ? input[24] & 15 : 16;
	amsdu = header == 26 && (input[24] & 0x80);
	if (amsdu && !net->ht.ht)
		goto reject;
	multicast = is_multicast_ether_addr(input + 4);
	if (!multicast && !ether_addr_equal(input + 4, net->dev->dev_addr))
		goto reject;
	skb = alloc_skb(length + NET_IP_ALIGN, GFP_KERNEL);
	if (!skb)
		goto reject;
	skb_reserve(skb, NET_IP_ALIGN);
	frame = skb_put_data(skb, input, length);
	size = length - header;
	encrypted = fc & 0x4000;
	index = 0;
	if (encrypted) {
		/* RXDEC is disabled; verify every MIC in software regardless of the
		 * descriptor decryption-status bit.
		 */
		if (size < 24)
			goto free_reject;
		iv = frame + header;
		if (iv[2] || (iv[3] & 0x3f) != 0x20)
			goto free_reject;
		index = iv[3] >> 6;
		if (!multicast && index)
			goto free_reject;
		index = multicast ? index : 4;
		key = &net->keys[index];
		pn = wifi_ccmp_pn(iv);
		if (!key->tfm || !pn || pn <= key->rx_pn[tid]) {
			net->rx_replay++;
			goto free_reject;
		}
		error = wifi_ccmp_crypt(key, frame, header, size - 16, true);
		if (error) {
			net->rx_mic++;
			goto free_reject;
		}
		size -= 16;
		if (rate & BIT(6)) {
			int mcs = wifi_rx_ht_mcs(rate);

			net->rx_ht_authenticated++;
			if (mcs >= 0)
				net->rx_ht_mcs[mcs]++;
		}
		if (amsdu)
			net->rx_amsdu_authenticated++;
	}
	metadata = (struct wifi_rx_frame *)skb->cb;
	memset(metadata, 0, sizeof(*metadata));
	metadata->pn = pn;
	metadata->tid = tid;
	metadata->key = index;
	metadata->encrypted = encrypted;
	metadata->amsdu = amsdu;
	memcpy(metadata->addresses, frame + 4, ETH_ALEN);
	memcpy(metadata->addresses + ETH_ALEN, frame + 16, ETH_ALEN);
	sequence = get_unaligned_le16(frame + 22) >> 4;
	skb->dev = net->dev;
	skb->priority = tid < 16 ? tid : 0;
	skb_pull(skb, header + (encrypted ? 8 : 0));
	skb_trim(skb, size);
	/* This is observed RX metadata, not a claimed TX/ACK success rate. */
	net->rx_legacy_rate = wifi_rx_legacy_rate(rate);
	if (tid == 16 || multicast || !encrypted || !net->reorder[tid].enabled) {
		wifi_net_rx_release(net, skb);
		return;
	}
	window = &net->reorder[tid];
	if (!wifi_reorder_insert(window, sequence, skb, wifi_net_rx_release, net)) {
		net->rx_reorder_dropped++;
		goto free_reject;
	}
	if (window->pending && !net->reorder_deadline[tid]) {
		net->reorder_deadline[tid] = jiffies + msecs_to_jiffies(WIFI_REORDER_TIMEOUT_MS);
		queue_delayed_work(net->workqueue, &net->reorder_work,
				   msecs_to_jiffies(WIFI_REORDER_TIMEOUT_MS));
	} else if (!window->pending) {
		net->reorder_deadline[tid] = 0;
	}
	return;
free_reject:
	dev_kfree_skb(skb);
reject:
	net->dev->stats.rx_dropped++;
	net->rx_rejected++;
}

/* One packet per incrementing block CMD53 on its WMM access-category FIFO. */
static int wifi_net_send_frame(struct h432b_wifi_net *net, struct sk_buff *skb)
{
	struct h432b_wifi_key *key = &net->keys[4];
	u8 *packet, *frame, *payload, public, available, tid;
	unsigned int length, transfer, pages, tries, offset, header;
	bool encrypted, basic, qos;
	int error = 0;

	if (!net->associated || net->stopping || net->faulted)
		return -ENOTCONN;
	if (skb_linearize(skb))
		return -ENOMEM;
	if (!net->authorized && skb->protocol != htons(ETH_P_PAE))
		return -EACCES;
	encrypted = key->tfm;
	if (!encrypted && skb->protocol != htons(ETH_P_PAE))
		return -ENOKEY;
	qos = net->ht.qos;
	tid = qos ? skb->priority & 7 : 0;
	header = qos ? 26 : 24;
	basic = wifi_tx_basic_rate(skb->data, skb->len);
	length = header + 8 + skb->len - ETH_HLEN + (encrypted ? 16 : 0);
	transfer = ALIGN(32 + length, 512);
	packet = kzalloc(transfer, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;
	frame = packet + 32;
	put_unaligned_le16(0x0108 | (qos ? 0x80 : 0) | (encrypted ? 0x4000 : 0), frame);
	memcpy(frame + 4, net->bssid, ETH_ALEN);
	memcpy(frame + 10, net->dev->dev_addr, ETH_ALEN);
	memcpy(frame + 16, skb->data, ETH_ALEN);
	put_unaligned_le16((net->tx_sequence[qos ? tid : 16]++ & 0xfff) << 4, frame + 22);
	if (qos)
		frame[24] = tid;
	offset = header;
	if (encrypted) {
		if (key->tx_pn >= 0xffffffffffffULL) {
			error = -EOVERFLOW;
			goto out;
		}
		wifi_ccmp_iv(frame + offset, ++key->tx_pn, 0);
		offset += 8;
	}
	payload = frame + offset;
	memcpy(payload, "\xaa\xaa\x03\x00\x00\x00", 6);
	memcpy(payload + 6, skb->data + 12, skb->len - 12);
	if (encrypted) {
		error = wifi_ccmp_crypt(key, frame, header, length - header - 16, false);
		if (error)
			goto out;
	}
	wifi_tx_descriptor(packet, length, basic, qos, tid,
			   get_unaligned_le16(frame + 22) >> 4);
	if (net->ht.ht && net->authorized && !basic && !(net->tx_ba_requested & BIT(tid))) {
		u8 parameters[4] = { tid };

		/* Firmware owns ADDBA negotiation and aggregate formation. */
		error = wifi_h2c_send(net->func, &net->owner->command, 45,
				      parameters, sizeof(parameters));
		if (error) {
			if (wifi_net_recoverable(error))
				wifi_net_fault(net, error);
			goto out;
		}
		net->tx_ba_requested |= BIT(tid);
	}
	pages = transfer / 256;
	for (tries = 0; tries < 100; tries++) {
		public = wifi_sdio_readb(net->func, 1, &error);
		if (error)
			goto out;
		available = wifi_sdio_readb(net->func, wifi_tx_free_pages(tid), &error);
		if (error)
			goto out;
		if (available >= public && available > pages + 5)
			break;
		sdio_release_host(net->func);
		usleep_range(1000, 2000);
		sdio_claim_host(net->func);
	}
	if (tries == 100) {
		error = -ENOSPC;
		goto out;
	}
	error = wifi_sdio_blocks(net->func, true, wifi_tx_fifo(tid),
				 packet, transfer / 512);
	if (error) {
		/* Do not retry a partially consumed packet or reuse its CCMP PN. */
		wifi_net_fault(net, error);
	}
out:
	if (wifi_net_recoverable(error) && !net->faulted)
		wifi_net_fault(net, error);
	kfree_sensitive(packet);
	return error;
}

static void wifi_net_tx_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(work, struct h432b_wifi_net, tx_work);
	struct sk_buff *skb;
	unsigned int limit = 4;
	unsigned long flags;
	int error, receive_error;

	mutex_lock(&net->owner->lock);
	sdio_claim_host(net->func);
	while (limit-- && (skb = skb_dequeue(&net->tx_queue))) {
		error = wifi_net_send_frame(net, skb);
		if (error) {
			net->tx_failed++;
			net->dev->stats.tx_dropped++;
		} else {
			net->dev->stats.tx_packets++;
			net->dev->stats.tx_bytes += skb->len;
		}
		dev_kfree_skb_any(skb);
		if (net->faulted) {
			int mask_error;

			sdio_writew(net->func, 0, WIFI_HIMR, &mask_error);
			wifi_net_link_down(net, WLAN_REASON_UNSPECIFIED, true);
			break;
		}
		/* TCP ACKs and key events must not wait behind a whole TX queue.
		 * Both FIFO consumers remain serialized by the same owner lock.
		 */
		receive_error = wifi_command_drain(net->func, &net->owner->command,
						   net->event_buffer);
		if (receive_error > 0)
			net->consecutive_empty = 0;
		if (receive_error >= 0)
			receive_error = wifi_rx_drain(net->func, &net->owner->command.rx,
						      net->event_buffer);
		if (receive_error > 0)
			net->consecutive_empty = 0;
		if (receive_error < 0) {
			int mask_error;

			sdio_writew(net->func, 0, WIFI_HIMR, &mask_error);
			wifi_net_fault(net, receive_error);
			break;
		}
	}
	sdio_release_host(net->func);
	mutex_unlock(&net->owner->lock);
	spin_lock_irqsave(&net->tx_queue.lock, flags);
	if (!READ_ONCE(net->stopping) && !READ_ONCE(net->faulted) &&
	    READ_ONCE(net->associated) && skb_queue_len(&net->tx_queue) < 32)
		netif_wake_queue(net->dev);
	spin_unlock_irqrestore(&net->tx_queue.lock, flags);
	if (!net->stopping && !net->faulted && !skb_queue_empty(&net->tx_queue))
		queue_work(net->workqueue, &net->tx_work);
}
