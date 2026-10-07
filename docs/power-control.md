# Power control

## Supported input

The front spring-loaded switch is GPH2[6], active high. The board device
tree exposes it through gpio-keys as KEY_POWER with 20 ms debounce.
Input events have been tested on hardware. The normal keypad lock must
not suppress the power switch.

The intended button behavior is stock-like suspend/resume, including removal
of braille-cell drive power during sleep. It is not a cold shutdown request.
Input support does not imply electrical shutdown or wake support.
Until those are qualified, the development OS ignores power-key actions.

## Shutdown and wake status

Stock firmware uses a suspend/resume path: it saves CPU and peripheral
state, enables EINT22 as a wake source, and programs PMIC controls.
The retained factory first-stage loader jumps to physical 0x40020000 on
wake, rather than consuming the upstream Linux INFORM0 resume pointer.
A compatible trampoline needs reserved memory: this address currently lies
in Linux's decompressed image. Do not overwrite it in the running kernel.
Do not enable Linux suspend without validating the retained factory loader
and Linux resume entry together.

GPA0[6] occurs in a braille-cell power-down routine. It is not an established
whole-board cutoff signal. No board poweroff handler is currently qualified.

## PMIC inventory

The runtime kernel and separate linux-h432b-power-test recipe share an
i2c-gpio adapter on GPD1[4] (SDA) and GPD1[5] (SCL), using open-drain
operation. Neither instantiates a regulator driver or enables suspend.

h432b-power-inventory verifies the adapter identity, then reads control
registers 0 and 1 at seven-bit address 0x66. Explicit --dvs additionally
reads voltage-slot registers 4, 5 and 6. The combined I2C transactions
write a register pointer followed by a one-byte read; they contain no
register-value writes. The tool performs no address scan.

The OS layer's openh432-power-test image is intended for RAM launch and
uses the installed slot-B NAND root through the minimal early handoff.
Building it neither deploys it nor changes NAND. Exact PMIC identification,
electrical cutoff, button wake, and regulator constraints remain unqualified.

## Validation

The inventory kernel and helper built with the pinned OE toolchain. Five
scope-regression tests passed. A RAM launch using the installed NAND root
successfully read control registers 0/1 as 0x3e/0xf1, matching the stock
resume state. Linux remained healthy, UBI read-only and kernel untainted.
This qualifies the bus/read path only, not regulator programming or suspend.

## Reserved resume bridge experiment

The separate `linux-h432b-resume-test` kernel excludes the bottom 2 MiB
from Linux RAM. Its chosen usable-memory range is 0x40200000–0x4fffffff; the ARM
decompressor uses that start and places the kernel at 0x40208000.
A reserved page at the factory wake entry, 0x40020000, holds a 12-byte
ARM trampoline which loads the resume target from INFORM0. LDR into PC
preserves the target's ARM/Thumb interworking semantics; it does not assume
the Linux kernel is built in ARM state.

The physical memory bank remains accurately described as 256 MiB. The
chosen usable-memory limit is deliberate: the retained U-Boot rewrites the
memory node to the full bank, but preserves this limit. Both decompressor
and early OF memory scanning honor it.

The bridge checks the board opt-in, memory boundary and instruction readback
before registering suspend operations. It does not modify the factory loader,
PMIC settings or braille rail. The normal board image is unchanged.
`openh432-resume-test` is a RAM-launch bundle using the installed slot-B root.

Build success and layout checks are not suspend qualification. First verify
normal boot at the relocated address, then use the kernel's PM test stages,
which return without entering low power. A real suspend test requires a
working wake input and observation of the returning USB console.

The first RAM layout test passed: Linux reported RAM beginning at 0x40200000,
kernel code at 0x40208000, and successful bridge instruction readback.
The power-key wake source was enabled; UBI remained read-only and the kernel
untainted. The subsequent `pm_test=devices` run did not return a responsive
USB console within 50 seconds. Its last visible message was console
suspension, after the task freezer completed. This is not proof of CPU sleep
or a failed wake trampoline: the devices test does not enter low power.
Later logs showed repeated mmc1 SDIO CMD5 timeouts and eventual PM suspend
exit approximately 88 seconds after entry. Thus the initial console timeout
was not evidence of a permanent hang. Post-test shell health was not verified
before Reset. The SDIO resume path needs isolation before a real sleep test.
Real suspend and automatic power-button policy are not qualified or enabled.

## Device-level PM isolation

Further RAM-only tests returned to a responsive USB console:

| Configuration | Result |
| --- | --- |
| Task freezer only | Returned in approximately 5 seconds |
| Wi-Fi SDIO controller detached, sequential callbacks | Two returns in approximately 5.27 seconds |
| Wi-Fi SDIO attached, sequential callbacks | Two returns in approximately 5.71 seconds |
| Wi-Fi SDIO attached, asynchronous callbacks | Returned in approximately 5.57 seconds |

