/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712 data descriptors follow rtl8712_xmit.c in Linux and Realtek's
 * SDIO driver: Copyright(c) 2007-2010 Realtek Corporation.
 * Normal data uses firmware rate adaptation. Only EAPOL, ARP and DHCP use
 * the compatibility basic rate; forcing it on every frame defeats adaptation.
 */
#define WIFI_TX_DESC_SIZE 32
#define WIFI_TX_OWN BIT(31)
#define WIFI_TX_FIRST BIT(27)
#define WIFI_TX_LAST BIT(26)
#define WIFI_TX_NON_QOS BIT(16)
#define WIFI_TX_DRIVER_RATE BIT(31)
#define WIFI_TX_BASIC_RATE 0x001f8000

static bool wifi_tx_basic_rate(const u8 *ethernet, unsigned int length)
{
	unsigned int header;
	u16 protocol;

	if (length < ETH_HLEN)
		return false;
	protocol = get_unaligned_be16(ethernet + 12);
	if (protocol == ETH_P_PAE || protocol == ETH_P_ARP)
		return true;
	/* Match the factory DHCP exception without trusting packet lengths. */
	if (protocol != ETH_P_IP || length < ETH_HLEN + 20 ||
	    ethernet[ETH_HLEN] >> 4 != 4)
		return false;
	header = (ethernet[ETH_HLEN] & 15) * 4;
	if (header < 20 || length < ETH_HLEN + header + 8 ||
	    ethernet[ETH_HLEN + 9] != IPPROTO_UDP ||
	    (get_unaligned_be16(ethernet + ETH_HLEN + 6) & 0x3fff))
		return false;
	ethernet += ETH_HLEN + header;
	return (get_unaligned_be16(ethernet) == 68 &&
		get_unaligned_be16(ethernet + 2) == 67) ||
	       (get_unaligned_be16(ethernet) == 67 &&
		get_unaligned_be16(ethernet + 2) == 68);
}

static void wifi_tx_descriptor(u8 *descriptor, unsigned int length, bool basic,
			       bool qos, u8 tid, u16 sequence)
{
	memset(descriptor, 0, WIFI_TX_DESC_SIZE);
	put_unaligned_le32(WIFI_TX_OWN | WIFI_TX_FIRST | WIFI_TX_LAST |
			   (WIFI_TX_DESC_SIZE << 16) | length, descriptor);
	/* Firmware JoinBss uses station MAC ID 5; hardware crypto is disabled. */
	put_unaligned_le32(5 | (qos ? (tid & 7) << 8 : WIFI_TX_NON_QOS), descriptor + 4);
	/* In SDIO H2C operation mode 1, firmware copies this sequence into
	 * QoS headers. Unlike the USB firmware path, it does not interpret
	 * the field as a TID and allocate a per-TID sequence on our behalf.
	 * Queue selection above remains independent of the packet sequence.
	 */
	put_unaligned_le32((sequence & 0xfff) << 16, descriptor + 12);
	if (basic) {
		put_unaligned_le32(WIFI_TX_DRIVER_RATE, descriptor + 16);
		put_unaligned_le32(WIFI_TX_BASIC_RATE, descriptor + 20);
	}
}

/* SDIO FIFO offset and matching per-AC free-page counter. */
static unsigned int wifi_tx_fifo(u8 tid)
{
	static const unsigned int fifo[] = { 0x18dc0, 0x18e00, 0x18e00, 0x18dc0,
					     0x18d80, 0x18d80, 0x18d40, 0x18d40 };

	return fifo[tid & 7];
}

static unsigned int wifi_tx_free_pages(u8 tid)
{
	static const u8 page[] = { 7, 8, 8, 7, 6, 6, 5, 5 };

	return page[tid & 7];
}
