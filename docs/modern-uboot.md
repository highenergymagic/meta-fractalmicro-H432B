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
| `flash systemext_A FILE` / `flash systemext_B FILE` | Writes an EROFS/verity image to the inactive internal-SD extension and checks readback; accepts raw or Android sparse transfers |
| `set_active a` / `set_active b` | Verifies the kernel, systembase and any kernel-declared extension before committing selection |

Suffixes accept upper- or lowercase A/B. `boot` aliases `kernel`; `system`
aliases `systembase`. Unsuffixed write targets are rejected; always supply an
explicit slot suffix. A write invalidates
the target slot before modifying its storage. Install all matching artifacts
before activating the slot. Readback success alone does not mark it healthy.

Pair activation verifies the kernel's descriptor against the systembase data
and integrity tree. This full check occurs during activation, not on every
Linux boot. Boot-time dm-verity verifies blocks as they are read.

Persistent-mode kernels additionally carry `fractalmicro,systemext-verity-v1`
in `/chosen`, copied from the same extension descriptor used by the rootfs.
Activation verifies the complete slot-matched extension using bounded memory.
Kernels without this property retain the NAND-only compatibility contract;
older persistent images must be rebuilt to gain the three-image check.

The MMC backend identifies the internal controller at `0xeb100000`, requires
the version 1 GPT layout, and permits writes only to its two extension
partitions. It does not provision the card, write cache/userdata, or accept
an external SD card as a substitute. Writes do not activate a slot. A failed
or interrupted update remains ineligible until an explicit successful
activation. This ordering is a recoverable update protocol, not an atomic
transaction across NAND and SD or a power-loss qualification.

The download buffer is 112 MiB **per download**, not per image. Standard Android
fastboot splits larger raw filesystem images into sparse fragments automatically.
Use the normal `flash` commands with the build's complete verity-wrapped images;
no custom host transport or filesystem conversion is required. Systembase
remains limited to 200 MiB and systemext to 2 GiB, including integrity trees.
Kernel envelopes remain raw and bounded by their NAND volume capacity.

The decoder accepts sparse v1.0 with 4 KiB blocks and RAW, FILL and DONT_CARE
chunks. It validates the complete downloaded fragment before changing storage.
MMC DONT_CARE ranges are skipped, preserving data from earlier fragments;
written ranges receive physical readback comparison. Static UBI systembase
updates require ordered, contiguous RAW/FILL extents, with only the skipped
prefix/suffix used by Android's splitting of a raw image. Deliberate holes,
out-of-order continuations and CRC-bearing encodings are rejected. A raw image's
zero-filled areas are supported through FILL chunks.

An intermediate NAND response means the fragment is staged, not that the
volume update is complete. UBI retains its update marker until all bytes
arrive; complete-volume readback checks the accumulated digest. An interrupted
transfer leaves the slot ineligible. After inspecting a failure, restart the
whole image from its first fragment; there is no persistent offset-resume
protocol. Activation streams the full systembase integrity check through a
bounded NAND cache, so it does not require image-sized RAM.

There is no erase command, no recovery-volume installer,
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
MMC extension writes, physical readback and combined activation have passed
device tests. Active-slot writes were rejected; a corrupt inactive extension
prevented activation without changing selection. The corrected three-image
set booted from NAND and passed runtime health checks. MMC provisioning and
factory-reset backends are not part of this loader's deployment interface.
Multi-download transfer qualification is recorded separately from the earlier
raw-image results in the hardware validation record.
