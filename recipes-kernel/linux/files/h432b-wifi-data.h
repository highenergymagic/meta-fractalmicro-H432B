/* SPDX-License-Identifier: GPL-2.0-only */
/* Station data path: bounded SDIO queues and host-verified CCMP. */
static const u32 wifi_net_ciphers[] = { WLAN_CIPHER_SUITE_CCMP };

static void wifi_net_clear_keys(struct h432b_wifi_net *net)
{
	unsigned int i;

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
	if (!net->associated || !ether_addr_equal(mac, net->bssid))
		error = -ENOTCONN;
	else if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		if ((params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) && !net->keys[4].tfm)
			error = -ENOKEY;
		else
			net->authorized = !!(params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED));
	}
	mutex_unlock(&net->owner->lock);
	return error;
}

static int wifi_net_get_station(struct wiphy *wiphy, struct net_device *dev,
				const u8 *mac, struct station_info *info)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);

	if (!net->associated || !ether_addr_equal(mac, net->bssid))
		return -ENOENT;
	info->filled = BIT_ULL(NL80211_STA_INFO_RX_PACKETS) |
		BIT_ULL(NL80211_STA_INFO_TX_PACKETS);
	info->rx_packets = dev->stats.rx_packets;
	info->tx_packets = dev->stats.tx_packets;
	return 0;
}

/* Owner mutex and MMC host held; callback data lifetime is one FIFO drain. */
static void wifi_net_receive(void *context, const u8 *input, unsigned int length,
			      u32 descriptor)
{
	struct h432b_wifi_net *net = context;
	struct h432b_wifi_key *key = NULL;
	struct sk_buff *skb;
	u8 *frame, *payload, *iv;
	unsigned int header, size, tid, index;
	u16 fc, protocol;
	u64 pn = 0;
	bool encrypted, multicast;
	int error;

	if (!net->associated || length < 32 || length > 2400)
		goto reject;
	fc = get_unaligned_le16(input);
	/* Three-address data from this AP only; reject null/fragment/A-MSDU/HT
	 * control frames rather than misinterpreting their payload boundaries.
	 */
	if ((fc & 0x000f) != 8 || (fc & 0x0300) != 0x0200 ||
	    (fc & (0x0040 | 0x0400 | 0x8000)) ||
	    (input[22] & 15) || !ether_addr_equal(input + 10, net->bssid))
		goto reject;
	header = fc & 0x0080 ? 26 : 24;
	tid = header == 26 ? input[24] & 15 : 16;
	if (header == 26 && (input[24] & 0x80))
		goto reject;
	multicast = is_multicast_ether_addr(input + 4);
	if (!multicast && !ether_addr_equal(input + 4, net->dev->dev_addr))
		goto reject;
	frame = kmemdup(input, length, GFP_KERNEL);
	if (!frame)
		goto reject;
	size = length - header;
	payload = frame + header;
	encrypted = fc & 0x4000;
	if (encrypted) {
		/* RXDEC is disabled; verify every MIC in software regardless of the
		 * unqualified descriptor decryption-status bit. */
		if (size < 24)
			goto free_reject;
		iv = payload;
		if (iv[2] || (iv[3] & 0x3f) != 0x20)
			goto free_reject;
		index = iv[3] >> 6;
		if (!multicast && index)
			goto free_reject;
		key = &net->keys[multicast ? index : 4];
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
		payload += 8;
		size -= 16;
	}
	if (size < 8 || memcmp(payload, "\xaa\xaa\x03\x00\x00\x00", 6))
		goto free_reject;
	protocol = get_unaligned_be16(payload + 6);
	/* Never deliver plaintext IP/ARP; unauthenticated port is EAPOL-only. */
	if ((!encrypted || !net->authorized) && protocol != ETH_P_PAE)
		goto free_reject;
	if (key)
		key->rx_pn[tid] = pn; /* commit only after MIC and framing checks */
	skb = netdev_alloc_skb_ip_align(net->dev, ETH_HLEN + size - 8);
	if (!skb)
		goto free_reject;
	skb_put_data(skb, frame + 4, ETH_ALEN);
	skb_put_data(skb, frame + 16, ETH_ALEN);
	skb_put_data(skb, payload + 6, size - 6);
	skb->protocol = eth_type_trans(skb, net->dev);
	net->dev->stats.rx_packets++;
	net->dev->stats.rx_bytes += ETH_HLEN + size - 8;
	netif_rx(skb);
	kfree_sensitive(frame);
	return;
free_reject:
	kfree_sensitive(frame);
reject:
	net->dev->stats.rx_dropped++;
	net->rx_rejected++;
}

