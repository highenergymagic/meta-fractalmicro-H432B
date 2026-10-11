# Battery interface

## Availability

Battery telemetry is included in the normal NAND runtime and shared with the
opt-in battery-test profile. It exposes read-only Linux power_supply measurements;
charger control and low-battery policy are not implemented. System sleep policy
is described in [power management](power-control.md).

## Hardware interface

The stock battery driver uses a separate standard-speed 1-Wire-style
fuel-gauge connection, not the PMIC I2C bus or a voltage-only ADC estimator.
Its capacity transaction matches the DS278x Read Data command and relative
capacity register. The exact device model is not yet verified on hardware.

| GPIO | Function inferred from stock driver |
| --- | --- |
| GPC0[0] | Active-low charging indication |
| GPC0[1] | Active-low secondary external-power indication |
| GPC0[2] | Active-low primary external-power indication |
| GPC0[3] | 1-Wire receive |
| GPC0[4] | Inverted transmit: high pulls the bus low; low releases it |

The electrical implementation of the inverting stage is not established.
A controlled USB unplug test confirmed that the secondary input deasserts
when USB power is removed. Charging has been observed with AC and USB connected together, but the primary
input has not been isolated through an AC-only transition test. Stock polling uses a five-second interval and filters
capacity samples; communication failure must not be assumed to mean no pack.

## Driver and diagnostic profile

The linux-h432b-runtime and linux-h432b-battery-test recipes consume the same
h432b-battery.inc driver and configuration. Telemetry does not alter charging
settings; the runtime's separate power-management integration enables suspend. The optional openh432-battery-test bundle isolates
telemetry testing and uses the installed slot-B root through its minimal
handoff.

Only the diagnostic profile enables `CONFIG_H432B_BATTERY_DEBUG`. It exposes
root-readable `snapshot` and `registers` files in debugfs under
`battery-inventory/`; these are not stable sysfs interfaces and are absent from
the normal NAND kernel. Reading `snapshot` initiates two CRC-checked ROM reads, then, for supported
family codes, two capacity reads with agreement and range checks.
Transactions use the identified ROM, not broadcast register access.
A family code is not necessarily an exact model identifier.

The driver has no EEPROM programming, charger control, generic register
write interface. Pack serial numbers are not exposed.
It reports GPIO levels and transport errors even when battery readings fail.

## Raw measurement snapshot

The driver also provides
`/sys/kernel/debug/battery-inventory/registers` in the diagnostic profile,
readable only by root after mounting debugfs.
Each explicit read verifies the single-drop ROM twice and accepts family
0x32 before selecting that device. It reads two passes of fixed windows
0x01–0x1b (status/capacity/measurements) and 0x60–0x7c (parameters).
It does not export the unique ROM, read the user-identity area, or accept
an arbitrary address from userspace.

The parameter bytes are the current EEPROM shadow RAM: no Recall, Copy,
Write Data or charger-control command is issued. `parameters_equal` reports
agreement between passes. Register data has no transport CRC; two passes
are evidence for comparison, not proof of error-free data. Measurements may
change between passes. Each two-byte measurement is read in one transaction.

These bytes are for model/scaling qualification, not calibrated engineering
units. Raw reads are explicit and do not expand the five-second background
poll. The standard measurements below use these qualified register formats.

For the documented DS2780/2784/2788-compatible interpretation, register
pairs are big-endian. Temperature at 0x0a is signed, shifted right five,
then multiplied by 0.125 degrees C. Voltage at 0x0c uses the same bit
alignment and approximately 4.88 mV per count. Current at 0x0e and average
current at 0x08 are signed 16-bit counts at 1.5625 microvolts across the
sense resistor per count. Parameter 0x69 stores conductance in inverse ohms:
current in microamps is therefore raw current times 1.5625 times conductance.
A zero conductance is invalid, not a zero-current measurement. Calibration
gain is already applied by the gauge and must not be multiplied in twice.

Positive current denotes charging, negative current discharging. Small
readings must be interpreted against sensor offset accuracy. These formulas
do not independently establish the fitted model, divider wiring, calibration
accuracy or pack health; retain the raw capture alongside any interpretation.

## Linux power_supply interface

