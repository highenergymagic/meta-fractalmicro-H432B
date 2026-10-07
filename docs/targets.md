# Build targets and source organization

## Normal deployment

| Target | Purpose |
| --- | --- |
| `linux-h432b-runtime` | The default Linux CIP/RT kernel; also used for standalone RAM recovery. |
| `u-boot-h432b-maintenance-chain` | Persistent CE carrier containing the fixed slot-B maintenance stage. |
| `u-boot-h432b-maintenance` | High-RAM NAND reader and one-shot fastboot stage, embedded by the carrier. |

The OS layer supplies `openh432-nand-b`, the minimal
`openh432-early-b` root handoff, and `openh432-systembase-b`.
The minimal initramfs performs the normal root-filesystem handoff.

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

| Kernel suffix / matching OS image suffix | Purpose |
| --- | --- |
| `wifi-test` | Runtime Wi-Fi driver with explicit transport and power-stage diagnostics. |
| `power-test` | Read-only-storage PMIC bus inspection baseline. |
| `battery-test` | Isolated qualification of the shared runtime battery driver. |
| `input-test` | Key, switch and vibration qualification. |
| `resume-test` | Suspend bridge and USB power-lifecycle work. |
| `external-sd-test` | Removable SD and USB-host qualification. |
| `sd-rw-test` | Bounded internal-SD filesystem write qualification. |
| `gps-test` | GPS qualification with the extended peripheral profile. |

Kernel recipes use the prefix `linux-h432b-`; image recipes use
`openh432-`. Shared drivers need not be duplicated to keep profiles isolated.
Support tools under `recipes-support` are generally opt-in. The bounded
`h432b-vibrator-test` command is included in the normal systembase but never
runs automatically. Input, battery, USB-host and external-SD diagnostics share
implementation fragments with the normal runtime rather than owning separate
driver copies.

The optional `u-boot-h432b-nand`, `-nand-profile`, `-nand-timer` and
`-nand-subpage` targets retain NAND inspection and performance experiments.
Building one does not promote its behavior into the persistent carrier.

## Compatibility

Deploy-directory identifiers are stable where host tools depend on them.
Use the recipe and ROLE.txt for the selected revision, not an artifact's
historical numeric prefix, to determine its role. Retired intermediate targets
remain available only by checking out their matching historical revision.
