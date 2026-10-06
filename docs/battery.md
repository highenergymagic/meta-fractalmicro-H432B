# Battery interface

## Recovered hardware interface

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
The secondary input must not yet be described as USB power. Source transitions
need controlled tests. Stock polling uses a five-second interval and filters
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
write interface or automatic polling. Pack serial numbers are not exposed.
It reports GPIO levels and transport errors even when battery readings fail.

This is a transport qualification tool, not a production power_supply driver.
Voltage/current scaling, charge-source semantics, low-battery policy and
device-specific power_supply integration remain unqualified. The default
power-button policy remains unchanged.

## Validation

The opt-in image built with the pinned OE toolchain in Docker. The hardware
and OS layer suites passed 105 tests, including six static scope checks.
A RAM boot using the installed slot-B root succeeded. Two snapshots five
seconds apart returned matching CRC-valid ROMs, family code0x32 and capacity
100 percent. With USB as the sole connected source, the secondary input was
asserted and the primary input deasserted. This supports, but does not yet
fully qualify, the secondary-input USB interpretation. The kernel remained
untainted and UBI read-only.

This qualifies communication and capacity reads on one device. Family0x32
does not distinguish DS2780, DS2784 and DS2788. Voltage/current scaling and
controlled source transitions are still unverified. No charger or calibration
settings were changed, and the image was not written to NAND.

## References

- [DS2780 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS2780.pdf)
- [DS2781 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS2781.pdf)

These describe compatible command/register layouts, not proof that a
particular model is fitted. No stock firmware or decompiled source is shipped.
