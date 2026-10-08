# H432B NAND layout and qualification

The measured device is Samsung EC/DC, 512 MiB SLC: 4096 eraseblocks of
128 KiB, 2048-byte pages and 64-byte OOB. Internal SD is a separate device.

## Physical regions

| Region | Offset | Size | Policy |
|---|---:|---:|---|
| Factory boot and CE-carried U-Boot | 0 | 4 MiB | Protected |
| Shared Linux UBI pool | 4 MiB | 507 MiB | Runtime UBI writes; explicit initial provisioning |
| Reserved tail/possible future flash BBT | 511 MiB | 1 MiB | Protected |

The factory prefix includes StepLoader, EBOOT, its boot table, and the
CE-carried U-Boot bootstrap on a converted device. Do not replace this with a guessed flat
1 MiB EBOOT partition. Factory normal pages use a different ECC layout.

The Linux driver uses standard software BCH8 per 512 bytes: 52 ECC bytes in
OOB12..63, first two bytes reserved for bad markers, full-page programming.
It reconstructs a RAM BBT; it does **not** automatically write a flash BBT.
A reserved tail is not itself an installed BBT.

Profiles selected by the build launcher are:

- readonly (diagnostic default): no program/erase operation can pass the controller guard.
- scratch: only physical 0x1fee0000..0x1ff00000, one 128 KiB block.
- ubi: only physical 0x00400000..0x1ff00000, the shared Linux pool.

The normal runtime explicitly selects Linux-pool writability; the read-only
profile is not its storage policy.

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

Two dynamic bootstate volumes reserve two LEBs each and hold redundant 4 KiB
U-Boot environment records. The A/B loader persists attempt consumption before
launch; Linux acknowledges a healthy boot using the same format. See the
[boot contract](boot-contract.md#persistent-boot-policy) for the state schema
and paired-image activation sequence.

The planner subtracts observed bad blocks, another 80 PEBs for future bad
blocks, and four PEBs for UBI layout/WL/atomic-change needs. With one observed
bad pool block, 137 additional LEBs remain unallocated. Actual UBI-reported
capacity must also pass before creating volumes. No autoresize volume may
consume this headroom.

The base-image class rejects images exceeding either the volume capacity or
the platform's hard 200 MiB cap, even if a configuration override raises the
declared maximum. The current SquashFS image is DEBUG userland, including a
physical USB root shell; it is not a secured production release.

ubiblock is a block-device view of a static UBI volume, not a separate NAND
partition. SquashFS base images belong on static volumes; mutable state
belongs in UBIFS/dynamic volumes or the separately planned SD layout.

## Validation scope

Qualification on one device includes scratch-block erase/program/readback,
protected-prefix comparison including OOB, UBI
provisioning and complete kernel/base-volume hashes. Linux and U-Boot BCH8
parity agreed for all four page sectors; injected one-through-eight-bit RAM
errors per sector were corrected.

The persistent CE-carried bootstrap has booted Linux on repeated normal
resets without host uploads. Normal boot uses a minimal root-handoff initramfs
and the separate SquashFS systembase, not a full development initramfs.
See [boot performance](boot-performance.md) for measurements and limits.

Build checks cover image/heap/stack overlap, carrier format and required
filesystem configuration. The A/B and bootstate diagnostic loaders enable
bounded Linux-pool writes for UBI state updates and maintenance; they use the
hardware timer for program/erase deadlines. Even an inspection can cause UBI
attachment repairs and is not forensic read-only access. Other read-only
loader profiles retain their write guards. Factory prefix and tail writes,
and raw OOB programming, remain blocked. Kernel/base replacement uses Linux
UBI; fastboot has no flash/erase backend.

Exhausted-slot fallback and healthy-boot acknowledgement are device-tested.
Physical power-cut recovery, NAND endurance and complete stock-CE restoration
remain unqualified; a signed update installer is not implemented. An early
software-timer diagnostic retired one additional pool block; subsequent tests
used the corrected hardware timer. Detailed scope is recorded in the
[hardware validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md). Preserve each
device's actual raw+OOB backup; a vendor EBOOT download is not a substitute.

See the [installation guide](https://github.com/highenergymagic/openh432-tools/blob/main/docs/installation.md)
for provisioning prerequisites and the [update command reference](https://github.com/highenergymagic/openh432-tools/blob/main/docs/commands.md#guarded-slot-b-updates)
for bounded existing-volume updates.
