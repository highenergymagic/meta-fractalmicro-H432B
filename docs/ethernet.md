# Wired Ethernet

## Hardware evidence

The H432B's wired controller was identified by live register reads as an
SMSC LAN9220: BYTE_TEST is `0x87654321`, ID_REV is `0x92200000`.
The stock CE module is named smsc9118.dll, but that name describes a driver
family rather than the fitted chip.

The board uses SROM bank 5 at `0xa8000000`, with a 16-bit data bus.
The inherited SROM_BW bank5 nibble is `0xd`; SROM_BC5 is `0x040e1460`.
CE's board initialization selects function 2 on MP01 pins 5, 6 and 7,
uses GPH1[1]/EINT9 with low-level signaling and a pull-up, and drives an
active-low controller reset on GPH1[2].

A live transient test changed only MP01[5] from function 5 to function 2.
That changed the byte-test result from all ones to its correct signature.
The original pinmux nibble was then restored and read back successfully.
The test did not reset the controller, write its EEPROM, or write NAND.

## Linux configuration

The device tree selects the upstream `smsc911x` driver through
`smsc,lan9220` and `smsc,lan9115` compatibles. It specifies 16-bit register
access, the internal MII PHY, the SROM clock, chip-select/bus pinmux, an
active-low open-drain interrupt, and the known reset GPIO.

The kernel fragment builds the controller and SMSC PHY drivers into the
kernel. There is no out-of-tree Ethernet data-path driver.

SROM bank timing is **inherited from the factory boot path**, not initialized
by this Ethernet node. Standalone reset/power sequencing and suspend/resume
remain unqualified. Do not infer an independently power-managed Ethernet
subsystem from successful register identification.

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

Chip identity, byte ordering, chip-select correction and restoration are
hardware-verified. Source contract tests pass. The enabled Ethernet kernel
configuration has not yet been compiled or booted at this checkpoint.
PHY discovery, IRQ self-test, cable link, DHCP and packet transfers are still
pending. No networking configuration or network-accessible shell is added.

Upstream references:
[Linux binding](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/Documentation/devicetree/bindings/net/smsc,lan9115.yaml?h=linux-6.12.y),
[Linux driver](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/net/ethernet/smsc/smsc911x.c?h=linux-6.12.y).
