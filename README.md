# meta-fractalmicro-H432B

Fractal Microsystems' experimental Yocto/OpenEmbedded BSP for the HIMS
BrailleSense U2 (H432B / Samsung S5PV210). Independent project, not affiliated
with or endorsed by HIMS. Target series: Yocto 6.0 Wrynose.

This layer contains machine support, ordered Linux/U-Boot patches and
hardware configuration. OS policy lives in
[meta-fractalmicro-openh432](https://github.com/highenergymagic/meta-fractalmicro-openh432).
Use [openh432-build](https://github.com/highenergymagic/openh432-build) for
the pinned container and exact source/layer revisions.

## Development status

The Yocto RAM baseline has booted on a U2 with working USB diagnostics,
internal SD reads, raw NAND reads and PREEMPT_RT. Both U-Boot roles compile;
the device test retained the proven installed bootloader and RAM loader.
WM8983 mixer controls and the 50/63 volume ceiling passed muted checks;
audible playback was qualified in the earlier bring-up baseline, not retested
in this Yocto run. Default kernel NAND and SD writes remain blocked.
Explicit NAND-only write-window profiles have now passed BCH erase/program,
UBI provisioning and full readback. A RAM-staged U-Boot has loaded the kernel
and debug initramfs from NAND; the base SquashFS volume mounts read-only.
The NAND56 bootstrap candidate is under installation/normal-boot qualification. See [NAND status](docs/nand.md).
Wi-Fi enumerates over SDIO but has no working function driver. Linux braille, keyboard, suspend and battery management
remain incomplete. This is not a complete, secure replacement firmware.

Linux is CIP 6.12.111-cip32 plus the separately pinned upstream rt21 patch:
a project integration, not a claim of an official CIP RT release.
U-Boot is the existing 2012.10 port, deliberately not upgraded during migration.
Kernel/U-Boot use Arm GNU 14.3.rel1 in the pinned build container.

**No build target installs or flashes anything.** Raw NAND51-linked U-Boot
and the high-RAM RAM52 loader are different artifacts, neither a factory
update image. The RAM52 loader must never be put in a NAND carrier.
Read docs/boot-contract.md before discussing deployment.

A third, separate role, `u-boot-h432b-fastboot`, adds standard fastboot to the
RAM loader. It has booted the Yocto RAM image using an unmodified fastboot
host. It does not flash NAND. See [fastboot contract](docs/fastboot.md).

## Licensing

New recipe/build metadata is MIT. Linux/DTS and U-Boot patches retain their
component licenses and notices; the MIT license does not relicense them.
See docs/provenance.md. No vendor firmware, extracted firmware, device dumps,
decompilations or proprietary SDK material is included.