/* One packet per incrementing block CMD53 on the best-effort FIFO. */
static int wifi_net_send_frame(struct h432b_wifi_net *net, struct sk_buff *skb)
{
	struct h432b_wifi_key *key = &net->keys[4];
	u8 *packet, *frame, *payload, public, available;
	unsigned int length, transfer, pages, tries, offset;
	bool encrypted;
	int error = 0;

	if (!net->associated || net->stopping || net->faulted)
		return -ENOTCONN;
	if (skb_linearize(skb))
		return -ENOMEM;
	if (!net->authorized && skb->protocol != htons(ETH_P_PAE))
		return -EACCES;
	encrypted = key->tfm != NULL;
	if (!encrypted && skb->protocol != htons(ETH_P_PAE))
		return -ENOKEY;
	length = 24 + 8 + skb->len - ETH_HLEN + (encrypted ? 16 : 0);
	transfer = ALIGN(32 + length, 512);
	packet = kzalloc(transfer, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;
	frame = packet + 32;
	put_unaligned_le16(0x0108 | (encrypted ? 0x4000 : 0), frame);
	memcpy(frame + 4, net->bssid, ETH_ALEN);
	memcpy(frame + 10, net->dev->dev_addr, ETH_ALEN);
	memcpy(frame + 16, skb->data, ETH_ALEN);
	put_unaligned_le16((net->tx_sequence++ & 0xfff) << 4, frame + 22);
	offset = 24;
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
		error = wifi_ccmp_crypt(key, frame, 24, length - 40, false);
		if (error)
			goto out;
	}
	put_unaligned_le32(0x8c200000 | length, packet);
	put_unaligned_le32(5 | BIT(16), packet + 4); /* station CAM id, non-QoS */
	put_unaligned_le32(BIT(31), packet + 16); /* driver-selected legacy rate */
	put_unaligned_le32(0x001f8000, packet + 20); /* 1 Mb/s, no fallback */
	pages = transfer / 256;
	for (tries = 0; tries < 100; tries++) {
		public = wifi_sdio_readb(net->func, 1, &error);
		if (error)
			goto out;
		available = wifi_sdio_readb(net->func, 7, &error);
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
	error = mmc_io_rw_extended(net->func->card, 1, net->func->num,
				  0x18dc0, 1, packet, transfer / 512, 512);
	if (error) {
		/* Do not retry a partially consumed packet or reuse its CCMP PN. */
		net->faulted = true;
		net->last_error = error;
	}
out:
	kfree_sensitive(packet);
	return error;
}

static void wifi_net_tx_work(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(work, struct h432b_wifi_net, tx_work);
	struct sk_buff *skb;
	unsigned int limit = 4;
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

			net->faulted = true;
			net->last_error = receive_error;
			sdio_writew(net->func, 0, WIFI_HIMR, &mask_error);
			wifi_net_link_down(net, WLAN_REASON_UNSPECIFIED, true);
			break;
		}
	}
	sdio_release_host(net->func);
	mutex_unlock(&net->owner->lock);
	if (!net->stopping && !net->faulted && net->associated)
		netif_wake_queue(net->dev);
	if (!net->stopping && !net->faulted && !skb_queue_empty(&net->tx_queue))
		queue_work(net->workqueue, &net->tx_work);
}
