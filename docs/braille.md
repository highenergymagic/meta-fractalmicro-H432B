# Internal braille display

## Availability

The runtime kernel includes the H432B 32-cell display transport. BRLTTY 6.9.1
is pinned and built with an internal-display backend (`h4`); the normal
systembase packages it through the OS layer. Linux frame output, a readable
Grade 2 greeting and eight-dot patterns have been checked on one U2.
The standard systembase enables BRLTTY at startup. Console output, braille
key entry, Backspace, Enter, scrolling and cursor routing have been confirmed
by the operator, along with the BRLTTY chords tried, including learn mode.
Suspend removes cell drive power; resume restores the cached frame and console
session. Exhaustive chord/routing-key coverage remains unqualified.

This is not the existing HIMS USB/Bluetooth external-display protocol.
It uses Linux GPIO and evdev interfaces on the Sense itself.

## Hardware and ownership

| Signal | GPIO |
| --- | --- |
| Data | GPJ1[5] |
| Clock | GPJ1[4] |
| Latch | GPJ4[1] |
| Inherited display enable | GPJ0[3] |

The driver claims individual GPIO descriptors. Keyboard scanning uses other
pins in the same banks; no whole-bank register writes are permitted. Frame
writes are serialized. The transport sends cells in reverse order, most
significant bit first, with the recovered alternating polarity convention.

Enable state must already be active and configured as output by the retained
boot chain. A Samsung GPIO readback patch exposes the hardware direction
without rewriting the inherited output. During suspend, the driver shifts a
neutral frame, waits 100 ms and drives
the enable low. Resume drives it high, waits 100 ms and restores the cached
frame. Tactile testing confirmed cell power removal and restoration. This is
not a PMIC transaction or whole-board poweroff; blanking alone is not rail removal.

Kernel shutdown, including reboot, also neutralizes the cells, waits 100 ms
and lowers the supply before closing the transport. A subsequent boot relies
on the retained factory boot chain to restore the supply. The runtime's
wakeable shutdown path allows the power switch to request a fresh boot after
clean shutdown; it does not restore the previous session or electrically
isolate the battery. See [power management](power-control.md#wakeable-shutdown)
and the [OS gesture policy](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/runtime.md#suspend-policy).

The standard eight-dot mapping is implemented in the kernel. Qualification
combined the established six-dot greeting, a dot-7 left-column extension,
and an operator-confirmed full eight-dot cell.

## Kernel interface

`/dev/h432b-braille` is a root-only character device with one exclusive
open owner. Each write must contain exactly 32 bytes. Bit 0 denotes dot 1,
through bit 7 for dot 8. Translation, contractions and cursor rendering
belong in userspace; these bytes are dot masks, not text or Braille ASCII.

An invalid frame length returns EINVAL without updating the display.
A competing open returns EBUSY. Writes fail if the inherited enable is low
or the device has been removed. Close leaves the last displayed frame intact.
There is no hardware cell-position readback; a successful write confirms
that the GPIO sequence completed, not that every physical dot moved.

## BRLTTY backend

The backend sends rendered cells to the character device and reads two
kernel evdev devices: `H432B keyboard and controls` and
`H432B braille routing`. It takes exclusive evdev grabs while running,
releasing them when stopped. Selector and power-key devices are not grabbed.

HIMS key names and upstream key tables provide familiar routing, scrolling,
braille chords and F1–F4 bindings. Backspace/Enter positions are Dot7/Dot8
within BRLTTY's braille keyboard interpretation. Media1–Media5 are named but
have no default action. Selector notification and keypad-lock policy remain
separate work.

Events are applied at SYN_REPORT boundaries. Overflow recovery uses
EVIOCGKEY after SYN_DROPPED; disconnect or malformed reads request a backend
restart. The physical display's dot ordering is handled only by the kernel,
not duplicated in BRLTTY.

## Explicit checks

Build `h432b-braille-check` for a standalone operator tool:

- `--abi-check`: checks exclusive open and rejection of a short frame.
- `--greeting`: displays uncontracted “Linux braille”.
- `--bottom-dots`: requests dot 7 in cell 1 and dot 8 in cell 3.
- `--blank`: writes a blank frame without cutting display power.

Stop BRLTTY before using the tool. No command runs automatically on boot.
A build or successful device write is not tactile qualification.

See the [OS service policy](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/braille.md)
and [validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md).
