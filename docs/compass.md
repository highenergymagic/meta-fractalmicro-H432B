# Motion sensor

The H432B contains an Aichi Steel AMI603 three-axis magnetometer and
three-axis accelerometer. The runtime kernel includes a direct-mode IIO
driver. Identity, factory-parameter reads, stationary measurements and response to
rotation/tilt have been verified from a normal NAND boot. This interface does
not provide calibrated compass headings. One power-button deep-sleep/wake
cycle restored readings on all six channels.

## Interface

Locate the device whose `name` under `/sys/bus/iio/devices/iio:device*/`
is `ami603`. Read `in_magn_{x,y,z}_raw` and
`in_accel_{x,y,z}_raw` for signed sensor-package-axis samples.
Each raw read requests a new measurement; separate axis reads are not a
synchronized vector.

Magnetic scale is expressed in gauss per count. Acceleration is
`(raw + offset) * scale` in metres per second squared. Scale and
acceleration origin come from the device's read-only factory parameters.
The driver retains the sensor's power-on magnetic fine offsets; it does not
run an offset sweep. Values at -2048 or 2047 indicate the measurement limit.
The driver does not apply cross-axis correction, mounting orientation,
hard-iron or soft-iron compensation. It does not expose the pedometer.

## Board integration

The dedicated GPIO I2C bus uses GPB4 for SCL and GPB6 for SDA, with
7-bit address `0x0f`. A fixed regulator controls GPE1[2] and is connected to
the silicon driver's `vdd-supply`; the driver contains no board GPIO numbers.
Measurements use bounded status-register polling. DRDY and interrupt
pins are not configured as wake sources.

The sensor returns to standby after each read. System suspend disables
its owned supply reference; resume verifies identity before allowing measurements.
Regulator errors are propagated, and a failed disable retains its reference
state so a later cleanup can retry without an unbalanced enable. Shutdown and
unbind release the supply. The regulator-based integration requires hardware
requalification; the measurement protocol and delay requirements are unchanged.
No OTP programming is performed.

## Qualification

Verify identity, repeated stationary readings, response to rotation and
tilt, and post-suspend readings before relying on the sensor.
A working register interface does not establish heading accuracy.
