# USB host ports

## Availability

The normal NAND kernel includes the onboard hub power/reset controls and
host-PHY lifecycle integration. Resume and external-SD diagnostics consume
the same h432b-usb-host.inc implementation. Earlier diagnostic tests enumerated
a PL2303 adapter at all three physical sockets. Descriptor transfers and
driver binding are verified; serial TX/RX is not.

## Hardware configuration

| Control | GPIO | Active state |
| --- | --- | --- |
| Hub enable | GPH2[7] | High |
| USB12 enable | GPH3[7] | High |
| USB3 enable | GPH1[3] | High |
| Hub reset | GPJ4[2] | Low |

These pins use disabled pull bias. Reset is held low for 100 ms before
release, following the qualified factory sequence. GPJ4[1] is the separate
braille latch and must not be modified. SYSTEM5V at GPE1[1] is inherited;
this implementation does not control it or program the PMIC.

The onboard hub identifies as 0409:005a, high-speed, four ports. The standard
`onboard-usb-dev` driver owns hub power and reset through the board DTS
and an ID-table extension. USB12 and USB3 are separate always-on fixed
regulators in the shared board configuration; their physical supply grouping and
selective power policy are not established. No unmeasured voltage is specified.

| Physical socket | Downstream hub port |
| --- | --- |
| Back panel | 2 |
| Left side, rearward | 3 |
| Left side, frontmost | 4 |

Hub port 1 is unidentified. Port numbering describes the tested hub topology,
not Linux USB bus numbers.

## Diagnostics

`tests/check-usb-hub.sh` checks hub/adapter identity, topology, speed and
driver binding beneath the physical EHCI controller. It does not open the
serial device or qualify payload transfers.

The optional `h432b-usb-power-test` helper provides fixed-register readback
and bounded manual enable/reset tests. It must not run while kernel drivers
own these pins. Register levels alone do not prove socket voltage. Prefer
the kernel-owned profile for functional enumeration tests.

## Validation limits

Automatic hub enumeration and PL2303 binding passed without a userspace GPIO
helper. Two device-only PM cycles returned with the hub and adapter bound.
Disconnect/re-enumeration can occur; preservation of an open serial session
is not guaranteed.

Hub and adapter re-enumeration have also passed after a runtime deep-sleep
cycle. Device power budgets, serial payload continuity and broad peripheral
compatibility remain unqualified. See [power management](power-control.md)
and the artifact-specific validation record for the tested scope.
