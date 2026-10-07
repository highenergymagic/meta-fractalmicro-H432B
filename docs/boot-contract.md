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
| `u-boot-h432b-maintenance` | `ram-maintenance-b` | Current kernel-B reader and one-shot fastboot stage at 0x46000000. |
| `u-boot-h432b-maintenance-chain` | `nand-maintenance-chain-raw` and `nand-maintenance-ce-carrier` | Persistent low-address bootstrap containing the maintenance stage. |
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

## Linux payload

Linux uses zImage, DTB and compressed initramfs. Fastboot and NAND readers
use a bounded Android-v2 envelope; the compressed root slot is limited to
16 MiB. NAND kernel volumes have a 132 x 124 KiB capacity.

Normal NAND boot loads the runtime kernel and a minimal root-handoff initramfs.
It mounts the separate slot-B SquashFS systembase through ubiblock and switches
root to systemd, with a volatile writable overlay. The standalone RAM recovery
bundle uses the same runtime kernel with a complete RAM root filesystem and
can boot without a provisioned UBI pool. Neither path implements automatic
A/B selection or rollback.

The [NAND documentation](nand.md) describes ECC and bounded UBI layout.
The factory prefix uses a different ECC format and must not be rewritten by
the Linux data driver. Installed EBOOT may differ from vendor download images:
preserve each device's actual raw+OOB backup privately. Internal SD has not
been repartitioned in qualification.

USB diagnostic interfaces provide privileged physical access, not a production
authentication boundary. Do not expose them through a network. There is no
end-user installer, and new artifacts do not inherit earlier test results.
