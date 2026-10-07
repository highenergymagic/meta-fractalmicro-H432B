# Power management

## Availability

The normal runtime provides KEY_POWER input and a PMIC inspection bus.
Electrical poweroff, wake-on-button and full suspend/resume are not qualified.
The OS ignores power-key actions until a complete sleep/wake path is available.

The factory user-visible off/on behavior is suspend/resume, including removal
of braille-cell drive power, rather than a cold shutdown. GPA0[6] appears in
the display power-down path; it is not an established whole-board cutoff.
Keypad locking must not suppress the independent power switch.

## Power switch and resume contract

The spring-loaded switch is GPH2[6]/EINT22, active high. The device tree uses
gpio-keys, KEY_POWER and 20 ms debounce. Event delivery is hardware-tested;
long-hold electrical behavior is not.

Factory wake enters physical 0x40020000 rather than the upstream Linux
INFORM0 resume pointer. That address overlaps the normal decompressed kernel.
Do not write a trampoline there or enable suspend in the normal runtime.

## PMIC interface

| Resource | Configuration |
| --- | --- |
| Control bus | GPIO I2C, GPD1[4] SDA and GPD1[5] SCL, open drain |
| Seven-bit address | 0x66 |
| Enable-control readback | Registers 0/1: 0x3e/0xf1 |
| DVS-slot readback | Registers 4/5/6: 0x79/0x57/0x57 |
| Observed selector outputs | GPH1[6]=1, GPH1[7]=0, GPH0[4]=0 |

The factory diagnostic identifies MAX8698. Physical SET-pin mapping, active
rail voltages and regulator constraints remain unqualified. Do not substitute
the MAX8998 driver; its register map differs.

The optional `h432b-power-inventory` tool reads only known control registers.
`--dvs` adds the three voltage-slot registers. Transactions write a register
pointer, not a register value; there is no address scan or voltage change.
The normal runtime and `linux-h432b-power-test` share the bus description.
The diagnostic image uses the installed slot-B root with read-only storage.

CPU frequency is 800 MHz. Factory menu clock profiles do not establish a
qualified Linux DVFS sequence. A voltage-aware implementation must coordinate
regulators, PLLs, dividers and DRAM refresh. Readback of programmed slots is
not measurement of the active voltage.
See the [MAX8698C data sheet](https://atta.szlcsc.com/upload/public/pdf/source/20210518/C2682647_D87189DADB797D327BCE49EA88D19166.pdf).

## Reserved resume diagnostic

`linux-h432b-resume-test` excludes the bottom 2 MiB from usable RAM:
0x40200000–0x4fffffff, with decompressed code at 0x40208000. A reserved page
at 0x40020000 contains a 12-byte ARM trampoline loading the resume address
from INFORM0 with interworking semantics.

The physical memory node still describes 256 MiB. The chosen usable-memory
limit survives the retained U-Boot's memory-node rewrite. The bridge verifies
the opt-in marker, range and instruction readback before registering suspend
operations; it does not modify EBOOT, PMIC settings or display power.

`openh432-resume-test` is a RAM-launch bundle using the installed slot-B
root. Normal boot at the relocated address and trampoline readback have
passed. Actual CPU sleep and factory-loader wake have not.

## Device callback diagnostics

`tests/check-pm-devices.sh` provides explicit freezer and device-only modes.
It validates the diagnostic profile, storage policy and selected PM level,
then restores PM settings afterward. It does not request real CPU sleep.

Freezer and device-only tests have returned to a responsive console, including
tests with SDIO attached and asynchronous callbacks. An earlier SDIO timeout
was not reproduced consistently; its cause remains unresolved. These tests
include a five-second kernel test delay and do not cover late/noirq stages,
wake sources or power-loss behavior.

USB gadget resume may create a new tty; reopen it before assessing console
health. The diagnostic host-PHY lifecycle patch pairs init/power-on with
power-off/exit and removes the observed lifecycle warning. External hub and
adapter enumeration have passed device-only recovery; this does not qualify
an uninterrupted open serial session.

See [USB host](usb-host.md) for board controls and port mapping, and
[battery telemetry](battery.md) for the separate fuel-gauge interface.
