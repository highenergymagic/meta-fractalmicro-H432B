# Build targets and source organization

## Normal deployment

| Target | Purpose |
| --- | --- |
| `linux-h432b-runtime` | The default Linux CIP/RT kernel; also used for standalone RAM recovery. |
| `u-boot-h432b-maintenance-chain` | Persistent CE carrier containing the fixed slot-B maintenance stage. |
| `u-boot-h432b-maintenance` | High-RAM NAND reader and one-shot fastboot stage, embedded by the carrier. |

The OS layer supplies `openh432-nand-b`, the minimal
`openh432-early-b` root handoff, and `openh432-systembase-b`.
The tiny initramfs is part of normal boot, not leftover early bring-up code.

## Installation and recovery

`u-boot-h432b` is the low-address USB-shell bootstrap needed by the initial
stock-CE conversion workflow. `u-boot-h432b-fastboot` is its high-RAM fastboot
loader. Both remain because a device without a Linux NAND pool cannot use the
normal NAND boot path.

`openh432-fastboot-ram` in the OS layer packages the runtime kernel with a
complete standalone RAM root. It is not a second kernel fork or an installer.
Its kernel permits Linux-pool and internal-SD writes; it is not a read-only
forensic acquisition environment.

## Optional diagnostics

These targets are deliberately separate from normal deployment:

| Kernel suffix / matching OS image suffix | Reason retained |
| --- | --- |
| `wifi-test` | Explicit RTL8712 CMD52/CMD53 transport qualification; not a network driver. |
| `power-test` | Read-only-storage PMIC bus inspection baseline. |
| `battery-test` | Battery telemetry driver not yet integrated into the runtime. |
| `input-test` | Key, switch and vibration qualification. |
| `resume-test` | Suspend bridge and USB power-lifecycle work. |
| `external-sd-test` | Removable SD and USB-host qualification. |
| `sd-rw-test` | Explicit removable-SD write qualification. |
| `gps-test` | GPS qualification with the extended peripheral profile. |

Kernel recipes use the prefix `linux-h432b-`; image recipes use
`openh432-`. Shared drivers need not be duplicated to keep profiles isolated.
Support tools under `recipes-support` are built explicitly, not installed
automatically in the normal root filesystem.

The optional `u-boot-h432b-nand`, `-nand-profile`, `-nand-timer` and
`-nand-subpage` targets retain NAND inspection and performance experiments.
Building one does not promote its behavior into the persistent carrier.

## Retired targets

The standalone `linux-h432b` and `linux-h432b-reboot-test` kernels,
`openh432-reboot-test` image, and `u-boot-h432b-ram`,
`u-boot-h432b-nand-auto`, `u-boot-h432b-reboot-test` and
`u-boot-h432b-chain` intermediate loaders are no longer selectable recipes.
Their applicable code is shared through `.inc` files or superseded by the
runtime/maintenance targets. Old source and qualification records remain
recoverable from Git history.

Patch ordering and established deploy-directory identifiers remain stable
where external tools depend on them. Recipe cleanup is not a firmware upgrade,
and no new hardware qualification is implied by an include-file refactor.
