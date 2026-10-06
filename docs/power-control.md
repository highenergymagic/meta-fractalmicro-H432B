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

## Opt-in PMIC inventory

The separate linux-h432b-power-test recipe adds an i2c-gpio adapter on
GPD1[4] (SDA) and GPD1[5] (SCL), using open-drain operation. It does not
instantiate a regulator driver, enable suspend, or change the default DTS.

h432b-power-inventory verifies the adapter identity, then reads only control
registers 0 and 1 at seven-bit address 0x66. The combined I2C transactions
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
