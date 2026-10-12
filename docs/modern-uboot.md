# U-Boot 2026.10

The H432B loader runs behind the retained factory StepLoader and EBOOT. It
boots the selected NAND kernel, manages redundant A/B boot state, and exposes
USB fastboot for maintenance. It does not replace the factory recovery path.

## Build and image roles

| Recipe | Output directory | Entry and role |
| --- | --- | --- |
| `u-boot-h432b-modern-nand` | `modern-nand-ce-carrier` | `0x40021000`; persistent factory-compatible CE carrier, cached NAND boot |
| `u-boot-h432b-modern` | `modern-ram-fastboot` | `0x42000000`; RAM-entry development loader |
| `u-boot-h432b-modern-cache-test` | `modern-nand-cache-test` | Compatibility name for cache qualification |
| `u-boot-h432b-modern-cache-audit` | `modern-nand-cache-audit` | Additional cache/handoff checks; not the standard deployment target |

Use the pinned [build workflow](https://github.com/highenergymagic/openh432-build).
Only `u-boot-ce.b000ff` from the NAND-carrier recipe is an EBOOT flash input.
Raw `u-boot.bin` and ELF files are components, not interchangeable installers.
The packager validates the ELF entry and records the payload length and CRC
in the CE carrier. These checks identify its role; they do not authenticate it.

The persistent loader requires an existing Linux UBI pool and redundant boot
state. It does not provision a stock device. See the
[installation guide](https://github.com/highenergymagic/openh432-tools/blob/main/docs/installation.md)
for conversion prerequisites and the [boot contract](boot-contract.md) for
the storage layout. Legacy 2012.10 conversion targets remain available.

## Normal boot and maintenance

Normal boot selects an eligible slot, persists its attempt accounting, verifies
the kernel bundle, and hands off the slot-matched root-integrity descriptor.
Linux mounts the separate systembase through dm-verity and acknowledges a
healthy accessible-console boot. Exhausted candidates fall back according to
the shared boot-state format described in the [boot contract](boot-contract.md).

The loader uses cached NAND reads and reports its timing in the kernel command
line. Observed loader times are approximately six seconds on the qualification
device; this excludes Linux and userspace startup and is not a maximum bound.

USB maintenance uses the fastboot interface `1d50:6152`. Product strings identify
Fractal Microsystems and the device mode. The USB identifier is derived from
validated factory Ethernet identity where available; it is not a verified
manufacturer serial number. Factory recovery uses a different interface.

## Fastboot interface

| Operation | Behavior |
| --- | --- |
| `getvar all` | Reports the supported variable set |
| `getvar NAME` | Reports the value, or fails with `unrecognised variable` |
| `flash kernel_A FILE` / `flash kernel_B FILE` | Validates and writes an inactive kernel volume, then checks readback |
| `flash systembase_A FILE` / `flash systembase_B FILE` | Writes an inactive systembase volume and checks readback |
| `set_active a` / `set_active b` | Validates the complete kernel/systembase pair before committing selection |

Suffixes accept upper- or lowercase A/B. `boot` aliases `kernel`; `system`
aliases `systembase`. Unsuffixed write targets are rejected; always supply an
explicit slot suffix. A write invalidates
the target slot before modifying its volume. Install both matching artifacts
before activating the slot. Readback success alone does not mark it healthy.

Pair activation verifies the kernel's descriptor against the systembase data
and integrity tree. This full check occurs during activation, not on every
Linux boot. Boot-time dm-verity verifies blocks as they are read.

The raw download buffer is 112 MiB. Although the systembase storage limit is
200 MiB, images larger than the download buffer cannot be flashed by this
transport. Sparse/streaming downloads are not implemented. There is no MMC
backend for `systemext_A/B`, no erase command, no recovery-volume installer,
and no flashing-lock/unlock policy. Unsupported operations fail rather than
reporting a successful update. Factory boot regions and the NAND tail remain
outside the permitted write boundary.

This is an unauthenticated development interface. Neither A/B metadata nor
dm-verity supplies secure boot or a hardware root of trust.

## Braille boot menu

A brief beep and vibration announce a two-second entry window. Press Play,
the fourth media button, to enter. The initial label is `boot loader menu`.
All menu text uses uncontracted Grade 1 braille.

Use Dot 1 for the previous item, Dot 4 for the next item, and Enter to select.
Navigation wraps; a held key must be released before another action.

| Item | Action |
| --- | --- |
| Reboot now | Restarts normal boot |
| Reboot into boot loader | Enters fastboot maintenance |
| Reboot into recovery | Unavailable until a recovery image/backend is integrated |
| Wipe data / Factory Reset | Confirmation UI only; unavailable without persistent-data provisioning |
| Wipe Cache | Confirmation UI only; unavailable without cache provisioning |

Unavailable actions do not erase storage. Confirmation defaults to Cancel.

## Qualification and limits

NAND boot, repeated software reboot, accessible-console return, inactive-volume
updates, pair activation, and menu entry/navigation have been exercised on one
H432B. Detailed artifact scope is recorded in
[hardware validation](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md).
Host tests cover boot-state transitions, target resolution, integrity checks,
write boundaries, carrier roles and menu state transitions.

Intermittent whole-system freezes remain unresolved. The Linux hardware
watchdog provides runtime recovery, not a diagnosis or an endurance guarantee.
MMC provisioning, persistent userdata and factory-reset backends are not part
of this loader's supported deployment interface.