These times include the kernel's five-second PM test delay. The attached
tests retained SDIO device 024c:8712 afterward. Post-test checks found an
untainted kernel, read-only UBI, and no failed systemd units. This is SDIO
enumeration/resume evidence, not a working Wi-Fi network driver.

The earlier SDIO command timeouts were not reproduced. Neither Wi-Fi presence
nor asynchronous callbacks alone is established as their cause. A separate
unbind/rebind attempt failed with an unexpected pinctrl owner for GPG3[3];
forcing that pin is not part of the test procedure.

USB gadget suspend disconnects and re-enumerates the console. A host reader
holding the old tty must reopen the new endpoint before assessing health.
Every tested device-resume cycle also logged a USB host PHY power-on-before-init
warning; external USB host operation after resume remains unqualified.

`tests/check-pm-devices.sh` provides explicit freezer and device-only modes.
It verifies board identity, the reserved-layout opt-in, read-only UBI, the
physical SDIO controller and device identity, and the selected PM test level
before requesting a transition. It restores PM diagnostic settings afterward.
It does not request real CPU sleep or enable power-key policy. Full sleep,
factory-loader wake, and braille power sequencing remain unqualified.

### USB host PHY lifecycle experiment

The opt-in resume kernel carries `0011-usb-host-phy-lifecycle.patch`.
Both Exynos host helpers previously called `phy_power_on()` without their
own `phy_init()` reference. The patch pairs initialization with power-on,
and power-off with exit, including partial-failure rollback. Generic USB
root-hub references remain independent. The Samsung provider has no separate
init callback, so correcting this API imbalance does not itself establish
a missing electrical initialization step.

The patched RAM image built successfully and returned from two asynchronous
device-level PM tests without the previous PHY lifecycle warning. USB gadget
console recovery, SDIO identity, read-only UBI, untainted kernel and absence
of failed services were checked after each return. The patch remains confined
to the experiment; no installed firmware was changed.

A serial adapter connected to the front-left external port did not enumerate
on either the baseline or patched kernel. Keeping baseline root hubs runtime
active did not change this. External port power, enumeration and data transfer
remain unqualified; no claim of functional USB host peripherals follows from
the lifecycle fix.

### Board USB enables

Stock board controls identify GPH2[7] as HUB, GPH3[7] as USB12,
and GPH1[3] as USB3, all enabled high. The names do not yet establish
which physical sockets belong to each control. The stock hub-on path prints a debug message before setting the GPIO.
An earlier interpretation of that call as event signalling was incorrect;
the import resolves to `NKDbgPrintfW`, and its argument is a UTF-16 log string.

The explicit `h432b-usb-power-test` diagnostic reads the pin configuration
through a read-only mapping and claims only these three GPIO lines through
the kernel API. It requires output muxes and unclaimed lines, captures their
initial values, then optionally enables them for 20 seconds and restores
their original values in reverse order. It has no PMIC, system-5V, display
or storage writes. It is a manual diagnostic, not an installed power service.

On the tested RAM kernel, all three pins had output mux 1 and were initially
low. Two enable tests verified all three high, followed by restoration to low.
One test additionally kept both USB root hubs runtime-active. Neither produced
a downstream USB connection event with a serial adapter in the back-panel port.
This qualifies GPIO control/readback only, not electrical port power.
Earlier adapter checks also found no connection on either left-side port.

The Samsung GPIO driver lacks a direction-query callback. Consequently its
initial GPIO character-device direction flags cannot establish the inherited
hardware direction; the diagnostic checks the raw mux before claiming a line.

### Stock initialization comparison

The stock board initializer configures all three USB-enable pins as outputs
and disables their pull resistors. Live snapshots of the experimental Linux
boot showed output muxes but pull-downs still selected. The existing bounded
enable test preserved those pulls. This is an identified initialization
difference, not a demonstrated cause of failed enumeration.

The stock EHCI initialization separately enables host clocks and PHY isolation,
writes PHY power/clock/reset, waits, then releases reset and waits again.
It is distinct from board GPIO enable. Linux root-hub registration and removal
of a PHY API warning do not establish that the external hub, supply and port
signals are correctly initialized. Further comparison must include the
upstream supply state and controller port-status observations.

A subsequent stock-bias test verified all three enables high with pulls
disabled for 20 seconds, then restored the original low levels and pull-downs.
The upstream SYSTEM5V GPIO, GPE1[1], was already an output high and was only
read. EHCI PORTSC remained 0x1000 and OHCI root-port status remained 0x100:
port-power bits set, connection bits clear. No downstream connection event
occurred with the adapter in the back-panel port. Neither the pull setting
nor the three enables alone explains the missing connection. Register
readback does not establish measured socket voltage.

The helper's `--stock-bias-20s` mode performs this bounded bias test.
Its `--read` mode only maps GPIO, PHY and host status registers read-only;
it does not claim or configure GPIOs. Host status reads are taken with both
root hubs runtime-active.

