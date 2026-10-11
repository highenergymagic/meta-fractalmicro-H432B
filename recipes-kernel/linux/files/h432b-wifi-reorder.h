/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bounded 12-bit RX sequence windows. Callers serialize each window and
 * authenticate frames before insertion; delivery commits CCMP replay state.
 * No timer, allocation, key ownership or packet parsing is hidden here.
 */
#define WIFI_REORDER_SIZE 64
#define WIFI_REORDER_MASK 0xfff
#define WIFI_REORDER_HALF 0x800
#define WIFI_REORDER_TIMEOUT_MS 30

struct wifi_reorder {
	void *frames[WIFI_REORDER_SIZE];
	u16 head;
	u8 pending;
	bool started;
	bool enabled;
};

typedef void (*wifi_reorder_release_t)(void *context, void *frame);

static unsigned int wifi_reorder_distance(u16 seq, u16 head)
{
	return (seq - head) & WIFI_REORDER_MASK;
}

static void wifi_reorder_advance(struct wifi_reorder *window,
				 wifi_reorder_release_t release, void *context)
{
	void **entry = &window->frames[window->head % WIFI_REORDER_SIZE];
	void *frame = *entry;

	*entry = NULL;
	window->head = (window->head + 1) & WIFI_REORDER_MASK;
	if (frame) {
		window->pending--;
		release(context, frame);
	}
}

static void wifi_reorder_ready(struct wifi_reorder *window,
			       wifi_reorder_release_t release, void *context)
{
	while (window->frames[window->head % WIFI_REORDER_SIZE])
		wifi_reorder_advance(window, release, context);
}

/* False leaves ownership with the caller. True consumes the frame, either
 * into the bounded window or through the delivery callback.
 */
static bool wifi_reorder_insert(struct wifi_reorder *window, u16 seq, void *frame,
				wifi_reorder_release_t release, void *context)
{
	unsigned int distance;

	seq &= WIFI_REORDER_MASK;
	if (!window->started) {
		window->head = seq;
		window->started = true;
	}
	distance = wifi_reorder_distance(seq, window->head);
	if (distance >= WIFI_REORDER_HALF)
		return false;
	while (distance >= WIFI_REORDER_SIZE) {
		wifi_reorder_advance(window, release, context);
		distance--;
	}
	if (window->frames[seq % WIFI_REORDER_SIZE])
		return false;
	window->frames[seq % WIFI_REORDER_SIZE] = frame;
	window->pending++;
	wifi_reorder_ready(window, release, context);
	return true;
}

/* A missing MPDU cannot hold subsequent traffic indefinitely. Skip the
 * current gap and release the next contiguous run; later gaps keep a timer.
 */
static void wifi_reorder_expire(struct wifi_reorder *window,
				wifi_reorder_release_t release, void *context)
{
	while (window->pending && !window->frames[window->head % WIFI_REORDER_SIZE])
		wifi_reorder_advance(window, release, context);
	wifi_reorder_ready(window, release, context);
}

static void wifi_reorder_clear(struct wifi_reorder *window,
			       wifi_reorder_release_t discard, void *context)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(window->frames); i++) {
		if (window->frames[i])
			discard(context, window->frames[i]);
	}
	memset(window, 0, sizeof(*window));
}
