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

Linux uses the upstream syscon-reboot-mode driver. The userspace interface is
`systemctl reboot --reboot-argument=bootloader` (or `fastboot`), not an
arbitrary userspace register writer. The nonzero normal value matters because
the pinned Linux notifier does not invoke its writer for zero magic.

The shared definitions live in `linux-h432b-platform.inc` and
`u-boot-h432b-boot-mode.inc`. There is no separate legacy reboot-test kernel
or loader target.

## Boot chains

The normal deployment uses `u-boot-h432b-ab-chain`: a factory-compatible
carrier containing the persistent A/B selector. It consumes the one-shot
request before normal slot selection. Without a maintenance request, it
selects an eligible kernel and matching systembase using redundant UBI boot
state. Invalid or exhausted state also leads to USB maintenance.

The optional `u-boot-h432b-maintenance-chain` is the legacy fixed-B carrier.
It ignores A/B selection and requires a verified `kernel_b` with matching
`systembase_b`. Neither carrier provisions storage. See the
[boot contract](boot-contract.md) for artifact roles and update ordering.

Both retain the factory first-stage loader and EBOOT. Normal autoboot does
not offer a timed fastboot window: USB enumeration alone is not command
readiness.

## Validation and limits

Hardware tests demonstrated a Linux bootloader reboot argument surviving
the factory boot chain, and a separately staged consumer clearing it and
entering fastboot. The maintenance carrier subsequently booted the installed
Linux system after plain Reset. These are distinct tests, not evidence for
every reset or power-loss scenario.

A retention-test marker survived a software reboot but was cleared by physical
Reset. INFORM7 is a one-shot request, not persistent update state; retention
across power removal, suspend and other factory firmware is not guaranteed.
Persistent A/B attempt tracking uses separate redundant UBI records.

Ordinary software reboot with empty USB host ports, including after deep
suspend/resume, has passed with the board-specific onboard-hub runtime-PM
policy. Do not force that hub to autosuspend; see [USB host](usb-host.md).

The optional `h432b-reboot-probe` package contains a read-only register
inventory and an explicit retention tester. The tester requires an empty
INFORM7 before setting its marker, and clears only its own marker. It does
not reboot or access NAND. It is not installed by default.

Consumer tests cover known and unknown requests, one-shot consumption and
clear failure. Build and unit-test success are not hardware qualification.

See [boot contracts](boot-contract.md) for carrier packaging and
[Ethernet support](ethernet.md) for the factory identity handoff.
