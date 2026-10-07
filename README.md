# meta-fractalmicro-H432B

Hardware support for running OpenH432 on the HIMS BrailleSense U2.

OpenH432 is a Fractal Microsystems project to extend the useful life of
existing braille notetakers with a Linux-based operating system. This
Yocto/OpenEmbedded board support layer targets the H432B platform, based
on Samsung's S5PV210 processor.

The project is under active development. It is not yet a replacement for
the device's accessible, everyday functionality.

## What this layer provides

- Machine configuration for `MACHINE = "h432b"`.
- Linux kernel configuration, device tree and board-specific patches.
- U-Boot recipes and patches for the device's boot and development workflows.
- Hardware-specific storage protections and image layout constraints.

The kernel is based on Linux CIP with a separately integrated PREEMPT_RT
patch. The bootloader is based on U-Boot 2012.10. Exact source revisions,
patches and toolchain inputs are recorded in the recipes and build locks.

Operating-system policy and image composition belong to
[meta-fractalmicro-openh432](https://github.com/highenergymagic/meta-fractalmicro-openh432),
not this hardware layer.

## Building

Start with [openh432-build](https://github.com/highenergymagic/openh432-build).
It provides the pinned layer revisions, build container and commands for
building a complete development image. The supported layer series is
Yocto Wrynose.

This repository is a source layer, not a downloadable firmware package.
Building it does not install anything on a device.

## Hardware status

Linux has booted from NAND across repeated normal resets, retaining the
factory bootloader. USB diagnostics, bounded internal SD read/write testing,
NAND access and real-time kernel operation have been exercised on hardware.
The normal NAND runtime permits Linux UBI and internal SD writes, while
protecting the factory boot and BBT regions. Audio playback, bootloader braille,
wired Ethernet and GPS service integration have also been demonstrated.
Broader input, vibration, USB-host and removable-SD qualification remains
available through explicit diagnostic profiles.

The internal Si4702 FM receiver supports muted V4L2 tuning and signal scans.
Detected frequency peaks were corroborated against local broadcasts; audible
FM output, stereo reception and suspend remain unqualified. See the
[FM receiver guide](docs/fm.md).

Important work remains:

- Internal Bluetooth has passed discovery, pairing and L2CAP packet exchange
  with a speaker using the NAND runtime and manual factory initialization.
  Automatic initialization, persistent identity provisioning and audio playback
  remain unfinished. See the [Bluetooth guide](docs/bluetooth.md).

- Internal Wi-Fi has a NAND-integrated cfg80211 station driver. WPA2-PSK/CCMP,
  DHCP, checksum-verified bidirectional transfers and reconnect have passed. Operator-supplied
  firmware is required; throughput, warm restart and power saving remain
  development work. See the [Wi-Fi guide](docs/wifi.md) for the supported
  profile and exact qualification scope.
- Linux braille, keyboard, battery management and suspend support are incomplete.
- The normal kernel uses a minimal initramfs to mount the separate NAND
  SquashFS systembase. Writable state is volatile; persistent userdata and
  coordinated A/B updates are not complete.

See the [validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/status.md)
for the scope of testing. Demonstrated hardware support is not a claim of
production readiness.

Host-side recovery transport and the stock-CE conversion guide are maintained
in [openh432-tools](https://github.com/highenergymagic/openh432-tools).
The loader firmware itself remains in this BSP layer.

## Technical documentation

- [Build targets](docs/targets.md): normal deployment, recovery and optional diagnostics.
- [Boot contract](docs/boot-contract.md): boot stages, image roles and deployment constraints.
- [NAND support](docs/nand.md): storage layout, protection boundaries and validation.
- [Internal SD](docs/internal-sd.md): controller identity, read-only baseline and opt-in write testing.
- [External SD](docs/external-sd.md): removable-slot configuration and read-only qualification.
- [Ethernet](docs/ethernet.md): bus configuration, networking and factory identity.
- [USB host](docs/usb-host.md): diagnostic support and socket topology.
- [Power management](docs/power-control.md): switch, PMIC and sleep/wake limits.
- [FM receiver](docs/fm.md): tuning, signal scanning and audio qualification limits.
- [Internal Bluetooth](docs/bluetooth.md): transport, tested scope and initialization gaps.
- [Internal Wi-Fi](docs/wifi.md): current support, firmware requirements and optional diagnostics.
- [Wi-Fi qualification](docs/wifi-qualification.md): tested images, results and unresolved behavior.
- [Tests](tests/README.md): offline checks versus explicit device qualification.
- [GPS receiver](docs/gps.md): UART mapping, shared runtime power control and assistance qualification.
- [Fastboot support](docs/fastboot.md): RAM download and boot workflow.
- [Boot performance](docs/boot-performance.md): measured bottlenecks and profiling limits.
- [Input support](docs/input.md): power-key qualification and remaining controls.
- [Battery interface](docs/battery.md): read-only power_supply telemetry, wiring and validation.
- [Source provenance](docs/provenance.md): origins and licensing of board support.

Read the boot contract before attempting deployment. RAM loaders and
persistent boot images are not interchangeable; fastboot currently supports
RAM boot, not persistent flash or erase.

## License and affiliation

New layer metadata is MIT-licensed. Linux, device-tree and U-Boot patches
retain their component licenses and copyright notices; the MIT license
does not relicense them. Proprietary vendor firmware and device dumps are
not included.

OpenH432 is an independent project and is not affiliated with or endorsed
by HIMS.
