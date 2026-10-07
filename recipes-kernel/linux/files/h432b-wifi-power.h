/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712 SDIO power sequencing for H432B.
 * Register offsets use the WLAN function window (0x8000 | offset).
 * Register names: Realtek rtl8712_syscfg_regdef.h.
 * Sequence reconstructed from the board's factory SDIO initialization.
 * This is not the USB power sequence and does not download firmware.
 */
#include <linux/delay.h>

/*
 * Factory function-register access uses incrementing byte-mode CMD53,
 * including single bytes. CCCR/FBR still use the standard function-0 API.
 * Host held; func->tmpbuf is the core's DMA-safe small-transfer buffer.
 */
static u8 wifi_sdio_readb(struct sdio_func *func, unsigned int address, int *error)
{
	int ret = sdio_memcpy_fromio(func, func->tmpbuf, address, 1);

	if (error)
		*error = ret;
	return ret ? 0xff : func->tmpbuf[0];
}

static void wifi_sdio_writeb(struct sdio_func *func, u8 value,
			     unsigned int address, int *error)
{
	int ret;

	func->tmpbuf[0] = value;
	ret = sdio_memcpy_toio(func, address, func->tmpbuf, 1);
	if (error)
		*error = ret;
}

struct h432b_power_result {
	bool attempted;
	bool warm;
	int error;
	int cleanup;
	unsigned int step;
	u16 before[3]; /* PLL, crystal, clock: factory warm-path signature */
	u16 after[3];
	u16 command;
	u16 verify;
};

/* Return immediately after the first failing transfer: no best-effort writes. */
static u32 wifi_read(struct sdio_func *func, unsigned int width,
		     unsigned int offset, int *error)
{
	if (width == 1)
		return wifi_sdio_readb(func, 0x8000 | offset, error);
	if (width == 2)
		return sdio_readw(func, 0x8000 | offset, error);
	return sdio_readl(func, 0x8000 | offset, error);
}

static void wifi_write(struct sdio_func *func, unsigned int width,
		       unsigned int offset, u32 value, int *error)
{
	if (width == 1)
		wifi_sdio_writeb(func, value, 0x8000 | offset, error);
	else if (width == 2)
		sdio_writew(func, value, 0x8000 | offset, error);
	else
		sdio_writel(func, value, 0x8000 | offset, error);
}

struct wifi_power_op {
	u16 offset;
	u8 width; /* zero denotes a delay in microseconds */
	u32 keep;
	u32 set;
};

#define P8(a, k, v) { (a), 1, (k), (v) }
#define P16(a, k, v) { (a), 2, (k), (v) }
#define P32(a, k, v) { (a), 4, (k), (v) }
#define WAIT_US(n) { 0, 0, 0, (n) }

static int wifi_power_ops(struct sdio_func *func, struct h432b_power_result *r,
			  const struct wifi_power_op *ops, unsigned int count)
{
	unsigned int i;
	int error = 0;
	u32 value;

	for (i = 0; i < count; i++) {
		r->step++;
		if (!ops[i].width) {
			usleep_range(ops[i].set, ops[i].set + 100);
			continue;
		}
		value = 0;
		if (ops[i].keep) {
			value = wifi_read(func, ops[i].width, ops[i].offset, &error);
			if (error)
				return error;
		}
		value = (value & ops[i].keep) | ops[i].set;
		wifi_write(func, ops[i].width, ops[i].offset, value, &error);
		if (error)
			return error;
	}
	return 0;
}

static int wifi_signature(struct sdio_func *func, u16 *values)
{
	static const unsigned int offsets[] = { 0x28, 0x26, 0x08 };
	int error = 0, i;

	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		values[i] = wifi_read(func, 2, offsets[i], &error);
		if (error)
			return error;
	}
	return 0;
}

