/* SPDX-License-Identifier: GPL-2.0-only */
/* Firmware-assisted station association. Included after net state helpers. */

static void wifi_net_link_down(struct h432b_wifi_net *net, u16 reason, bool local)
{
	bool connected = net->associated;

	net->associated = false;
	net->authorized = false;
	net->rx_legacy_rate = 0;
	net->tx_ba_requested = 0;
	memset(&net->ht, 0, sizeof(net->ht));
	wifi_net_clear_keys(net);
	skb_queue_purge(&net->tx_queue);
	netif_carrier_off(net->dev);
	netif_tx_disable(net->dev);
	if (net->connecting) {
		net->connecting = false;
		cfg80211_connect_result(net->dev, net->bssid, net->join_ie,
					net->join_ie_len, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_KERNEL);
	} else if (connected) {
		cfg80211_disconnected(net->dev, reason, NULL, 0, local, GFP_KERNEL);
	}
}

static int wifi_net_event(void *context, u8 code, const u8 *data, unsigned int length)
{
	struct h432b_wifi_net *net = context;
	int result;

	if (code == 10) {
		/* 32-bit firmware wlan_network: list[2], type, fixed, timestamp,
		 * AID, signed join result, then BSSID_EX at byte 28.
		 */
		if (length < 156)
			return -EBADMSG;
		result = (s32)get_unaligned_le32(data + 24);
		net->join_result = result;
		dev_dbg(&net->func->dev, "association result=%d bytes=%u\n", result, length);
		if (!net->connecting)
			return 0;
		if (!ether_addr_equal(data + 32, net->bssid))
			return -EBADMSG;
		cancel_delayed_work(&net->join_timeout);
		if (result <= 0) {
			wifi_net_link_down(net, WLAN_REASON_UNSPECIFIED, false);
			return 0;
		}
		net->connecting = false;
		net->associated = true;
		net->authorized = false;
		cfg80211_connect_result(net->dev, net->bssid, net->join_ie,
					net->join_ie_len, NULL, 0, WLAN_STATUS_SUCCESS,
					GFP_KERNEL);
		netif_carrier_on(net->dev);
		netif_start_queue(net->dev);
	} else if (code == 25) {
		unsigned int tid;
		struct wifi_reorder *window;

		/* ADDBA report: peer MAC[6], starting sequence LE16, TID byte.
		 * Like the vendor driver, accept the first authenticated sequence:
		 * some APs reset sequence numbers after the four-way handshake.
		 */
		if (length < 9 || data[8] >= ARRAY_SIZE(net->reorder))
			return -EBADMSG;
		if (!net->associated || !net->ht.ht || !ether_addr_equal(data, net->bssid))
			return 0;
		tid = data[8];
		net->rx_addba_reports++;
		window = &net->reorder[tid];
		wifi_reorder_clear(window, wifi_net_rx_discard, net);
		window->enabled = true;
		net->reorder_deadline[tid] = 0;
	} else if (code == 12 && length >= ETH_ALEN &&
		   ether_addr_equal(data, net->bssid)) {
		cancel_delayed_work(&net->join_timeout);
		wifi_net_link_down(net, WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY, false);
	}
	return 0;
}

static void wifi_net_join_timeout(struct work_struct *work)
{
	struct h432b_wifi_net *net = container_of(to_delayed_work(work),
						 struct h432b_wifi_net, join_timeout);
	u8 disconnect[4] = {};
	int error;

	mutex_lock(&net->owner->lock);
	if (!net->connecting)
		goto out;
	sdio_claim_host(net->func);
	error = wifi_h2c_send(net->func, &net->owner->command, 15,
			      disconnect, sizeof(disconnect));
	sdio_release_host(net->func);
	if (wifi_net_recoverable(error))
		wifi_net_fault(net, error);
	wifi_net_link_down(net, WLAN_REASON_UNSPECIFIED, true);
out:
	mutex_unlock(&net->owner->lock);
}

