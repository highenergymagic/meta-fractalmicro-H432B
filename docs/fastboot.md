# Fastboot RAM loader

This is a separate high-RAM development loader, not a NAND-flashable image.
Build `u-boot-h432b-fastboot` through openh432-build's pinned Docker launcher.
The resulting role is `ram53-fastboot-only/u-boot.bin`, linked at 0x46000000.
The numeric prefix is a retained artifact identifier, not a device revision.
See the [boot contract](boot-contract.md) for all image roles.

The board adapter implements the standard Android fastboot USB protocol on
U-Boot 2012.10's S3C UDC. It is not a wholesale backport of a later U-Boot
release. Mainline 2012.10 did not provide fastboot. New component source is
GPL-2.0-or-later; build metadata is MIT.

## Interface and commands

Interface 0 retains the development EP0 diagnostic shell. Interface 1 is
ff/42/03 with bulk IN 0x81 and OUT 0x02, 512-byte high-speed / 64-byte full-speed
packets. It reuses the project's experimental USB identity and reports
`OPENH432-FASTBOOT`, a development serial string, not a unique device ID.
Only one such device should be selected by this serial.

Supported commands: `getvar`, `download`, `boot`, `reboot`.
Useful variables: product, version, version-bootloader, serialno,
max-download-size, download-size, download-crc32, is-userspace, secure,
unlocked, has-slot:boot. Unknown commands/variables return FAIL.
There is no storage backend: `flash` and `erase` return FAIL.
Reboot returns to the device's installed boot path; it does not automatically
reload this RAM-only loader. Reboot-bootloader, continue, OEM and flashing-unlock commands
are not implemented.

A standard host `fastboot stage FILE` exercises download without executing
anything. Compare `fastboot getvar download-crc32` with the host CRC.
CRC32 is transfer diagnostics, not authentication. The development loader
is deliberately unlocked and retains a privileged physical USB shell.

## Boot image contract

The OS layer's `openh432-fastboot-ram` target creates
`openh432-ram-boot.img`, an Android header-v2 envelope containing the Linux
zImage, XZ initramfs and DTB. Android userspace is not needed.

- Header page 2048 bytes, version 2, size 1660.
- No second stage, recovery overlay or header command-line override.
- Kernel destination 0x42000000, maximum 32 MiB.
- DTB destination 0x44000000, maximum 1 MiB.
- Initramfs destination 0x44400000, maximum 16 MiB.
- Download buffer 0x48000000, maximum 32 MiB for the whole padded envelope.

The parser checks all spans before copying, exact final size, fixed load
addresses, ARM zImage and FDT headers, and rechecks the downloaded CRC.
The boot path copies into the existing RAM slots and uses the qualified
bootz/USB teardown sequence. Action happens after the final USB OKAY completes.
Image format checks do not authenticate the publisher.

After explicitly staging and launching the fastboot RAM loader using the
[host tools](https://github.com/highenergymagic/openh432-tools/blob/main/docs/installation.md):

```sh
fastboot -s OPENH432-FASTBOOT getvar version-bootloader
fastboot -s OPENH432-FASTBOOT boot openh432-ram-boot.img
```

These host commands are separate from building. No build opens USB.
Neither the loader nor the boot envelope should be sent to the factory
EBOOT uploader as a CE/NAND carrier.

## Validation and remaining work

On a real U2, standard fastboot 35.0.2 enumerated the fastboot RAM loader, downloaded a
10,100,632-byte rootfs in 8.14 s with matching CRC, and transferred the complete
13,735,936-byte boot envelope in about 11 s. Linux reached the systemd USB
console and passed the baseline health/read-only hardware checks. Eleven
download lengths from 1 to 65536 bytes passed CRC, including USB packet and
16 KiB request boundaries. Unknown-variable and erase rejection were observed.
Native C parser tests run inside the pinned build container.

Only high-speed USB has been hardware-tested. This is not production security
qualification or a persistent flashing interface. Separate Linux/UBI provisioning
and NAND boot tests are documented in [NAND support](nand.md); they do not add
storage-write support to fastboot.

Protocol: https://android.googlesource.com/platform/system/core/+/master/fastboot/README.md
Image layout: https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h
