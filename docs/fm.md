# FM receiver

## Availability

The normal runtime exposes the internal Si4702-C19 as V4L2 `/dev/radio0`.
Tuning and muted signal scanning are verified, with peaks corroborated against
local broadcasts. Audible FM output and stereo reception remain unverified.
The Si4702 has no RDS decoder; station names and programme text are unavailable.

## Hardware configuration

| Resource | Configuration |
| --- | --- |
| Control bus | GPIO I2C, seven-bit address 0x10 |
| SDA / SCL | GPD1[0] / GPD1[1], separate from the PMIC bus |
| Enable | GPE0[4], active high |
| Reset | GPH3[2], active low |
| Additional control | GPH1[4] held high; tuner-side role not independently established |

The BSP extends the upstream Si470x I2C driver with Si4702-C19 part support,
crystal startup and bounded tune-completion polling. It contains no H432B callbacks or pin
numbers. The device tree supplies the regulator, reset GPIO, crystal selection,
100 kHz spacing and 50 microsecond de-emphasis. A separate GPIO hog holds the
additional board control high. No interrupt pin is assumed. The default band
is 87.5–108 MHz; V4L2 also exposes the part's 76–108 and 76–90 MHz bands.
Startup defaults to muted audio and preserves reserved values.
Muted reopening and tuning after deep sleep have passed. The polled hardware
seek implementation, reception across sleep and a tuner handle held open
across suspend require qualification on the combined image.
The driver serializes PM with control accesses, leaves a stopped
tuner stopped on resume, and restores the configured channel and audio
controls when resuming an active tuner. Shutdown asserts reset and removes
the board enable rail. Unbinding blocks further device access while preserving
open-file state until its final release. Supply teardown and detach require
qualification on the combined image.

RDS is absent from the Si4702 hardware. Hardware seek supports upward/downward
direction, wrap-around or bounded operation, and 50/100/200 kHz spacing.
Completion is polled every 20 ms with separate tune and seek timeouts.
The driver clears the operation flag on every exit and restores the original
frequency if no station is found or the operation fails, as required by the
[V4L2 seek interface](https://docs.kernel.org/userspace-api/media/v4l/vidioc-s-hw-freq-seek.html).
These capabilities follow the component protocol, not board GPIO wiring.
See the [Si4702/03-C19 data sheet](https://www.sparkfun.com/datasheets/BreakoutBoards/Si4702-03-C19-1.pdf)
for the part's register and frequency-band definitions.

## Audio and antenna

Audio is analogue into the WM8983 codec, not a PCM stream from the tuner.
It requires separate ALSA input/line-bypass routing and output enablement.
The codec path has been powered during a bounded test, but no listening
confirmation establishes end-to-end FM audio.

The factory antenna uses the headphone lead. An unterminated compatible audio
cable may provide reception, but connector wiring and cable geometry affect
performance. A signal peak alone is not decoded station identity, and hiss
does not establish reception of a broadcast.

## Diagnostic client

Build the optional target from the build repository:

```sh
python3 scripts/bsp.py fetch h432b-fm-check --without-wifi
python3 scripts/bsp.py build h432b-fm-check --without-wifi
```

The deployed `h432b-fm-check` executable is not installed or started by the
default systembase. After explicit transfer to the target:

| Invocation | Behavior |
| --- | --- |
| No arguments | Five muted tuning/readback checks |
| `--scan` | 206 muted points across the band, 200 ms settling after each tune |
| `--listen` | Checks, then ten seconds of unmuted tuner output at 87.5 MHz |

Listening does not configure codec routing. The client remutes before closing,
including handled termination. Scan output reports frequency, V4L2 signal,
stereo, mute and AFC-rail state. Signal is the driver's scaled 0–65535 value,
not a percentage or calibrated field-strength measurement.

See [FM validation](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md#fm)
for the tested artifact, scan results and qualification limits.