static int wifi_net_connect(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_connect_params *params)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	const u8 *selected = NULL;
	u8 *join, mode[4] = { 1 }, auth[4] = { 2 };
	unsigned int i, channel, ie_length;
	int error = 0;

	/* No WEP, TKIP, WPA1, SAE, PMF or firmware-offloaded credentials. */
	if (!params->ssid_len || params->ssid_len > 32 ||
	    params->auth_type != NL80211_AUTHTYPE_OPEN_SYSTEM ||
	    params->crypto.wpa_versions != NL80211_WPA_VERSION_2 ||
	    params->crypto.n_ciphers_pairwise != 1 ||
	    params->crypto.ciphers_pairwise[0] != WLAN_CIPHER_SUITE_CCMP ||
	    params->crypto.cipher_group != WLAN_CIPHER_SUITE_CCMP ||
	    params->crypto.n_akm_suites != 1 ||
	    params->crypto.akm_suites[0] != WLAN_AKM_SUITE_PSK ||
	    params->mfp != NL80211_MFP_NO ||
	    !wifi_ht_assoc_ies_valid(params->ie, params->ie_len))
		return -EOPNOTSUPP;
	join = kzalloc(884, GFP_KERNEL);
	if (!join)
		return -ENOMEM;
	mutex_lock(&net->owner->lock);
	if (net->stopping || net->faulted || !netif_running(dev)) {
		error = -ENETDOWN;
		goto out;
	}
	if (net->connecting || net->associated || net->request) {
		error = -EBUSY;
		goto out;
	}
	for (i = 0; i < WIFI_BSS_CACHE; i++) {
		const u8 *candidate = net->cache[i].data;

		if (!net->cache[i].valid ||
		    time_after(jiffies, net->cache[i].seen + 30 * HZ) ||
		    get_unaligned_le32(candidate + 12) != params->ssid_len ||
		    memcmp(candidate + 16, params->ssid, params->ssid_len) ||
		    (params->bssid && !ether_addr_equal(candidate + 4, params->bssid)))
			continue;
		channel = get_unaligned_le32(candidate + 72);
		if (channel < 1 || channel > 13 ||
		    net->channels[channel - 1].flags & IEEE80211_CHAN_DISABLED ||
		    (params->channel && params->channel->hw_value != channel))
			continue;
		selected = candidate;
		break;
	}
	if (!selected) {
		error = -ENOENT;
		goto out;
	}
	error = wifi_ht_select(selected + 128, get_unaligned_le32(selected + 112) - 12,
			       &net->ht);
	if (error)
		goto out;
	channel = get_unaligned_le32(selected + 72);
	if (net->channels[channel - 1].flags & IEEE80211_CHAN_NO_HT40)
		net->ht.capability[0] &= ~(BIT(1) | BIT(6));
	/* Firmware builds SSID/rate IEs; the host supplies negotiated RSN and
	 * its own WMM/HT capabilities, never a copy of the AP's capability IE.
	 */
	memcpy(join, selected, 128);
	memcpy(join + 128, params->ie, params->ie_len);
	ie_length = params->ie_len + wifi_ht_join_ies(join + 128 + params->ie_len, &net->ht);
	put_unaligned_le32(128 + ie_length, join);
	put_unaligned_le32(12 + ie_length, join + 112);
	memcpy(net->bssid, selected + 4, ETH_ALEN);
	memcpy(net->join_ie, join + 128, ie_length);
	net->join_ie_len = ie_length;
	wifi_net_clear_keys(net);
	memset(net->tx_sequence, 0, sizeof(net->tx_sequence));
	net->tx_ba_requested = 0;
	net->join_result = 0;
	net->connecting = true;
	net->authorized = false;
	sdio_claim_host(net->func);
	/* Host CCMP owns both encryption and authentication; leave CAM unused. */
	wifi_write(net->func, 1, 0x250, 0, &error);
	if (!error && wifi_read(net->func, 1, 0x250, &error))
		error = -EIO;
	if (!error)
		error = wifi_h2c_send(net->func, &net->owner->command, 17, mode, sizeof(mode));
	if (!error)
		error = wifi_h2c_send(net->func, &net->owner->command, 19, auth, sizeof(auth));
	if (!error)
		error = wifi_h2c_send(net->func, &net->owner->command, 14, join, 884);
	sdio_release_host(net->func);
	if (error) {
		net->connecting = false;
		net->last_error = error;
		if (wifi_net_recoverable(error))
			wifi_net_fault(net, error);
	} else {
		queue_delayed_work(net->workqueue, &net->join_timeout, 20 * HZ);
		queue_delayed_work(net->workqueue, &net->event_work, 0);
	}
out:
	mutex_unlock(&net->owner->lock);
	kfree_sensitive(join);
	return error;
}

static int wifi_net_disconnect(struct wiphy *wiphy, struct net_device *dev,
			       u16 reason)
{
	struct h432b_wifi_net *net = wiphy_priv(wiphy);
	u8 parameters[4] = {};
	int error = 0;

	cancel_delayed_work_sync(&net->join_timeout);
	mutex_lock(&net->owner->lock);
	if (!net->stopping && !net->faulted && (net->connecting || net->associated)) {
		sdio_claim_host(net->func);
		error = wifi_h2c_send(net->func, &net->owner->command, 15,
				      parameters, sizeof(parameters));
		sdio_release_host(net->func);
	}
	wifi_net_link_down(net, reason, true);
	if (wifi_net_recoverable(error))
		wifi_net_fault(net, error);
	mutex_unlock(&net->owner->lock);
	return error;
}
