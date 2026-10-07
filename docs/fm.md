# Internal FM receiver

## Hardware

A normal NAND boot read device ID `0x1242` and powered-down chip ID
`0x1000`, identifying a Silicon Labs Si4702 revision C. This part has no RDS.
The control interface is I2C address `0x10`, carried on GPIO-emulated I2C:
GPD1[0] is SDA and GPD1[1] is SCL. These are separate from the PMIC bus.

Board startup uses GPE0[4] power enable, active-low GPH3[2] reset and
GPH1[4] held high as in the factory initialization. The tuner-side function
of the last signal has not been independently established. Identity readback
has validated this power/reset sequence; no enclosure access is needed.

## Linux integration

The runtime includes the upstream Si470x I2C driver with board sequencing,
crystal startup and bounded tune-completion polling. No interrupt pin is
assumed. The board path defaults to muted audio, retains reserved register
values and does not expose RDS or hardware seek. It uses 100 kHz spacing and
50 microsecond de-emphasis over 87.5--108 MHz.

The standard interface is V4L2 `/dev/radio0`, not PCM audio. Audio is an
analogue path into the WM8983 codec and requires separate ALSA routing.
Codec routing, audible reception, automatic seeking and power-management
qualification remain incomplete. Hiss alone is not evidence of receiving a
station. A wired headphone lead is used as the antenna in the factory design.

The optional `h432b-fm-check` recipe builds an explicit, muted V4L2 tuning
test client. An explicit `--listen` option unmutes the tuner at 87.5 MHz
for ten seconds after the tuning checks. It remutes before closing, including
on handled interruption; it does not configure the codec or speaker. It is not started automatically or installed in the default
systembase. The `--scan` option instead scans 87.5–108 MHz in 100 kHz
steps, remains muted, waits 200 ms after tuning, and reports V4L2 signal,
stereo and AFC-rail indicators. Signal is the driver's scaled 0–65535 value,
not a percentage or a calibrated field-strength measurement. By default it selects five frequencies and requires matching readback and
mute enabled; it does not claim broadcast reception.

## Qualification

- NAND identity-only driver: device/chip IDs read successfully; kernel untainted.
- Standard V4L2 driver: normal NAND boot registered `/dev/radio0` and read
  powered-up chip ID `0x1053` (Si4702-C19).
- Two open/tune/close cycles each selected 87.5, 90.5, 99.5, 107.9 and
  87.5 MHz. All ten frequency readbacks matched, with mute remaining enabled.
  Signal readings were zero without a headphone antenna. No failed systemd
  units or kernel taint were reported.
- Kernel bundle SHA-256:
  `cce985f4bb928c739d99473d48630b61d55287ac362835882bab1da14d9f19a3`.
  Slot-B readback passed and kernel A was unchanged. The systembase and
  bootloader were not replaced. This is hardware qualification of a local-layer
  build, not a clean-build or cross-architecture reproducibility result.
- Analogue route test: ALSA powered the input PGA, boost mixer, line bypass,
  output stages and internal speaker without a PCM stream. A ten-second
  tuner-unmute test completed at reduced output gain, and the complete ALSA
  control readback afterward matched its saved baseline. Audible output was inconclusive because the listener was not in a position
  to hear the test; RF station reception remains unqualified.
- With a 3.5 mm/composite adapter used as an improvised antenna, a muted
  206-point scan completed with every tuning readback correct. Weak peaks
  appeared around 90.1 and 94.1 MHz (maximum V4L2 signal 4369/65535), but
  no stereo indication. The operator corroborated the peak frequencies against known local broadcasts.
  This supports RF station detection, not decoded station identity or audio
  qualification. The Si4702 cannot provide RDS station names.
  Mixer controls were unchanged after the scan.

Factory firmware, disassembly and device logs remain private.