The driver registers `/sys/class/power_supply/h432b-battery` with type
`Battery` and these read-only properties:

- `capacity`: verified remaining capacity, from 0 to 100 percent.
- `status`: Charging, Discharging, Not charging, or Unknown.
- `voltage_now`: gauge voltage in microvolts.
- `temp`: gauge temperature in tenths of a degree Celsius.
- `current_now`: signed instantaneous current in microamps.
- `current_avg`: signed averaged current in microamps.

Positive current means charging; negative means discharging. Measurements
are enabled only for the qualified family-0x32 register format. Each poll
checks two reads of the programmed conductance, rejects zero/mismatched
calibration, reads each register pair in one transaction and rejects invalid
or saturated values. Measurement failures return no data, not zero current.
Conversions are unit-tested with signed and boundary cases. These are gauge
readings using pack-stored calibration, not independently calibrated lab
measurements. The status policy still uses the recovered GPIO inputs.

Telemetry is sampled every five seconds and cached, so ordinary sysfs/uevent
reads do not trigger extra bus transactions. Changes generate standard
power_supply notifications. A failed sample invalidates capacity immediately;
samples older than 15 seconds are also unavailable. Status becomes Unknown.
A transport failure is not reported as an absent battery or zero percent.

Charging requires an asserted charging indication plus external-source
presence. With external power but no charging indication, status is Not
charging, even at 100 percent: the driver does not invent a charge-complete
signal. No exact chip model, health, presence, serial number or estimated runtime
is advertised.

The driver has no writable power_supply properties: it monitors the gauge and
board power indications, rather than replacing the autonomous charger.
`CONFIG_H432B_BATTERY` selects it in the power-supply subsystem. Matching is by
the battery device's compatible string, not the machine's product name.
Board wiring uses the canonical `hims,h432b-battery` compatible. The original
`fractal,h432b-battery-inventory` alias remains accepted for existing device
trees; driver and power_supply names are unchanged.

System suspend, shutdown and device removal cancel polling and release the bus.
The cache becomes unavailable at suspend; resume queues a fresh sample instead
of reporting a pre-sleep measurement. Explicit diagnostic reads return EBUSY
while the transport is stopped. Device removal stops the worker before
unregistering the supply and releasing GPIOs.
A NAND-runtime observation with AC and USB connected reported Charging and
sustained positive gauge current. This does not establish USB-only charging,
a complete charge cycle or automatic low-battery policy.
Power-button actions belong to the separate system sleep policy.

The shared status/range policy is compiled and tested inside the pinned build
container. It covers all valid capacities and GPIO combinations, error and
stale-data handling, negative GPIO errors and out-of-range capacity. Static
tests separately check the read-only property list, polling, cleanup and
shared runtime/diagnostic integration.

## Validation scope

RAM-profile tests verified CRC-valid family-0x32 identification, capacity and
all six power_supply properties. A controlled USB-removal test deasserted the
secondary-source input and changed status to Discharging, with approximately
240–360 mA discharge and a voltage change from about 4.18 to 4.15 V.
This establishes the USB-present mapping and current-sign interpretation.

Repeated fixed-window reads had matching parameter shadows and valid ROM
CRCs. Gauge readings use pack-stored calibration; they are not independently
calibrated measurements. Near-zero current must be interpreted against sensor
offset accuracy. Sticky status flags describe prior conditions and do not
alone establish present undervoltage or an observed full charge cycle.

A NAND-runtime sample with AC and USB connected reported approximately
1.24–1.33 A positive current and rising voltage from 3.484 to 3.503 V, with
charging and both external-source indications asserted. This establishes
active charging in that configuration, not the ability to charge from USB
alone or calibrated measurement accuracy.

Compiled tests cover status/GPIO combinations, stale/error handling and signed
conversion boundaries. Isolated AC transitions, the exact gauge model and
long-term pack health remain unqualified. For artifact scope see the
[validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md).

## References

- [DS2780 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS2780.pdf)
- [DS2784 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds2784.pdf)
- [DS2788 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds2788.pdf)
- [DS2781 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS2781.pdf)

These describe compatible command/register layouts, not proof that a
particular model is fitted. No stock firmware or decompiled source is shipped.
