# Battery interface

## Availability

Battery telemetry is available only in the opt-in battery-test profile, not
the normal NAND runtime. It exposes read-only Linux power_supply measurements;
charger control, full suspend and low-battery policy are not implemented.

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
when USB power is removed. The primary input has not been qualified with an
AC adapter. Stock polling uses a five-second interval and filters
capacity samples; communication failure must not be assumed to mean no pack.

## Read-only qualification image

The separate linux-h432b-battery-test recipe and OS layer's
openh432-battery-test bundle are opt-in experiments. They do not change the
normal board image, enable suspend or alter charging settings. The bundle
uses the installed slot-B NAND root through its minimal early handoff.

The battery-inventory platform device exposes a root-readable snapshot
attribute. Reading it initiates two CRC-checked ROM reads, then, for supported
family codes, two capacity reads with agreement and range checks.
Transactions use the identified ROM, not broadcast register access.
A family code is not necessarily an exact model identifier.

The diagnostic has no EEPROM programming, charger control, generic register
write interface. Pack serial numbers are not exposed.
It reports GPIO levels and transport errors even when battery readings fail.

## Raw measurement snapshot

The opt-in diagnostic also provides
`/sys/devices/platform/battery-inventory/registers`, readable only by root.
Each explicit read verifies the single-drop ROM twice and accepts family
0x32 before selecting that device. It reads two passes of fixed windows
0x01–0x1b (status/capacity/measurements) and 0x60–0x7c (parameters).
It does not export the unique ROM, read the user-identity area, or accept
an arbitrary address from userspace.

The parameter bytes are the current EEPROM **shadow RAM**: no Recall, Copy,
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

The opt-in driver registers `/sys/class/power_supply/h432b-battery` with type
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

The driver has no writable power_supply properties. Device removal cancels
polling before unregistering the supply and releasing GPIOs.
Charging transitions, the AC source, low-battery policy and suspend remain
separate work. The normal image and power-button policy
are unchanged.

The shared status/range policy is compiled and tested inside the pinned build
container. It covers all valid capacities and GPIO combinations, error and
stale-data handling, negative GPIO errors and out-of-range capacity. Static
tests separately check the read-only property list, polling, cleanup and
default-image isolation.

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

Compiled tests cover status/GPIO combinations, stale/error handling and signed
conversion boundaries. Active charging transitions, the AC input, exact gauge
model, long-term pack health and NAND-runtime integration remain unqualified.

## References

- [DS2780 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS2780.pdf)
- [DS2784 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds2784.pdf)
- [DS2788 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds2788.pdf)
- [DS2781 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS2781.pdf)

These describe compatible command/register layouts, not proof that a
particular model is fitted. No stock firmware or decompiled source is shipped.
