# H432B NAND layout and qualification

The measured device is Samsung EC/DC, 512 MiB SLC: 4096 eraseblocks of
128 KiB, 2048-byte pages and 64-byte OOB. Internal SD is a separate device.

## Physical regions

| Region | Offset | Size | Policy |
|---|---:|---:|---|
| Factory boot and CE-carried U-Boot | 0 | 4 MiB | Protected |
| Shared Linux UBI pool | 4 MiB | 507 MiB | Explicit provisioning only |
| Reserved tail/possible future flash BBT | 511 MiB | 1 MiB | Protected |

The factory prefix includes StepLoader, EBOOT, its boot table, and the
currently working CE-carried U-Boot. Do not replace this with a guessed flat
1 MiB EBOOT partition. Factory normal pages use a different ECC layout.

The Linux driver uses standard software BCH8 per 512 bytes: 52 ECC bytes in
OOB12..63, first two bytes reserved for bad markers, full-page programming.
It reconstructs a RAM BBT; it does **not** automatically write a flash BBT.
A reserved tail is not itself an installed BBT.

Profiles selected by the build launcher are:

- readonly (default): no program/erase operation can pass the controller guard.
- scratch: only physical 0x1fee0000..0x1ff00000, one 128 KiB block.
- ubi: only physical 0x00400000..0x1ff00000, the shared Linux pool.

Building a profile never deploys, erases, formats, or accesses USB.
The whole NAND operation is validated before controller accesses, including
split program transactions. Boot/tail ranges remain hard protected in the
driver. Native guard tests cover all 4096 eraseblock addresses.

## Bounded UBI capacity

Full-page I/O leaves 124 KiB logical eraseblocks. Static volumes:

| Volume | LEBs | Maximum stored image |
|---|---:|---:|
| kernel_a / kernel_b, each | 132 | 15.984 MiB |
| recovery | 264 | 31.969 MiB |
| systembase_a / systembase_b, each | 1651 | 199.926 MiB |

Two dynamic bootstate volumes reserve two LEBs each. This reserves capacity;
it does not implement an A/B update/rollback policy yet.

The planner subtracts observed bad blocks, another 80 PEBs for future bad
blocks, and four PEBs for UBI layout/WL/atomic-change needs. With one observed
bad pool block, 137 additional LEBs remain unallocated. Actual UBI-reported
capacity must also pass before creating volumes. No autoresize volume may
consume this headroom.

The base-image class rejects images exceeding either the volume capacity or
the user's hard 200 MiB cap, even if a configuration override raises the
declared maximum. The current SquashFS image is DEBUG userland, including a
physical USB root shell; it is not a secured production release.

ubiblock is a block-device view of a static UBI volume, not a separate NAND
partition. SquashFS base images belong on static volumes; mutable state
belongs in UBIFS/dynamic volumes or the separately planned SD layout.

## Evidence, not assumptions

The first Linux scratch erase/program/readback passed for all 64 pages,
with no ECC failures or corrected bits. Raw inspection confirmed the bad
markers were unchanged. The protected 4 MiB prefix remained byte-identical,
including OOB. A full raw+OOB backup exists privately; it is not redistributed.

Independent U-Boot BCH qualification passed: parity matched Linux for all
four sectors; 1–8 injected bit errors per sector were repaired in RAM; a full
128 KiB corrected NAND read matched the host CRC. Its heap/stack layout has a
build-time overlap guard, including a regression for the initially bad layout.

UBI provisioning and full kernel/base-volume SHA256 readbacks passed on the
test device. Seven bounded volumes exist with 138 additional free PEBs beyond
UBI's internal reserves. Boot prefix plus OOB stayed byte-identical to backup.
A missing SquashFS parent Kconfig option was fixed and is now a fatal build
check; the corrected kernel has been written and verified.

Interactive NAND boot passed: U-Boot read the static kernel volume and
launched Linux; independent full SHA256 readbacks, SquashFS mount, RT,
zero-taint and systemd-health checks passed. The corrected RAM55 automatic
reader also passed those checks without a host kernel upload. Host-observed
time from its launch to the Linux shell was 136.261 seconds, of which
Linux/systemd startup was 28.696 seconds. The uncached reader is not yet
optimized. The first automatic attempt exposed two integration bugs:
mtdparts defaults were not applied to the volatile environment, and the
USB error fallback attempted double registration. Both are fixed.

The separate NAND56 CE-carried two-stage bootstrap builds, including
image/heap/stack overlap checks, and its first factory-assisted installation
is under qualification. Normal-reset boot has not yet been tested. Its packager
reproduces the qualified legacy carrier byte-for-byte, but this does not establish normal-reset reliability of the larger image. The kernel
bundle still contains a DEBUG initramfs, not production root-switch logic.
No A/B rollback policy is implemented.

U-Boot NAND writes are disabled at both MTD callbacks and the controller
command interface. The immutable EBOOT/NAND51 rollback artifacts are retained
privately. Never substitute a vendor EBOOT download for the actual device's
backup; they are not necessarily identical.
