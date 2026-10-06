/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef H432B_BATTERY_POLICY_H
#define H432B_BATTERY_POLICY_H

enum h432b_charge_state {
	H432B_UNKNOWN,
	H432B_CHARGING,
	H432B_DISCHARGING,
	H432B_NOT_CHARGING,
};

static inline int h432b_capacity_usable(int error, int capacity, int fresh)
{
	return !error && fresh && capacity >= 0 && capacity <= 100;
}

static inline enum h432b_charge_state
h432b_charge_state(int error, int capacity, int fresh,
		  int charging, int primary, int secondary)
{
	if (!h432b_capacity_usable(error, capacity, fresh) ||
	    charging < 0 || primary < 0 || secondary < 0)
		return H432B_UNKNOWN;
	if (charging)
		return (primary || secondary) ? H432B_CHARGING : H432B_UNKNOWN;
	/* 100% plus an inactive charging pin does not prove charge termination. */
	return (primary || secondary) ? H432B_NOT_CHARGING : H432B_DISCHARGING;
}
#endif
