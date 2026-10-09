# Input and haptics

## Availability

The normal NAND kernel includes KEY_POWER, the keyboard, routing keys and
selectors. The `linux-h432b-input-test` profile uses the same shared driver and
wiring. The evdev ABI remains provisional. BRLTTY provides braille chord translation;
keypad locking and notification policy are not implemented. The normal systembase includes
a bounded vibration command, not an automatic haptics service.

Raw GPIO recorders must not run alongside the input driver or while changing
the braille display. The scan circuitry shares GPIO banks with the display;
do not configure whole banks.

## Power switch

GPH2[6]/EINT22 is active high and rests low. gpio-keys reports KEY_POWER
with 20 ms debounce and no autorepeat. Press/release delivery is verified.
The normal OS policy requests deep suspend; another power press wakes the
same session. Other input devices are not wake sources. Electrical poweroff
and long-hold hardware behavior remain unqualified.
See [power management](power-control.md).

## Keyboard matrix

Rows GPJ0[2], [1] and [0] are selected low one at a time and returned high
between selections. Sense inputs are active low.

| Key on row GPJ0[2] | Sense input |
| --- | --- |
| Dot 7 / Backspace position | GPJ2[0] |
| Dot 3 | GPJ2[1] |
| Dot 2 | GPJ2[2] |
| Dot 1 | GPJ2[3] |
| Space | GPJ2[4] |
| Dot 4 | GPJ2[5] |
| Dot 5 | GPJ2[6] |
| Dot 6 | GPJ2[7] |
| Dot 8 / Enter position | GPJ3[0] |
| F1 / F2 / F3 / F4 | GPJ3[1] / [2] / [3] / [4] |

Row 2 / GPJ3[7] is asserted at rest with unresolved meaning; it is excluded
from the key map. Dots 1+2+3 and all-six-dot chords have passed raw mapping
tests. Key releases need not be simultaneous: userspace chord assembly must
accumulate the chord until release, not require simultaneous key-up events.
Cross-row rollover and comprehensive ghosting are unqualified.

## Display controls

| Scroll position | Row | Sense |
| --- | --- | --- |
| Left upper | GPJ0[2] | GPJ3[5] |
| Left lower | Direct | GPH0[5] |
| Right upper | GPJ0[2] | GPJ3[6] |
| Right lower | Direct | GPH0[6] |

| Routing cells, left to right | Row | Sense inputs |
| --- | --- | --- |
| 1–8 | GPJ0[0] | GPJ2[0..7] |
| 9–16 | GPJ0[0] | GPJ3[0..7] |
| 17–24 | GPJ0[1] | GPJ2[0..7] |
| 25–32 | GPJ0[1] | GPJ3[0..7] |

All 32 routing keys and four scroll positions have verified single-key
electrical mappings. This is not full evdev or rollover qualification.

## Media buttons and selectors

Five media buttons, left to right, map to GPH2[1..5], active low and
independent of scan rows. The front selector uses GPJ1[0..1]; the side lock
selector uses GPJ1[2..3].

| Selector | Position | Raw pair, high bit first |
| --- | --- | --- |
| Front | Left / centre / right | 01 / 11 / 10 |
| Side | Rear / centre / front | 01 / 11 / 10 |

Raw 00 is a contact gap, not a fourth mode. These are state selectors rather
than momentary keys. Front-selector notification roles (silent, vibrate,
sound) belong to future userspace policy; position-to-role assignment is not
implemented. Lock transitions must release or cancel held chords.

## Provisional Linux ABI

The input driver scans at approximately 10 ms plus scan overhead and
requires two matching samples. There is no autorepeat. GPIO errors and
suspend/unbind release held keys and return rows high. No wake source is declared.

| Input device | Events |
| --- | --- |
| H432B keyboard and controls | KEY_BRL_DOT1–6, Backspace, Space, Enter, F1–F4 |
| H432B keyboard and controls | Media left-to-right: BTN_0–4 |
| H432B keyboard and controls | Scroll left-upper, left-lower, right-upper, right-lower: BTN_TRIGGER_HAPPY1–4 |
| H432B braille routing | Cells 1–32: BTN_TRIGGER_HAPPY1–32 |
| H432B selectors | ABS_MISC: front left=0, centre=1, right=2 |
| H432B selectors | ABS_RZ: side rear=0, centre=1, front=2 |

Routing uses a separate device to avoid scroll-code collisions. Applications
must query initial state and resynchronize after SYN_DROPPED.

RAM-profile qualification covers registration of all three devices, dots
1+2+3 chord events, routing cells 1 and 32, and both selector event sequences.
It does not qualify every key through evdev or a complete accessibility stack.

## Mapping tools

`h432b-input-recorder --self-test` opens no hardware.
`--record SECONDS` scans for 1–180 seconds using exclusive GPIO requests.
It reports stable changes with monotonic timestamps, returns rows high on
handled exits and refuses already-owned lines. It does not emit input events,
enforce locks, operate the motor or write storage.

The recorder excludes GPJ0[3], GPJ1[4..5] and GPH2[6].
Power-switch observation is available separately through
`tools/power-input/read-power-input.c` and the bounded
`read-power-events.c` evdev reader. Build target tools through pinned OE.

## Vibration

GPE1[4] is the active-high motor control. The
`h432b-vibrator-test` validates the board, claims only that line, starts low
and accepts `--pulse-100ms` or `--pulse-300ms`. It reports output readback
and returns low on completion, handled errors, SIGINT and SIGTERM.
It is installed in the normal systembase but never started automatically.

A 300 ms pulse with pulls disabled was physically confirmed. A 100 ms
test was not perceptible, but pulse duration and pull configuration differed,
so no minimum effective duration is established.

Userspace timing is not a hardware watchdog: scheduler stalls, SIGKILL or a
kernel crash can prevent cleanup. Production haptics requires kernel-managed
timed effects, duty-cycle limits and shutdown/suspend handling.
