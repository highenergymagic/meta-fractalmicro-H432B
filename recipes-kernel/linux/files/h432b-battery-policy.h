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

struct h432b_measurements {
	int voltage_uv, temp_decic, current_ua, current_avg_ua;
};

/* DS2780/2784/2788 register units; calibration is applied inside the gauge. */
static inline int h432b_decode_measurements(int voltage, int temp,
		int current_raw, int average, int conductance,
		struct h432b_measurements *m)
{
	if (conductance <= 0 || conductance > 255 ||
	    voltage <= 0 || voltage > 32767 ||
	    temp < -10240 || temp > 21760 ||
	    current_raw <= -32768 || current_raw >= 32767 ||
	    average <= -32768 || average >= 32767)
		return 0;
	m->voltage_uv = (voltage / 32) * 4880;
	m->temp_decic = (temp / 32) * 125 / 100;
	m->current_ua = current_raw * 25 * conductance / 16;
	m->current_avg_ua = average * 25 * conductance / 16;
	return m->voltage_uv > 0;
}
#endif