### External hub reset identified and enumeration observed

Stock selector action 0x0D includes a separate hub reset: GPJ4[2] low,
100 ms delay, then high. This is distinct from GPH2[7] hub power and from
the braille latch on GPJ4[1]. The initial Linux snapshot showed GPJ4[2]
configured as an output held low.

With the three USB enables high, pulls disabled, and this reset pulse
performed, Linux enumerated a high-speed four-port hub, VID:PID 0409:005a,
followed by a full-speed PL2303 adapter, 067b:2303. The back-panel socket
was observed on downstream hub port 2. The frontmost left-side socket
was subsequently qualified on downstream hub port 4 with kernel-owned
power/reset and PL2303 binding. The rearward left-side socket was likewise
qualified on downstream hub port 3. All three external sockets therefore
pass enumeration and PL2303 binding; hub port 1 has not been identified.

The explicit `--hub-reset-20s` diagnostic mode restores reset, enables and
bias settings after the test; the resulting disconnect is expected.
Enumeration qualifies descriptor/control transfers, not serial TX/RX.
That diagnostic image did not include the PL2303 driver. Kernel-owned
power/reset and serial-driver support are described below; the diagnostic
is not a boot service.

### Experimental kernel-owned USB support

The resume-test kernel includes an opt-in USB host DTS fragment and a small
onboard-device ID-table patch for 0409:005a. The standard
`onboard-usb-dev` driver owns hub power through a fixed GPIO-controlled
regulator, and holds the active-low GPJ4[2] reset for 100 ms before release.
The delay follows the board's qualified stock sequence, not a documented
silicon minimum. No PMIC or SYSTEM5V control is introduced.

USB12 and USB3 enables are represented separately as always-on fixed
regulators for initial qualification. Their physical socket grouping and
selective power-saving policy are not yet established. No unmeasured regulator
voltage is specified. All four control pins have disabled pull bias. The regulator pins have
explicit output mux; the reset GPIO consumer sets its direction. Its shared
platform/USB OF node uses bias-only pinctrl to avoid duplicate mux ownership. PL2303 support is built into the experimental kernel.

`tests/check-usb-hub.sh` checks the hub and known adapter identities under the
physical EHCI controller, speeds, four-port topology and driver bindings.
It does not open the serial device or claim serial TX/RX qualification.
Device-only PM tests do not exercise the onboard driver's late-suspend
power-off callback or establish full CPU sleep/wake support.

### Kernel-owned hub qualification

The opt-in resume-test image has passed automatic hub enumeration and
PL2303 driver binding with the adapter in the back-panel socket. No userspace
GPIO helper was involved. Two guarded `pm_test=devices` cycles with SDIO
still attached returned successfully; the hub and adapter were present and
bound afterward. Suspend statistics recorded two successes and zero failures,
the kernel remained untainted, and UBI remained read-only.

The adapter disconnected and re-enumerated following the first device-resume
cycle. This qualifies device recovery, not preservation of an open serial
session. Serial TX/RX, actual CPU sleep,
and late-suspend hub power-off remain unqualified. These changes are still
restricted to the RAM diagnostic image, not the default NAND image.

## CPU-supply investigation

The factory diagnostic menu names MAX8698. Its normal initialization uses
the established 0x66 control bus and programs DVS register bytes 0x04=0x79,
0x05=0x57 and 0x06=0x57. It then configures GPH1[6:7] as outputs with
values 1/0 and GPH0[4] as an output with value 0. These are observations of
factory code, not Linux regulator settings or measured rail voltages.

The [MAX8698C datasheet](https://atta.szlcsc.com/upload/public/pdf/source/20210518/C2682647_D87189DADB797D327BCE49EA88D19166.pdf)
describes multiple programmed ARM/internal-supply voltage slots selected
by SET pins. The --dvs inventory reads the programmed voltage slots;
the default mode reads enable controls only. Establish the board's SET-pin
mapping before reporting an active voltage or implementing frequency
transitions. Do not bind the similarly named MAX8998 driver:
its register map is different.

EBOOT's menu also offers CPU clock profiles including 1 GHz, but that
clock-switch routine does not establish a matching voltage transition.
No Linux CPU-voltage changes or 1 GHz qualification have been performed.

The runtime NAND kernel has passed two live DVS reads: registers 0/1 were
0x3e/0xf1 and 4/5/6 were 0x79/0x57/0x57. Read-only GPIO inspection confirmed
GPH1[6]=1, GPH1[7]=0 and GPH0[4]=0, all configured as outputs. These match
factory initialization but do not establish the physical SET-pin wiring
or measured rail voltages. No PMIC register-value writes were performed.
The kernel booted normally from NAND; volume hashes verified, with no
failed units, NAND ECC errors or kernel taint. CPU frequency remains 800 MHz.
