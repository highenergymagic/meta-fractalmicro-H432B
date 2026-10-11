/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CCMP framing follows Linux mac80211 wpa.c / aead_api.c:
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 * Copyright 2014-2015, Qualcomm Atheros, Inc.
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * Use the kernel's synchronous CCM implementation, not the unqualified
 * firmware CAM path. PN state is serialized by the interface owner mutex.
 */
struct h432b_wifi_key {
	struct crypto_aead *tfm;
	u8 material[16];
	u64 tx_pn, rx_pn[17];
};

static u64 wifi_ccmp_pn(const u8 *iv)
{
	return (u64)iv[0] | (u64)iv[1] << 8 | (u64)iv[4] << 16 |
		(u64)iv[5] << 24 | (u64)iv[6] << 32 | (u64)iv[7] << 40;
}

static void wifi_ccmp_iv(u8 *iv, u64 pn, u8 index)
{
	iv[0] = pn; iv[1] = pn >> 8; iv[2] = 0; iv[3] = 0x20 | index << 6;
	iv[4] = pn >> 16; iv[5] = pn >> 24; iv[6] = pn >> 32; iv[7] = pn >> 40;
}

/* Only three-address, nonfragmented data is accepted by the caller.
 * Heap-backed scatterlist buffers: never map stack data through the crypto API.
 */
static int wifi_ccmp_crypt(struct h432b_wifi_key *key, u8 *frame,
			   unsigned int header, unsigned int payload, bool decrypt)
{
	struct aead_request *request;
	struct scatterlist sg[2];
	u8 *scratch, *aad, *nonce, *iv = frame + header;
	u16 fc = get_unaligned_le16(frame);
	unsigned int aad_len = header == 26 ? 24 : 22, i;
	u64 pn = wifi_ccmp_pn(iv);
	int error;

	scratch = kzalloc(48, GFP_KERNEL);
	if (!scratch)
		return -ENOMEM;
	aad = scratch;
	nonce = scratch + 32;
	/* Mask subtype bits 4..6, retry, power management and more-data. */
	fc &= ~(0x0070 | 0x0800 | 0x1000 | 0x2000);
	if (header == 26)
		fc &= ~0x8000;
	fc |= 0x4000;
	put_unaligned_le16(fc, aad);
	memcpy(aad + 2, frame + 4, 18);
	aad[20] = frame[22] & 15;
	if (header == 26)
		aad[22] = frame[24] & 15;
	nonce[0] = 1; /* CCM L=2 */
	nonce[1] = header == 26 ? frame[24] & 15 : 0;
	memcpy(nonce + 2, frame + 10, ETH_ALEN);
	for (i = 0; i < 6; i++)
		nonce[8 + i] = pn >> ((5 - i) * 8);
	request = aead_request_alloc(key->tfm, GFP_KERNEL);
	if (!request) {
		kfree_sensitive(scratch);
		return -ENOMEM;
	}
	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], aad, aad_len);
	sg_set_buf(&sg[1], iv + 8, payload + 8);
	aead_request_set_ad(request, aad_len);
	aead_request_set_crypt(request, sg, sg, payload + (decrypt ? 8 : 0), nonce);
	error = decrypt ? crypto_aead_decrypt(request) : crypto_aead_encrypt(request);
	aead_request_free(request);
	kfree_sensitive(scratch);
	return error;
}
