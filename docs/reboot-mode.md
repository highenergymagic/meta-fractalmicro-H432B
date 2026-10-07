# Reboot and maintenance mode

The runtime kernel and maintenance bootloader share a one-shot boot-mode
protocol using S5PV210 INFORM7 at 0xe010f01c. INFORM0 is reserved for the
upstream suspend path and is not used for this protocol.

## Protocol

| Value | Meaning |
| --- | --- |
| 0x48344e4d | Normal boot |
| 0x48344642 | One-shot fastboot |
| Other | Ignore without writing |

The loader clears known requests and verifies the clear before selecting a
boot path. A failed clear stays in USB maintenance. A fastboot request skips
NAND initialization and enters the maintenance loop. It does not enable
fastboot flash or erase.

Linux uses the upstream syscon-reboot-mode driver. The intended interface is
`systemctl reboot --reboot-argument=bootloader` (or `fastboot`), not an
arbitrary userspace register writer. The nonzero normal value matters because
the pinned Linux notifier does not invoke its writer for zero magic.

The shared definitions live in `linux-h432b-platform.inc` and
`u-boot-h432b-boot-mode.inc`. There is no separate legacy reboot-test kernel
or loader target.

## Current boot chain

`u-boot-h432b-maintenance` loads the existing `kernel_b` volume, falling
back to the USB maintenance interface on failure.
`u-boot-h432b-maintenance-chain` packages that stage in the low-address CE
carrier. It requires a verified kernel-B image and matching systembase-B
volume; it does not provision either.

This is fixed-slot development policy, not A/B rollback. The factory
first-stage loader and EBOOT remain in place. Normal autoboot does not offer
a timed fastboot window: USB enumeration alone is not command readiness.

## Validation and limits

Hardware tests demonstrated a Linux bootloader reboot argument surviving
the factory boot chain, and a separately staged consumer clearing it and
entering fastboot. The maintenance carrier subsequently booted the installed
Linux system after plain Reset. These are distinct tests, not evidence for
every reset or power-loss scenario.

A retention-test marker survived a software reboot, while an earlier physical
Reset cleared it. Retention across power removal, suspend and other factory
firmware is not guaranteed. A persistent boot-control record would require
a separate design with ECC, bad-block handling and interrupted-write recovery.

The optional `h432b-reboot-probe` package contains a read-only register
inventory and an explicit retention tester. The tester requires an empty
INFORM7 before setting its marker, and clears only its own marker. It does
not reboot or access NAND. It is not installed by default.

Consumer tests cover known and unknown requests, one-shot consumption and
clear failure. Build and unit-test success are not hardware qualification.
Historical experimental artifacts and their source revisions remain in Git
history; they are not additional supported installation targets.

See [boot contracts](boot-contract.md) for carrier packaging and
[Ethernet support](ethernet.md) for the factory identity handoff.
