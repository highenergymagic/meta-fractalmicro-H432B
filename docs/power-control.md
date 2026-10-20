# Power management

## Availability

The normal NAND runtime supports power-button deep suspend/resume and
[wakeable clean shutdown](#wakeable-shutdown). Suspend removes braille-cell
drive power; wake restores the cached display and the same interactive
session. After clean shutdown, power instead requests a fresh boot. Neither
path electrically isolates the battery. The power switch is the only enabled
wake source; other keys and both three-position selectors have been tested
without waking a suspended device.

Peripheral recovery is qualified separately from core suspend:

| Interface | Verified after deep sleep | Remaining limits |
| --- | --- | --- |
| Ethernet | DHCP, SSH and checksum-verified bidirectional transfers | Interface resets/reopens; uninterrupted sessions and endurance unqualified |
| Wi-Fi | Automatic WPA2 station reassociation and interface-bound ICMP | Sustained transfers and repeated-cycle endurance unqualified |
| Bluetooth | Provisioned identity, radio parameters and HCI commands retained | Manual initialization required; connected-peer retention unqualified |
| Audio | Playback started after resume, stream-clock release and OpenEVV console speech restored | Open streams across suspend and repeated-cycle endurance unqualified |
| GPS | Checksum-valid NMEA reception | Assistance retention and acquisition performance unqualified |
| FM | Muted reopening and tuning | Reception and open handles across sleep unqualified |
| USB | Gadget reconnection, hub/adapter re-enumeration and empty-port reboot after wake | Open serial sessions and payload continuity unqualified |
| RTC | Elapsed sleep-time accounting with network synchronization stopped | Battery-removal retention and long-term accuracy unqualified |

These checks cover specific images and individual device cycles, not a combined
peripheral endurance test. See the
[validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md#power-button-deep-suspend-qualification)
for artifacts and methods.

The factory user-visible off/on behavior is suspend/resume, including removal
of braille-cell drive power, rather than a cold shutdown. The display driver
controls its supply through GPJ0[3]; this is not a whole-board power cutoff.
Keypad locking must not suppress the independent power switch.

## Power switch and resume contract

The spring-loaded switch is GPH2[6]/EINT22, active high. The device tree uses
gpio-keys, KEY_POWER and 20 ms debounce. Press/release events and sustained
holds through the shutdown countdown have been hardware-tested.

Factory wake enters physical 0x40020000 rather than the upstream Linux
INFORM0 resume pointer. Suspend-enabled runtime builds exclude the bottom
2 MiB of RAM and decompress at 0x40208000, keeping the fixed wake entry outside
Linux-managed memory. The bridge refuses incompatible memory layouts and
requires EINT22 to be the sole enabled external wake source.

## Wakeable shutdown

The runtime device tree enables the board-specific `hims,wakeable-poweroff`
path. Clean software shutdown followed by power-button fresh startup has
passed a hardware cycle, as has ordinary suspend/resume on the same kernel.
Repeated-cycle endurance and shutdown current remain unqualified.

The final kernel power-off handler runs after normal service, filesystem and
device shutdown. It waits for a stable release of the power switch, masks all
wake sources except EINT22, and enters retained-memory sleep. The next power
press dispatches through the factory resume bridge to a stackless hardware
reset routine, requesting a fresh boot rather than restoring the old session.
Ordinary suspend retains its separate resume target and session semantics.

This is soft-off, not electrical battery isolation. Retained memory and wake
logic require power; shutdown current and peripheral rail reduction have not
been measured. It does not program PMIC voltage/enable registers, replace
factory firmware, enable RTC alarm wake, or write an additional NAND flag.
A held shutdown gesture delays sleep until release. Failure to enter sleep
leaves the device halted, requiring Reset; this is not a wakeable fallback.

## Beeper

The normal runtime describes the passive beeper on GPD0[0]/TOUT0 using the
upstream `pwm-beeper` driver. PWM channel 0 is reserved as an output; the
system timer channels remain separate. The input device is named `pwm-beeper`
and accepts `EV_SND/SND_BELL` (default 1 kHz) and `EV_SND/SND_TONE`.
The driver stops output on close and during suspend. No ALSA stream or audio
codec is involved. Warning tones, cancellation and the complete shutdown
countdown have been verified on hardware. Explicitly disabling the Samsung
PWM channel also silences any waveform inherited from firmware before normal
runtime use. Quiet startup after this handoff has been verified; repeated-cycle
endurance remains unqualified.

Button gesture timing belongs to the OS layer's `FMPowerKey.service`, not the
kernel driver. Applications should not compete with that service for tones.

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

The default CPU frequency is 800 MHz. Factory menu clock profiles do not establish a
qualified Linux DVFS sequence. A voltage-aware implementation must coordinate
regulators, PLLs, dividers and DRAM refresh. Readback of programmed slots is
not measurement of the active voltage.
See the [MAX8698C data sheet](https://atta.szlcsc.com/upload/public/pdf/source/20210518/C2682647_D87189DADB797D327BCE49EA88D19166.pdf).

### Stock sleep policy and regulator integration

The recovered stock kernel writes ONOFF1/ONOFF2 as `0x32/0x11` before
sleep and restores `0x3e/0xf1` after wake. Under the MAX8698C register map,
the changed bits disable LDO3, LDO4, LDO6, LDO7 and LDO8. BUCK3, LDO2,
LDO5, LDO9 and battery-monitor enable bits are retained. BUCK1/BUCK2 have
their software enable bits clear in both states; their effective enable
also depends on PWREN. A zero software enable bit is not proof of a dead rail.

Linux does not currently reproduce these PMIC writes. Their decoding identifies
candidate sleep-power work, not measured savings or a qualified consumer map.
Establish each output's board consumers and restore ordering before enabling
regulator suspend control. Do not substitute the MAX8998 register map.

CPUIdle remains disabled; the ARM idle fallback executes WFI rather than
busy-looping. Deeper idle states and voltage-aware frequency scaling are
separate integration tasks. The upstream S5PV210 CPUFreq implementation requires real
`vddarm`/`vddint` regulators and updates DRAM refresh with clock transitions;
enabling a governor alone does not supply that hardware contract.

### Clock-transition prerequisites

The observed board configuration is not the reference driver's uniform mobile
DRAM configuration. DMC0 `MEMCONTROL` is `0x00202100` (LPDDR type field), while
DMC1 is `0x00202412` (DDR2 type field). The pinned CPUFreq driver checks the
type of DMC0 only, but can alter refresh counters for both controllers.
Passing that check alone does not qualify its transitions on H432B.

The observed boot state has APLL at 800 MHz, `CLK_DIV0=0x14131330` and the
main-system bus at 200 MHz. The reference 400 MHz divider settings preserve
that bus rate. Its 200 MHz entry and accompanying bus-rate comment disagree
under the pinned clock-provider model; 100 MHz changes bus clocks, and 1 GHz
requires a PLL transition. None of these operating-point changes has been
qualified by the upstream driver's presence alone. The fixed-bus driver
isolates 800/400 MHz from voltage and memory-bus changes.

The pinned kernel has no MAX8698 regulator implementation. An older Samsung
[MAX8698 driver](https://github.com/CyanogenMod/android_kernel_samsung_smdk4412/blob/7f0b2fbac08158a192322fcb6183593071387a71/drivers/regulator/max8698.c)
provides a GPL-licensed register/selector reference, not a drop-in driver for
the current regulator and GPIO APIs. Its probe changes DVS slots and GPIO
levels; those defaults must not be imported without adapting them to the
board's established boot state.

`h432b-clock-inventory` is an optional, explicitly invoked diagnostic target.
Running `h432b-clock-inventory --read` reads only the fixed clock, DMC and
selector-GPIO words and verifies the board compatible first. It neither
requests a frequency nor writes a register. Pair its output with
`h432b-power-inventory --dvs`; register values alone are not measured voltages.

### Fixed-supply CPU frequency control

The runtime builds `s5pv210-fixed`, a CPUFreq driver exposing 400 and 800 MHz.
It changes the ARM and HCLK_MSYS dividers together, retaining a 200 MHz
main-system bus. APLL, PMIC settings and DRAM refresh registers are untouched.
It requires the firmware's 800 MHz configuration and checks the clock source,
divider and ARM memory-controller settings before allowing transitions.

The default governor is `performance`: installing the driver does not enable
automatic downclocking. `userspace` and `ondemand` are available for
qualification. Read and select frequencies through the standard interface:

```sh
policy=/sys/devices/system/cpu/cpufreq/policy0
cat "$policy/scaling_available_frequencies"
echo userspace > "$policy/scaling_governor"
echo 400000 > "$policy/scaling_setspeed"
cat "$policy/cpuinfo_cur_freq"
echo performance > "$policy/scaling_governor"
```

The driver restores 800 MHz before suspend, reboot or shutdown, regardless of
the userspace maximum-frequency limit. Resume validates the restored clock
configuration before allowing governor requests again. Polling is bounded;
transition failures attempt rollback and disable further changes.

Hardware checks cover 202 frequency transitions, test-data integrity,
speech and braille at 400 MHz, and deep suspend/resume with the userspace
maximum capped at 400 MHz. Automatic scaling with a 50 ms sampling interval
returns to 800 MHz under load and spends most of a quiet interval at 400 MHz.
These checks do not establish long-duration stability or battery-life gains.

A short battery-only comparison did not resolve an idle-power reduction
against the surrounding fixed-frequency measurements. Idle already uses WFI,
and the supply voltage remains unchanged. The default remains `performance`.
Do not enable lower voltages or additional frequency points using this driver.

### Measuring consumption

Measure idle consumption on battery with both AC and USB power disconnected.
Keep peripheral state and workload fixed between comparisons. The gauge's
current while externally powered is net battery charge/discharge current,
not total board demand. Preserve signed current, voltage, uptime and connection
state; discard transition samples and account for the gauge's averaging delay.

The runtime gauge stops polling during suspend. A missing sample or the first
post-wake current reading cannot establish sleep current. Sleep and soft-off
consumption need a validated charge-accumulator delta over a timed interval,
or an external measurement. Battery percentage alone is too coarse for short
sleep tests. Do not infer battery life from the PMIC register values.

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
