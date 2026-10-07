# External SD slot

The external slot is being qualified on the S5PV210 controller at
`0xeb200000` (`sdhci2`). Factory-driver analysis identifies GPH3[1] as
the card-detect input with internal pulls disabled. The active-low GPIO configuration has passed physical removal and
reinsertion testing without a reset.

## Diagnostic profile

`linux-h432b-external-sd-test` enables the controller with a four-bit bus,
a 25 MHz frequency ceiling and no 1.8 V signaling. It uses the standard
Samsung SDHCI driver and pin groups, with GPIO-based card detection.
No additional power rail is inferred or switched.

Both the internal and external SD block devices retain the board's
read-only block/ioctl guard. The recipe rejects writable NAND profiles.
Mechanical write-protect sensing is not yet qualified; the software guard
remains active regardless of the card's lock tab.

`openh432-external-sd-test` packages this kernel for RAM boot using the
installed read-only NAND root. Builds do not access the device or install
firmware.

## Qualification

`tests/check-external-sd.sh` resolves cards through their physical
controllers, verifies read-only flags and NAND protection, and compares
two direct 4 MiB reads at each of three locations: start, midpoint, and near
the end. It neither mounts the card nor writes to it. Matching repeated
reads establish repeatability, not comparison against an independent
known-good copy or a whole-card capacity test.

The pinned build and 109 source checks passed. The RAM image enumerated
an SDHC card on the external controller, reporting 61,071,360 sectors.
All three pairs of 4 MiB direct reads matched, with individual transfers
around 6.3–6.8 MB/s. Both SD devices and NAND remained read-only.
Removal was subsequently detected without a reset: the external block
device disappeared while the internal card remained present. Reinsertion
also enumerated automatically without a reset. All three read checks passed
again, with hashes identical to the pre-removal samples and transfers around
7.6 MB/s. This establishes boot-time detection, bounded repeatable reads,
and one removal/reinsertion cycle with the tested card. Mechanical write
protection sensing and writes remain unqualified.
No persistent firmware was changed.

## Device-only suspend/resume

With the external card inserted, two guarded `pm_test=devices` cycles
completed in approximately 5.9 seconds each, with SDIO still attached and
asynchronous device PM enabled. After each cycle, all external-card sample
hashes matched the pre-suspend values, and the USB hub and PL2303 adapter
passed enumeration/binding checks.

Suspend statistics reported two successes and zero failures; the kernel
remained untainted, no systemd services failed, NAND stayed read-only, and
`pm_test` was restored to `none`. Both SD cards remained read-only.

This tests device callbacks and recovery only. It does not exercise actual
CPU sleep, the late/noirq suspend stages, wake sources, or storage power-loss
behavior.
