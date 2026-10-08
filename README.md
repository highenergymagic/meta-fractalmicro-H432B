# H432B board support layer

Yocto/OpenEmbedded hardware support for the HIMS BrailleSense U2, maintained
by Fractal Microsystems as part of OpenH432.

## Supported platform

| Setting | Value |
| --- | --- |
| Machine | `h432b` |
| Board | HIMS H432B / BrailleSense U2 |
| SoC | Samsung S5PV210, ARM Cortex-A8 |
| Yocto series | Wrynose |
| Layer dependency | OpenEmbedded Core |

This layer contains the machine configuration, Linux CIP kernel with
PREEMPT_RT integration, device tree, U-Boot 2012.10 adaptations and
board-specific image constraints. The A/B bootloader preserves the factory
boot chain and uses redundant UBI bootstate for attempt tracking and fallback.
Source versions and patch order are defined in the recipes.

Distribution policy and userspace packages belong to
[meta-fractalmicro-openh432](https://github.com/highenergymagic/meta-fractalmicro-openh432).
Host installation tools belong to
[openh432-tools](https://github.com/highenergymagic/openh432-tools).

## Build integration

Use the [OpenH432 build workflow](https://github.com/highenergymagic/openh432-build)
for the supported layer composition and pinned build container. It selects
`MACHINE = "h432b"` and supports native Linux x86-64 and ARM64 builders.

For local changes, place this checkout alongside the build, distribution
and assets repositories, then use the launcher's `--local-layers` option.
See [build configuration](https://github.com/highenergymagic/openh432-build/blob/main/docs/building.md).

## Board documentation

- [Hardware reference](docs/index.md): storage, connectivity, input, audio and power interfaces.
- [Build targets](docs/targets.md): deployment, recovery and diagnostic configurations.
- [Boot contract](docs/boot-contract.md): image roles and loader requirements.
- [Source provenance](docs/provenance.md): component origins and licensing.
- [Tests](tests/README.md): offline checks and device-test procedures.

The [support matrix](https://github.com/highenergymagic/openh432-build/blob/main/docs/status.md)
defines runtime support, diagnostic-only configurations and known limitations.
Consult it for the scope of peripheral and power-management qualification.

Read the boot contract and
[installation guide](https://github.com/highenergymagic/openh432-tools/blob/main/docs/installation.md)
before writing device storage.

## Licence

New layer metadata is MIT-licensed. Kernel, device-tree and bootloader changes
retain their component licences and copyright notices. Proprietary firmware
and device dumps are not included.

OpenH432 is independent of, and not endorsed by, HIMS.
