# Wired Ethernet

## Availability

Wired Ethernet is included in the normal runtime. The systembase configures
DHCP through systemd-networkd. Network access and key-authenticated maintenance
SSH have been exercised on hardware. Deep-sleep recovery has passed DHCP,
SSH and checksum-verified bidirectional transfers on one device cycle;
long-duration and repeated-cycle qualification remain outstanding.

## Hardware configuration

The H432B's wired controller was identified by live register reads as an
SMSC LAN9220: BYTE_TEST is `0x87654321`, ID_REV is `0x92200000`.
The stock CE module is named smsc9118.dll, but that name describes a driver
family rather than the fitted chip.

The board uses SROM bank 5 at `0xa8000000`, with a 16-bit data bus.
The inherited SROM_BW bank5 nibble is `0xd`; SROM_BC5 is `0x040e1460`.
CE's board initialization selects function 2 on MP01 pins 5, 6 and 7,
uses GPH1[1]/EINT9 with low-level signaling and a pull-up, and drives an
active-low controller reset on GPH1[2].

## Linux configuration

The device tree selects the upstream `smsc911x` driver through
`smsc,lan9220` and `smsc,lan9115` compatibles. It specifies 16-bit register
access, the internal MII PHY, the SROM clock, chip-select/bus pinmux, an
active-low open-drain interrupt, and the known reset GPIO.

The kernel fragment builds the controller and SMSC PHY drivers into the
kernel. There is no out-of-tree Ethernet data-path driver.

SROM bank timing is **inherited from the factory boot path**, not initialized
by this Ethernet node. The board PM bridge saves and restores those timings.
For non-wake Ethernet, suspend closes the interface and drains its IRQ/NAPI
work; resume pulses the known controller reset and reopens the interface.
The normal systemd-networkd configuration reacquires DHCP. Wake-on-LAN is not
enabled; only the power switch wakes this board.

## Diagnostics

The optional `h432b-ethernet-probe` recipe builds two explicit tools:

- `h432b-ethernet-probe --host-bus|--controller`: fixed, read-only register
  allowlist; controller reads stop if BYTE_TEST does not match.
- `h432b-ethernet-mux-test --test-and-restore`: requires the known initial
  MP01[5] function 5, temporarily selects function 2, reads identification,
  then restores the original nibble. It is a hardware-changing diagnostic,
  not a read-only probe or a production service. Abnormal process termination
  or a bus fault can prevent restoration; do not run it concurrently with
  other pinmux users or once the kernel Ethernet driver owns the interface.

Neither tool runs automatically or belongs to the normal runtime image.
Diagnostics and device logs stay out of the source repository.

## Validation status

The LAN9220 identity and register byte ordering have been verified. Ethernet
has provided network access and maintenance SSH on the NAND system, including
a session maintained while USB was disconnected. Performance, broad PHY/link
interoperability and extended suspend-cycle reliability are not qualified.

Upstream references:
[Linux binding](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/Documentation/devicetree/bindings/net/smsc,lan9115.yaml?h=linux-6.12.y),
[Linux driver](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/net/ethernet/smsc/smsc911x.c?h=linux-6.12.y).

## Factory Ethernet identity

The stock bootloader supplies an ARGS v1.1 handoff in low RAM. Its Ethernet
address originates in the preserved factory NAND configuration, not a
per-boot random seed. Linux-capable U-Boot stages copy and validate that
handoff before Linux reuses RAM, then populate both `mac-address` and
`local-mac-address` on the LAN9220 device-tree node.

The parser rejects invalid handoff signatures/versions, zero, broadcast,
multicast, and the stock driver's shared fallback address. An invalid handoff
does not cause a NAND write or synthesis of a replacement factory address;
U-Boot reports that factory identity is unavailable.

Linux exposes the effective Ethernet address at
`/sys/class/net/eth0/address`. The root device-tree property
`/sys/firmware/devicetree/base/fractalmicro,board-id` contains a NUL-terminated
`FM-H432B-MAC-` identifier derived from the validated factory Ethernet MAC.
`fractalmicro,board-id-source` identifies that derivation explicitly.

This board ID is **not a verified manufacturer serial number**. No
`serial-number` property is fabricated. The stock processor-model-derived
boot name and a removable battery's identity are not suitable system serials.
Linux-capable U-Boot stages use the same MAC-derived board ID for their
maintenance USB serial and the Linux ACM console serial. Missing or invalid
factory identity leaves those serials absent, not a shared fabricated value.
The USB serial identifies the software's board-ID derivation; it does not
change the distinction from a manufacturer serial number.
Per-device addresses and identifiers must never be embedded in published
sources or generic firmware artifacts.

On the qualification device, recovery-assisted launch and an
independent plain-Reset NAND boot expose the same factory address and derived
board ID. Linux reports address assignment type 0, and systemd preserves the
address. This does not qualify other firmware revisions or establish a
manufacturer serial-number source.
