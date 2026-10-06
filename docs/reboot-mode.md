# Reboot-to-fastboot experiments

The installed boot chain does not currently implement reboot-to-fastboot.
Normal autoboot starts NAND loading immediately. USB enumeration during that
loading is not a fastboot command window: command processing runs only in
the maintenance loop.

## Candidate transport

S5PV210 INFORM7, at 0xe010f01c, is under investigation as a reset-retained
mailbox. INFORM0 is excluded because the upstream suspend path uses it.
A zero register inventory is not evidence of ownership or retention.
Neither the request writer nor the test consumer is enabled by the default
kernel, loader or NAND carrier recipes.

The opt-in `u-boot-h432b-reboot-test` recipe builds a RAM-only consumer.
Its protocol uses exact 32-bit values:

| Value | Meaning |
| --- | --- |
| 0x48344e4d | Normal boot |
| 0x48344642 | One-shot fastboot |
| Other | Ignore without writing |

Known requests are cleared and read back before selecting a boot path.
A failed clear stays in USB maintenance instead of loading NAND. Fastboot
requests short-circuit NAND initialization; the existing USB maintenance
loop then processes commands. This does not implement NAND flash commands.

The nonzero normal value is intentional: the pinned Linux 6.12 reboot-mode
notifier does not call its writer for a zero magic value. The opt-in
`linux-h432b-reboot-test` recipe uses the upstream syscon-reboot-mode driver
with a separate experimental DTB and isolated kernel source/deploy paths.
It does not provide virtual/kernel or change the default image selection.
Its intended userspace interface is `systemctl reboot --reboot-argument=bootloader`
(or the equivalent `fastboot` argument), not a /dev/mem userspace writer.
The Linux writer and RAM consumer have passed the split hardware test below;
an installed automatic handoff is not yet qualified.

One hardware experiment confirmed that the retention-test marker survived
a software reboot through the existing factory boot chain and installed Linux.
RST_STAT reported a software reset; the marker was then explicitly cleared
and verified. An additional physical Reset in an earlier experiment cleared
it. This is not a guarantee across power removal, suspend or other firmware.
The retention result does not qualify the new kernel or consumer.

## Diagnostics and limits

The optional `h432b-reboot-probe` recipe builds a read-only fixed-register
inventory and a separate explicit retention tester. The latter requires an
empty INFORM7 before writing 0x48345254; cleanup writes zero only if that
exact marker remains. It does not reboot, access NAND, or select arbitrary
registers. These tools are not installed in the default image.

Consumer mock tests cover known/unknown values, one-shot consumption and
clear failure. Source tests ensure the normal carrier does not enable the
experiment. Compilation and tests are not hardware qualification.

A persistent boot-control record is a separate design decision. A raw NAND
implementation needs ECC, bad-block handling, redundancy, and interrupted-
write recovery. Do not add a single fixed-offset flag or change partitions
for this experiment.

## Build validation

On 2026-10-06 the pinned native-amd64 OE container built both experimental
recipes: 939 tasks for the loader and 913 for the kernel. The loader's mock
tests ran in that container; all 61 layer source tests passed. The built
kernel has REBOOT_MODE and SYSCON_REBOOT_MODE enabled, and the compiled DTB
contains the matching mode values and offset.

| Artifact | SHA256 |
| --- | --- |
| RAM loader | `3cf2c2cac5e542c92a79d5258f163aac1ee15d85c7cb9242ad735df87f0985ad` |
| Test zImage | `8eb97b916d57dc45c6627fd227b99faa11204d8f315107bbb6feed3148d799d2` |
| Test DTB | `651f68c1823cbe2de7fe3fa24a736494acf99dc66fc8c50b7baae9d3cfd8ae4d` |

The kernel deploys beneath `kernel-reboot-test/`; the loader beneath
`ram-reboot-test/`. These are not installation images or CE update carriers.
No cross-host byte comparison has been performed for these artifacts.
The new consumer has not been installed in the persistent boot chain.

With this layer as a sibling of the pinned build repository, the development
build entry point is:

```sh
python3 scripts/bsp.py build --local-layers h432b-reboot-probe u-boot-h432b-reboot-test linux-h432b-reboot-test
```

The build does not open USB or deploy to hardware.

## Split hardware qualification

The test kernel booted from a fastboot RAM envelope with the reboot-mode driver
bound, zero failed systemd services and kernel taint zero. A normal privileged
`systemctl reboot --reboot-argument=bootloader` performed an orderly shutdown.
The existing USB bootstrap then exposed the expected 0x48344642 in INFORM7.

Without rewriting the register or pressing physical Reset, the experimental
consumer was staged into RAM and launched. Standard fastboot `getvar version`
responded with 0.4; INFORM7 read zero. A subsequent fastboot reboot returned to
the known bootstrap with no stale request.

This demonstrates the Linux notifier, retention through the factory chain,
and consume-before-fastboot behavior. It is deliberately a split test:
a host still stages the RAM consumer. It does not establish an installed,
unattended Linux-to-fastboot path, NAND flashing support, power-loss persistence,
or support for untested hardware units.
