/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
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
	return 0;
}
