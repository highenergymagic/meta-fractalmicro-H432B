/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#define current kernel_current_macro_must_not_be_used()
#include "h432b-battery-policy.h"

int main(void)
{
	int cap, charging, primary, secondary, checks = 0;

	for (cap = 0; cap <= 100; cap++) {
		assert(h432b_capacity_usable(0, cap, 1));
		assert(!h432b_capacity_usable(-5, cap, 1));
		assert(!h432b_capacity_usable(0, cap, 0));
		for (charging = 0; charging < 2; charging++)
			for (primary = 0; primary < 2; primary++)
				for (secondary = 0; secondary < 2; secondary++) {
					enum h432b_charge_state want;
					if (charging)
						want = (primary || secondary) ?
							H432B_CHARGING : H432B_UNKNOWN;
					else
						want = (primary || secondary) ?
							H432B_NOT_CHARGING : H432B_DISCHARGING;
					assert(h432b_charge_state(0, cap, 1, charging,
						primary, secondary) == want);
					assert(h432b_charge_state(-5, cap, 1, charging,
						primary, secondary) == H432B_UNKNOWN);
					assert(h432b_charge_state(0, cap, 0, charging,
						primary, secondary) == H432B_UNKNOWN);
					checks += 3;
				}
	}
	assert(!h432b_capacity_usable(0, -1, 1));
	assert(!h432b_capacity_usable(0, 101, 1));
	assert(h432b_charge_state(0, 100, 1, 0, 0, 1) == H432B_NOT_CHARGING);
	assert(h432b_charge_state(0, 50, 1, -5, 0, 1) == H432B_UNKNOWN);
	assert(h432b_charge_state(0, 50, 1, 0, -5, 1) == H432B_UNKNOWN);
	assert(h432b_charge_state(0, 50, 1, 0, 0, -5) == H432B_UNKNOWN);
	printf("battery policy: %d status matrix checks plus range/error checks passed\n", checks);

	{
		struct h432b_measurements m;
		int raw;
		assert(h432b_decode_measurements(0x6b00, 0x1ae0, 4, -112, 50, &m));
		assert(m.voltage_uv == 4177280 && m.temp_decic == 268);
		assert(m.current_ua == 312 && m.current_avg_ua == -8750);
		assert(h432b_decode_measurements(0x6000, -2560, -6400, 6400, 50, &m));
		assert(m.temp_decic == -100 && m.current_ua == -500000);
		assert(m.current_avg_ua == 500000);
		assert(!h432b_decode_measurements(0, 0, 0, 0, 50, &m));
		assert(!h432b_decode_measurements(-32, 0, 0, 0, 50, &m));
		assert(!h432b_decode_measurements(0x6000, 0, 0, 0, 0, &m));
		assert(!h432b_decode_measurements(0x6000, 0, 0, 0, 256, &m));
		assert(!h432b_decode_measurements(0x6000, -10241, 0, 0, 50, &m));
		assert(!h432b_decode_measurements(0x6000, 21761, 0, 0, 50, &m));
		assert(!h432b_decode_measurements(0x6000, 0, 32767, 0, 50, &m));
		assert(!h432b_decode_measurements(0x6000, 0, 0, -32768, 50, &m));
		for (raw = -32767; raw < 32767; raw++) {
			assert(h432b_decode_measurements(0x6000, 0, raw, raw, 255, &m));
			assert(m.current_ua == (int)((long long)raw * 25 * 255 / 16));
		}
		puts("battery measurements: conversion, sign, bounds and 65534 current cases passed");
	}
	return 0;
}
