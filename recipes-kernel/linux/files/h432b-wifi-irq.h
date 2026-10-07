/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Explicit RTL8712 SDIO active-state acknowledgement test.
 * Register definitions and request bits: Realtek GPL-2.0 SDIO headers and
 * pwrctrl/rtl871x_pwrctrl.c, vendor source commit
 * 2237e98dacd8421b38beb2d1aad88aa2b9f79dd8. Factory SDIO callback confirms
 * HISR bit 7 dispatches a read of HCPWM; no HISR write-to-clear is used here.
 */
#include <linux/completion.h>

#define WIFI_HIMR 0x09
#define WIFI_HISR 0x10
#define WIFI_HRPWM 0x80
#define WIFI_HCPWM 0x81
#define WIFI_CPWM_IRQ BIT(7)

struct h432b_ack_result {
	struct completion done;
	bool attempted;
	bool native;
	int error;
	int cleanup;
	int irq_error;
	unsigned int callbacks;
	u16 saved_mask;
	u16 armed_mask;
	u16 status;
	u16 final_status;
	u8 old_request;
	u8 request;
	u8 before;
	u8 reply;
	u8 final_reply;
};

/* Called with the MMC host held, never with the sysfs mutex. */
static void wifi_ack_irq(struct sdio_func *func, struct h432b_ack_result *r)
{
	int error = 0, mask_error;

	r->callbacks++;
	/* One-shot: mask before inspecting the source, including on errors. */
	sdio_writew(func, 0, WIFI_HIMR, &mask_error);
	r->status = sdio_readw(func, WIFI_HISR, &error);
	if (!error)
		r->reply = wifi_sdio_readb(func, WIFI_HCPWM, &error);
	if (!r->irq_error)
		r->irq_error = mask_error ? mask_error : error;
	complete(&r->done);
}

static int wifi_ack_test(struct sdio_func *func, struct h432b_ack_result *r,
			 sdio_irq_handler_t *handler)
{
	bool enable_attempted = false, mask_saved = false, irq_attempted = false;
	unsigned long completed = 0;
	u8 ioex, ien;
	int error = 0, restore;

	init_completion(&r->done);
	r->native = !!(func->card->host->caps & MMC_CAP_SDIO_IRQ);
	sdio_claim_host(func);
	/* Do not take over another handler or an already enabled function IRQ. */
	ien = sdio_f0_readb(func, SDIO_CCCR_IENx, &error);
	if (error)
		goto out;
	if (func->irq_handler || (ien & BIT(func->num))) {
		error = -EBUSY;
		goto out;
	}
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;
	if (!(ioex & BIT(func->num))) {
		enable_attempted = true;
		error = sdio_enable_func(func);
		if (error)
			goto out;
	}
	r->saved_mask = sdio_readw(func, WIFI_HIMR, &error);
	if (error)
		goto out;
	mask_saved = true;
	sdio_writew(func, 0, WIFI_HIMR, &error);
	if (error)
		goto out;
	r->old_request = wifi_sdio_readb(func, WIFI_HRPWM, &error);
	if (error)
		goto out;
	/* Drain any old acknowledgement before requesting a new toggle. */
	r->before = wifi_sdio_readb(func, WIFI_HCPWM, &error);
	if (error)
		goto out;
	r->request = ((r->old_request ^ BIT(7)) & BIT(7)) | BIT(6) | 0x0c;
	irq_attempted = true;
	error = sdio_claim_irq(func, handler);
	if (error)
		goto out;
	/* Only CPWM, not unimplemented receive or command-event queues. */
	sdio_writew(func, WIFI_CPWM_IRQ, WIFI_HIMR, &error);
	if (error)
		goto out;
	r->armed_mask = sdio_readw(func, WIFI_HIMR, &error);
	if (error)
		goto out;
	if (r->armed_mask != WIFI_CPWM_IRQ) {
		error = -EIO;
		goto out;
	}
	wifi_sdio_writeb(func, r->request, WIFI_HRPWM, &error);
	if (error)
		goto out;
	/* The IRQ thread needs the host: never wait while holding it. */
	sdio_release_host(func);
	completed = wait_for_completion_timeout(&r->done, msecs_to_jiffies(2000));
	sdio_claim_host(func);
	if (!completed)
		error = -ETIMEDOUT;
	else if (r->irq_error)
		error = r->irq_error;
	else if (!(r->status & WIFI_CPWM_IRQ) ||
		 !((r->reply ^ r->before) & BIT(7)) || (r->reply & 0x0f) != 0x0c)
		error = -EPROTO;
out:
	if (mask_saved) {
		sdio_writew(func, 0, WIFI_HIMR, &restore);
		r->cleanup = restore;
	}
	/* Also unwinds a partially successful claim; clears the callback first. */
	if (irq_attempted) {
		restore = sdio_release_irq(func);
		if (!r->cleanup)
			r->cleanup = restore;
	}
	if (mask_saved) {
		r->final_status = sdio_readw(func, WIFI_HISR, &restore);
		if (!r->cleanup)
			r->cleanup = restore;
		r->final_reply = wifi_sdio_readb(func, WIFI_HCPWM, &restore);
		if (!r->cleanup)
			r->cleanup = restore;
		sdio_writew(func, r->saved_mask, WIFI_HIMR, &restore);
		if (!r->cleanup)
			r->cleanup = restore;
	}
	if (enable_attempted) {
		restore = sdio_disable_func(func);
		if (!r->cleanup)
			r->cleanup = restore;
	}
	sdio_release_host(func);
	/* Requested active state is not undone; no suspend request is issued. */
	return error;
}