static int wifi_power_sequence(struct sdio_func *func,
			       struct h432b_power_result *r)
{
	static const struct wifi_power_op cold_head[] = {
		/* EFUSE_TEST power/leakage control bit, not an efuse command. */
		P32(0x34, 0xffffffff, 0x00040000),
		P16(0x26, 0xffff, 0x0081), /* crystal */
		P8(0x10, 0xff, 0x01),     /* analog bandgap */
		P16(0x11, 0xffff, 0x1001),
	};
	static const struct wifi_power_op cold_tail[] = {
		WAIT_US(1500),
		P8(0x10, 0xff, 0x02),
		P8(0x11, 0xff, 0x02),
		WAIT_US(500),
		P8(0x21, 0xff, 0x01),
		P8(0x20, 0xff, 0x01),
		WAIT_US(1000),
		P16(0x00, 0xffff, 0x0008),
		P16(0x02, 0xffff, 0x2000),
		P16(0x00, 0x77ff, 0),
		P16(0x26, 0xfbff, 0),
		P8(0x10, 0xff, 0x08),
		WAIT_US(100),
		P32(0x28, 0xffffffff, 0x01),
		WAIT_US(500),
		P32(0x28, 0xffffffff, 0x110),
		P16(0x00, 0xffee, 0),
		P16(0x08, 0xfffb, 0),
		P16(0x08, 0xffff, 0x1800),
		P16(0x08, 0xfff9, 0),
		P16(0x02, 0xffff, 0x8800),
		P16(0x00, 0xf9ff, 0),
	};
	static const struct wifi_power_op warm_tail[] = {
		P8(0x03, 0x73, 0),
		P8(0x1f, 0, 0), /* RF control disabled during digital reset */
		P8(0x03, 0xff, 0x88),
		P16(0x08, 0, 0xb8a0),
	};
	static const struct wifi_power_op common[] = {
		P8(0x09, 0, 0xb8),
		P16(0x42, 0, 0x0fff),
		P16(0x50, 0, 0x5678),
		P16(0x40, 0, 0x3fff),
	};
	int error = 0, i;
	u32 value;

	error = wifi_signature(func, r->before);
	if (error)
		return error;
	r->warm = r->before[0] == 0x6911 &&
		  r->before[1] == 0xdb8f && r->before[2] == 0xb8a4;
	if (r->warm) {
		r->step++;
		wifi_write(func, 1, 0x09, 0x38, &error);
		if (error)
			return error;
		/* Factory polls 1001 times; return an error instead of ignoring timeout. */
		for (i = 0; i < 1001; i++) {
			value = wifi_read(func, 1, 0x09, &error);
			if (error)
				return error;
			if (value & 0x40)
				break;
			usleep_range(100, 200);
		}
		if (i == 1001)
			return -ETIMEDOUT;
		error = wifi_power_ops(func, r, warm_tail, ARRAY_SIZE(warm_tail));
	} else {
		error = wifi_power_ops(func, r, cold_head, ARRAY_SIZE(cold_head));
		if (error)
			return error;
		r->step++;
		value = wifi_read(func, 2, 0x11, &error);
		if (error)
			return error;
		if ((value & 0x0ff0) != 0x0490) {
			wifi_write(func, 2, 0x11, 0x5497, &error);
			if (error)
				return error;
		}
		error = wifi_power_ops(func, r, cold_tail, ARRAY_SIZE(cold_tail));
	}
	if (error)
		return error;
	error = wifi_power_ops(func, r, common, ARRAY_SIZE(common));
	if (error)
		return error;
	r->step++;
	/* Local-window HRPWM; do not translate it into the WLAN window. */
	wifi_sdio_writeb(func, 0, 0x80, &error);
	if (error)
		return error;
	error = wifi_signature(func, r->after);
	if (error)
		return error;
	r->command = wifi_read(func, 2, 0x40, &error);
	if (error)
		return error;
	r->verify = wifi_read(func, 2, 0x50, &error);
	if (error)
		return error;
	/* Prove MAC I/O responds after switching the control path. */
	if (r->command != 0x3fff || r->verify != 0x5678)
		return -EIO;
	return 0;
}
#undef P8
#undef P16
#undef P32
#undef WAIT_US
