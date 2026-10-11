# Boot and image contract

OpenH432 retains the factory first-stage loader and EBOOT. EBOOT loads a
CE-format carrier containing a U-Boot bootstrap; Linux does not run under CE.

## Artifact roles

Numeric prefixes are stable build-output identifiers inherited from development,
not hardware revisions or a request to install successive versions.

| Recipe | Deploy directory | Role and link address |
| --- | --- | --- |
| `u-boot-h432b` | `nand51-raw` | USB-shell bootstrap at 0x40021000; raw binary requires CE packaging. |
| `u-boot-h432b-fastboot` | `ram53-fastboot-only` | Fastboot RAM loader at 0x46000000. |
| `u-boot-h432b-nand` | `ram54-nand-reader` | Interactive read-only NAND loader at 0x46000000. |
| `u-boot-h432b-bootstate` | `ram-bootstate` | Explicit bootstate diagnostic shell at 0x46000000; no automatic Linux boot. |
| `u-boot-h432b-bootstate-chain` | `nand-bootstate-test-ce-carrier` | CE carrier for the bootstate diagnostic stage; not the runtime selector. |
| `u-boot-h432b-ab` | `ram-ab` | Persistent A/B selector and one-shot fastboot stage at 0x46000000. |
| `u-boot-h432b-ab-chain` | `nand-ab-chain-raw` and `nand-ab-ce-carrier` | Factory-compatible carrier containing the A/B stage. |
| `u-boot-h432b-maintenance` | `ram-maintenance-b` | Legacy fixed kernel-B reader and one-shot fastboot stage at 0x46000000. |
| `u-boot-h432b-maintenance-chain` | `nand-maintenance-chain-raw` and `nand-maintenance-ce-carrier` | Legacy fixed-B carrier; ignores persistent A/B state. |
| `u-boot-h432b-nand-profile` | `ram57-nand-profile` | RAM-only NAND timing and instruction-cache experiment. |
| `u-boot-h432b-nand-timer` | `ram-nand-timer` | RAM-only PWM4 clock and NAND timing tests. |
| `u-boot-h432b-nand-subpage` | `ram-nand-subpage` | RAM-only BCH partial-page read experiment. |

Verify the deployed `ROLE.txt` before using any artifact. High-RAM images must
never be flashed directly or disguised as low-address code. Building a recipe
does not stage, execute, install or qualify its output.

## Factory handoff

Low-address bootstrap code links at physical 0x40021000. The factory NK carrier
loads at CE address 0x80020000: ECEC at +0x40, ROMHDR pointer 0x80020100 at +0x44,
and relative offset 0x100 at +0x48. Both pointer fields are required for normal
NAND boot as well as recovery-assisted launch.

The two-stage bootstrap embeds a checked high-RAM automatic reader, copies it
to 0x46000000 and transfers control. The carrier is explicitly bounded.
The packager's byte-for-byte regression against a historical carrier is a
format check, separate from device qualification.

Physical DRAM starts at 0x40000000 and totals 256 MiB. Image/heap/stack overlap
checks run during NAND-reader and bootstrap builds.

### Cortex-A8 branch-predictor hardening

