/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_FASTBOOT_SLOTS_H
#define H432B_FASTBOOT_SLOTS_H

#include "bootstate.h"

/* Before replacing any image, persist zero attempts and verify it. The
 * caller must preserve a different bootable slot. No image write may follow
 * a failure, even if the metadata operation might have committed.
 */
static inline int
h432b_fastboot_invalidate(const struct h432b_bootstore *store,
			 unsigned char *scratch, unsigned int slot)
{
	struct h432b_bootstate state;
	int current;
	unsigned int target;

	if (slot > 1)
		return -1;
	current = h432b_bootstate_load(store, scratch, &state);
	if (current < 0 || !state.left[slot ^ 1U])
		return -1;
	if (!state.left[slot])
		return 0;
	state.left[slot] = 0;
	state.serial++;
	target = (unsigned int)current ^ 1U;
	if (h432b_env_encode(&state, scratch) ||
	    store->replace(store->context, target, scratch) ||
	    store->read(store->context, target, scratch + H432B_ENV_BYTES) ||
	    memcmp(scratch, scratch + H432B_ENV_BYTES, H432B_ENV_BYTES))
		return -2;
	return 0;
}

/* Persist a preferred slot without consuming its first boot attempt.
 * This is the storage operation, not a complete Android boot-control API:
 * format v1 has no independent successful flag. Callers must validate the
 * installed image pair before activation. Never infer success from LEFT=3.
 *
 * Return 0 only after verified persistence, -1 for invalid input/state,
 * or -2 for a storage failure (which may have committed despite the error).
 * scratch requires 2 * H432B_ENV_BYTES. No default state is manufactured.
 */
static inline int
h432b_fastboot_set_active(const struct h432b_bootstore *store,
			 unsigned char *scratch, const char *slot_name)
{
	struct h432b_bootstate state, check;
	unsigned int slot, target;
	int current;

	if (!slot_name || (slot_name[0] != 'a' && slot_name[0] != 'b') ||
	    slot_name[1])
		return -1;

	slot = slot_name[0] - 'a';
	current = h432b_bootstate_load(store, scratch, &state);
	if (current < 0)
		return -1;

	state.order[0] = slot;
	state.order[1] = slot ^ 1U;
	state.left[slot] = 3;
	state.serial++;
	target = (unsigned int)current ^ 1U;
	if (h432b_env_encode(&state, scratch) ||
	    store->replace(store->context, target, scratch) ||
	    store->read(store->context, target, scratch + H432B_ENV_BYTES) ||
	    memcmp(scratch, scratch + H432B_ENV_BYTES, H432B_ENV_BYTES) ||
	    h432b_env_decode(scratch + H432B_ENV_BYTES, H432B_ENV_BYTES, &check))
		return -2;

	return 0;
}

#endif
