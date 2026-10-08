# Power management

## Availability

The normal NAND runtime supports power-button deep suspend and resume.
Suspend removes braille-cell drive power; wake restores the cached display
and the same interactive session. The power switch is the only enabled wake
source. Other keys and both three-position selectors have been tested without
waking the device.

Peripheral recovery is qualified separately from core suspend:

| Interface | Verified after deep sleep | Remaining limits |
| --- | --- | --- |
| Ethernet | DHCP, SSH and checksum-verified bidirectional transfers | Interface resets/reopens; uninterrupted sessions and endurance unqualified |
| Wi-Fi | Automatic WPA2 station reassociation and interface-bound ICMP | Sustained transfers and repeated-cycle endurance unqualified |
| Bluetooth | Provisioned identity, radio parameters and HCI commands retained | Manual initialization required; connected-peer retention unqualified |
| Audio | Playback started after resume and stream-clock release | Open streams across suspend and gap-free boot playback unqualified |
| GPS | Checksum-valid NMEA reception | Assistance retention and acquisition performance unqualified |
| FM | Muted reopening and tuning | Reception and open handles across sleep unqualified |
| USB | Gadget reconnection and hub/adapter re-enumeration | Open serial sessions and payload continuity unqualified |
| RTC | Elapsed sleep-time accounting with network synchronization stopped | Battery-removal retention and long-term accuracy unqualified |

These checks cover specific images and individual device cycles, not a combined
peripheral endurance test. See the
[validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md#power-button-deep-suspend-qualification)
for artifacts and methods. Electrical poweroff is not implemented.

The factory user-visible off/on behavior is suspend/resume, including removal
of braille-cell drive power, rather than a cold shutdown. The display driver
controls its supply through GPJ0[3]; this is not a whole-board power cutoff.
Keypad locking must not suppress the independent power switch.

## Power switch and resume contract

The spring-loaded switch is GPH2[6]/EINT22, active high. The device tree uses
gpio-keys, KEY_POWER and 20 ms debounce. Event delivery is hardware-tested;
long-hold electrical behavior is not.

Factory wake enters physical 0x40020000 rather than the upstream Linux
INFORM0 resume pointer. Suspend-enabled runtime builds exclude the bottom
2 MiB of RAM and decompress at 0x40208000, keeping the fixed wake entry outside
Linux-managed memory. The bridge refuses incompatible memory layouts and
requires EINT22 to be the sole enabled external wake source.

## Real-time clock

The runtime enables the S5PV210 RTC at 0xe2800000 through the upstream Samsung
driver. Its register-access clock and unmanaged 32.768 kHz source are both
described in the device tree. This description does not program the PMIC or
establish the physical source of the clock signal.

Linux exposes `/dev/rtc0` and `/sys/class/rtc/rtc0`. Standard RTC-core support
accounts for elapsed suspend time and synchronizes the RTC from an NTP-adjusted
system clock. The RTC is not wake-capable on this board, and the wake-alarm
sysfs interface is absent.

An initially invalid RTC needs a trusted time source. The qualification image
obtained network time and initialized the RTC in UTC; subsequent ticking and
elapsed time across deep sleep were verified independently of network clock
correction. Battery-removal retention, long-term accuracy and repeated
cold-start initialization remain unqualified.

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

## Resume memory layout

The runtime and `linux-h432b-resume-test` exclude the bottom 2 MiB from usable RAM:
0x40200000–0x4fffffff, with decompressed code at 0x40208000. A reserved page
at 0x40020000 contains a 12-byte ARM trampoline loading the resume address
from INFORM0 with interworking semantics.

The physical memory node still describes 256 MiB. The chosen usable-memory
limit survives the retained U-Boot's memory-node rewrite. The bridge verifies
the opt-in marker, range and instruction readback before registering suspend
operations; it does not modify EBOOT or PMIC settings. The braille driver
separately shifts a neutral frame, waits 100 ms, then removes cell power.
Resume restores the supply, waits 100 ms and restores the cached frame.

`openh432-resume-test` is a RAM-launch bundle using the installed slot-B
root. NAND runtime boot at the relocated address and trampoline readback have
passed. CPU sleep and factory-loader wake have passed one device cycle; this does
not qualify every peripheral's resume behavior.

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
health. The shared host-PHY lifecycle patch pairs init/power-on with
power-off/exit and removes the observed lifecycle warning. External hub and
adapter enumeration have passed device-only recovery; this does not qualify
an uninterrupted open serial session.

See [USB host](usb-host.md) for board controls and port mapping, and
[battery telemetry](battery.md) for the separate fuel-gauge interface.