Cortex-A8 requires ACTLR bit 6 (`IBE`) before its `BPIALL` operation can
invalidate the branch predictor. The common U-Boot entry sets this CPU
prerequisite independently of board clock and DRAM initialization, preserving
the other ACTLR bits. Both the factory-address bootstrap and the high-RAM
stage inherit the change. It is based on the
[upstream U-Boot Cortex-A8 mitigation](https://github.com/u-boot/u-boot/commit/7b37a9c732bf).

`u2 cpu` reads MIDR and ACTLR without modifying them; it succeeds only for a
Cortex-A8 with IBE set. IBE is writable only in Secure state. A Nonsecure
handoff cannot establish it, and a successful build does not prove that the
installed firmware has done so.

Linux must still enable and perform its own branch-predictor hardening. Its
IBE validation and vulnerability reporting are retained. Qualification requires
checking the live register and Linux mitigation status after normal boot,
software reboot and suspend/resume; setting one firmware bit alone is not a
claim that all speculative-execution vulnerabilities are mitigated.

## Linux payload

Linux uses zImage, DTB and compressed initramfs. Fastboot and NAND readers
use a bounded Android-v2 envelope; the compressed root slot is limited to
16 MiB. NAND kernel volumes have a 132 x 124 KiB capacity.

Normal NAND boot loads the runtime kernel and a root-handoff initramfs with
the startup cue, not the full userspace.
The A/B stage selects a kernel volume and supplies `rauc.slot=A` or
`rauc.slot=B` to Linux. The initramfs mounts the corresponding SquashFS
systembase through ubiblock and switches root to systemd, with a volatile
writable overlay. The current systembase is slot-independent. Historical
`-b` image target names remain for build compatibility; an unmanaged boot
without a selected slot retains the explicit B handoff.

The standalone RAM recovery bundle uses the same runtime kernel with a complete
RAM root filesystem and can boot without a provisioned UBI pool.

## Persistent boot policy

Two dynamic UBI volumes, `bootstate_a` and `bootstate_b`, hold redundant
4 KiB U-Boot-format environments with CRC32 and incremental serial flags.
These copies are not tied to the corresponding operating-system slots.
The loader imports only the version, `BOOT_ORDER`, `BOOT_A_LEFT` and
`BOOT_B_LEFT` fields; executable environment settings are not imported.

Each bootstate volume has two 126,976-byte LEBs. The 4 KiB record contains
a little-endian CRC32, an 8-bit incremental serial and NUL-separated variables:
`H432_BOOTSTATE_VERSION=1`, `BOOT_ORDER=A B` (or `B A`), and decimal
`BOOT_A_LEFT` / `BOOT_B_LEFT` counts from 0 through 255. Normal healthy
allowance is three attempts. Required duplicates and malformed records are
rejected; serial selection handles wrap from 255 to zero.

The selector visits slots in `BOOT_ORDER` and skips zero attempt counts.
Before launching a kernel, it decrements the selected count, updates the
alternate environment volume and verifies the stored record. Failed persistence
prevents launch. Both invalid copies or exhausted slots lead to USB maintenance,
not automatic reinitialization. Returned image-load failures consume further
attempts; a hung kernel requires a reset before selection runs again.

Linux receives the selected slot and attempt serial. A timer schedules the
health service without blocking the multi-user target. After a 30-second health
interval following its console dependencies, `FMMarkBootSuccessful.service`
checks the mounted slot, BRLTTY, the tty1 user session and service restart counts. It restores that slot to three
attempts under the environment-library lock, only if the recorded attempt is
still current. Network connectivity is not a success requirement.

An update must first make its inactive destination ineligible, write and verify
both the kernel and systembase, then activate the pair in one environment
transaction. Never update the mounted systembase. A legacy fixed-B carrier
ignores this policy and must not be mistaken for the A/B carrier.

Hardware-watchdog recovery, signed update-bundle installation and persistent
userdata migration are not provided by this policy. RAUC-compatible variable
names do not imply that a RAUC installer is installed. Software fallback on
detected root-handoff failure requests a reset; it does not guarantee recovery
from a wedged kernel or reset controller.

### Bootstate utilities

The target package `h432b-bootstate-check` shares the loader's state decoder:

- `--inspect COPY0 COPY1`: validate two captured records without writing NAND.
- `--emit-initial-b`: emit an initial B-only record to stdout; file generation
  only, not provisioning or installation.
- `--mark-good A|B SERIAL`: perform a locked, serial-checked success update.
  Use the health-gated service rather than invoking this directly in normal use.

The standard systembase supplies `/etc/fw_env.config` for volumes
`/dev/ubi0_5` and `/dev/ubi0_6` at offset zero, environment size `0x1000`.
These must be named `bootstate_a` and `bootstate_b` with the required dynamic
geometry. The service verifies the layout before writing. A libubootenv
adaptation propagates lock errors and permits a valid redundant peer when
one volume is unreadable after an interrupted UBI update.

The diagnostic loader's `u2bootstate` command provides `inspect`,
`consume` and `boot` operations. Inspection can trigger UBI attachment
repairs; consume and boot persist an attempt. None is a forensic capture tool.

The [NAND documentation](nand.md) describes ECC and bounded UBI layout.
The factory prefix uses a different ECC format and must not be rewritten by
the Linux data driver. Installed EBOOT may differ from vendor download images:
preserve each device's actual raw+OOB backup privately. Internal SD has not
been repartitioned in qualification.

USB diagnostic interfaces provide privileged physical access, not a production
authentication boundary. Do not expose them through a network. There is no
end-user installer, and new artifacts do not inherit earlier test results.
