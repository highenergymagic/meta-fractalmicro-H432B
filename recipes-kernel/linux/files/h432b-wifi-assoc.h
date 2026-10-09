/* SPDX-License-Identifier: GPL-2.0-only */
/* Firmware-assisted station association. Included after net state helpers. */

static void wifi_net_link_down(struct h432b_wifi_net *net, u16 reason, bool local)
{
	bool connected = net->associated;

	net->associated = false;
	net->authorized = false;
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
	if (error) {
		net->last_error = error;
		WRITE_ONCE(net->faulted, true);
	}
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
	unsigned int i, channel;
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
	    params->mfp != NL80211_MFP_NO || params->ie_len > sizeof(net->join_ie))
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
	/* Firmware builds SSID/rate IEs itself. Only fixed beacon fields plus
	 * the supplicant's negotiated RSN IE belong in this command.
	 */
	memcpy(join, selected, 128);
	put_unaligned_le32(128 + params->ie_len, join);
	put_unaligned_le32(12 + params->ie_len, join + 112);
	memcpy(join + 128, params->ie, params->ie_len);
	memcpy(net->bssid, selected + 4, ETH_ALEN);
	memcpy(net->join_ie, params->ie, params->ie_len);
	net->join_ie_len = params->ie_len;
	wifi_net_clear_keys(net);
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
		WRITE_ONCE(net->faulted, true);
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
	if (error) {
		net->last_error = error;
		WRITE_ONCE(net->faulted, true);
	}
	mutex_unlock(&net->owner->lock);
	return error;
}
