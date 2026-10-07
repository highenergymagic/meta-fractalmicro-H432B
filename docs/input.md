# Input and power support

## Physically verified inputs

The display has four scroll buttons and 32 cursor-routing keys, one per cell.
Each scroll position below passed two physical press/release pairs, with no
unrelated changes. All four inputs are active-low.

| Scroll position | Scan row | Sense input |
| --- | --- | --- |
| Left upper | GPJ0[2] | GPJ3[5] |
| Left lower | Direct input | GPH0[5] |
| Right upper | GPJ0[2] | GPJ3[6] |
| Right lower | Direct input | GPH0[6] |

All 32 routing keys were tested in cell order, left to right, twice each.
The capture contained 64 complete press/release pairs with no unrelated
input changes. Routing inputs are active-low.

| Cells (leftmost = 1) | Scan row | Sense inputs in cell order |
| --- | --- | --- |
| 1–8 | GPJ0[0] | GPJ2[0..7] |
| 9–16 | GPJ0[0] | GPJ3[0..7] |
| 17–24 | GPJ0[1] | GPJ2[0..7] |
| 25–32 | GPJ0[1] | GPJ3[0..7] |

The right-side lock selector is GPJ1[2..3], expressed as raw bit3:bit2:

| Physical position | Raw value |
| --- | --- |
| Rearmost | 1 (01) |
| Centre | 3 (11) |
| Frontmost | 2 (10) |

Verified from an initial rear position through centre, front, centre, rear.
No matrix, direct-button or front-selector changes accompanied these moves.
This verifies position reporting, not Linux lock enforcement or wake behavior.

The front media-mode selector is GPJ1[0..1], expressed as raw bit1:bit0:

| Physical position | Raw value |
| --- | --- |
| Left | 1 (01) |
| Centre | 3 (11) |
| Right | 2 (10) |

Verified from an initial left position through centre, right, centre, left.
Matrix keys, direct buttons and GPJ1[2..3] were unchanged during this test.

The five front media buttons, ordered left to right as operated, map directly
to GPH2[1], GPH2[2], GPH2[3], GPH2[4], and GPH2[5], respectively. They are
active-low and independent of the matrix scan rows. Two press/release pairs
per button were physically verified with unchanged matrix and selector
readings. This verifies electrical positions, not the final evdev key codes;
functional labels and behavior across selector positions remain to be tested.

All nine Perkins keys share scan row GPJ0[2] and active-low sense inputs.
An ordered physical test captured two complete press/release pairs per key,
with no changes on other rows, direct inputs or selectors. Dot 1 also passed
an earlier independent three-pair test. A separate dots 1+2+3 chord test
captured all three inputs simultaneously on two presses, with clean releases
and no extra changed inputs. Two all-six-dot chords also registered the
expected combined mask with no extra inputs. During one release, dot 6
released about 33 ms before the remaining five dots; both returned fully to
baseline. Chord assembly must accumulate dots until the chord is released,
not require simultaneous key-up events. Larger combinations, cross-row
rollover and comprehensive ghosting tests remain unqualified.

| Physical key | Sense input |
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

The four function keys also use GPJ0[2], active-low. Each was verified with
two complete press/release pairs, without other input changes.

| Physical key | Sense input |
| --- | --- |
| F1 | GPJ3[1] |
| F2 | GPJ3[2] |
| F3 | GPJ3[3] |
| F4 | GPJ3[4] |

The initial scan also has row 2 / GPJ3[7] asserted at rest; its meaning is
unresolved and it must not yet be treated as a mapped key.

The front power switch has been identified as GPH2[6] (EINT22), active-high.
Read-only observation correlated four physical press/release pairs with this
line. It rests low. This does not establish the hardware's long-hold behavior,
power-cutoff mechanism or wake-from-off sequence.

The device tree uses the standard gpio-keys driver and KEY_POWER, with 20 ms
software debounce and no autorepeat or unqualified wakeup declaration.
It retains the inherited pull configuration. Interrupt-driven evdev delivery
was qualified with three physical press/release pairs, each KEY_POWER event
followed by SYN_REPORT, with no unwanted repeats or missing releases.
The development OS deliberately ignores short and long power-key actions:
electrical poweroff and wake behavior have not yet been qualified.

The input-only discovery tool is in tools/power-input/read-power-input.c.
It maps one GPIO register page read-only, checks the pin mux, and reports
transitions for a bounded interval. It neither configures GPIO nor shuts down
the board. Build with the pinned OE build container, not a host cross-compiler.

## Remaining controls

Perkins dots should emit KEY_BRL_DOT1 through KEY_BRL_DOT6, alongside normal
Backspace, Space and Enter events. Chord assembly and braille translation
belong in userspace. Function and media positions are mapped above; final
key codes and event delivery still need implementation and validation.

The two three-position selectors now have captured truth tables. They are
state selectors, not momentary keys; one binary EV_SW code cannot represent
three positions. Their input ABI remains to be chosen. Lock transitions must
release/cancel held chords.

The stock scan circuitry shares GPIO banks with the braille display.
Do not blanket-configure those banks or infer wiring from a reference board.
Final power cutoff is a separate task from recognizing KEY_POWER.

## Build validation

Pinned Yocto build, package QA and offline layer tests passed. The power-key
driver was then tested in a RAM-booted Linux image as described above. This
does not qualify the remaining keys or a complete power-management policy.

The bounded read-power-events.c tool identifies the expected input device,
reads events without grabbing it, and never invokes a power-management action.

## Raw mapping recorder

The opt-in `h432b-input-recorder` recipe builds a bounded GPIO character-device
recorder. It is not installed or started by the normal image. `--self-test`
opens no devices; `--record SECONDS` explicitly enables scanning for 1–180
seconds. GPIO requests are exclusive and refuse already-owned lines.

The stock scan sequence selects GPJ0[2], [1], then [0] low one at a time,
returning each high before the next selection. The recorder reads GPJ2[0..7]
and GPJ3[0..7] for each row, plus direct inputs GPH2[1..5] and GPH0[5..6].
GPJ1[0..1] and GPJ1[2..3] are reported as raw selector pairs.
A pair of matching scans filters unstable samples; only changed states are
printed, with monotonic timestamps. This is discovery data, not a finalized
key map or input ABI.

The recorder never requests GPJ0[3], GPJ1[4..5], or the power-button line
GPH2[6]. It does not inject input events, apply keypad-lock policy, touch the
vibration motor, or write persistent storage. It returns scan outputs high on
normal exit, signal termination and handled failures. Do not run it alongside
another keyboard scanner or while changing the braille display.

## Notification selector and haptics policy

The intended userspace role of the former media-mode selector is notification
mode: silent, vibrate, and sound. Physical positions are mapped above;
position-to-notification policy is not yet implemented. Reporting must preserve all three
positions and initial state; userspace owns notification policy.

The stock vibrator routine drives GPE1[4] high, waits for the requested pulse,
then drives it low. A 300 ms diagnostic pulse was physically confirmed,
with GPIO readback matching low, high, then low. GPE1[4] is therefore a
qualified active-high motor control for that bounded test; broader duty-cycle
limits and production haptics behavior remain unqualified. A future
haptics driver should provide bounded pulses and stop the motor on shutdown
or suspend. No motor output is activated by the input recorder.

The recorder passed its native and on-device logic tests and a two-second
live baseline scan. Perkins keys, function keys, media buttons and both
selectors are physically mapped above. Dots 1+2+3 and all-six-dot chords
also passed. All 32 routing keys and four scroll buttons have verified
single-key mappings. Broader rollover and Linux event delivery remain
unqualified.

## Opt-in Linux input driver

Build `linux-h432b-input-test` to compile the experimental driver and its
device tree. It does not replace the normal kernel recipe or installed NAND
image. Raw GPIO recorders must not run concurrently with this driver.

The driver owns only the mapped GPIO lines, scans every approximately 10 ms
plus scan overhead, and requires two matching samples before reporting.
There is no autorepeat. GPIO read errors and suspend/unbind release held keys;
scan rows return electrically high. It does not declare a wake source.

Three input devices provide a provisional, board-specific interface:

| Device name | Events |
| --- | --- |
| H432B keyboard and controls | Braille dots 1–6, Backspace, Space, Enter, F1–F4 |
| H432B keyboard and controls | Media left-to-right: BTN_0 through BTN_4 |
| H432B keyboard and controls | Scroll left-upper, left-lower, right-upper, right-lower: BTN_TRIGGER_HAPPY1 through 4 |
| H432B braille routing | Cells 1–32: BTN_TRIGGER_HAPPY1 through 32 |
| H432B selectors | ABS_MISC: front selector left=0, centre=1, right=2 |
| H432B selectors | ABS_RZ: side selector rear=0, centre=1, front=2 |

The separate routing device prevents collisions with the scroll event codes.
Media codes identify positions without guessing printed functions. Selector
contact gaps (raw 00) do not generate a fourth state. Clients should query
current state when opening and handle evdev SYN_DROPPED by resynchronizing.
The fixed asserted row-2 bit 15 is excluded from the key map.

Notification modes, keypad-lock filtering and chord translation belong in
userspace. No notification manager or lock enforcement is installed by this
driver. The CIP/RT test kernel and RAM envelope built successfully in the pinned OE
container and booted on hardware. All three devices registered alongside the
existing power switch; kernel taint was zero and no systemd units were failed.
Evdev qualification also captured two dots 1+2+3 chords as KEY_BRL_DOT1,
KEY_BRL_DOT2 and KEY_BRL_DOT3, with one SYN_REPORT for each chord press
and release. Routing cells 1 and 32 each produced two clean press/release
pairs on the separate routing device. There were no extra or repeat events
in that capture. The front selector also delivered ABS_MISC values 1, 2,
1, 0 for centre, right, centre, left, each followed by SYN_REPORT, with no
key events or side-selector changes. The side selector delivered ABS_RZ
values 1, 2, 1, 0 for centre, front, centre, rear, each with SYN_REPORT and
no unrelated events. Both selectors therefore have qualified position-event
delivery. This is not a full evdev regression of every key or qualification
of userspace lock/notification policy; the ABI remains provisional.

## Bounded motor diagnostic

The opt-in `h432b-vibrator-test` recipe builds a development-only GPIO tool.
It verifies the board compatible, requests only GPE1[4] exclusively, starts
low, and requires an explicit `--pulse-100ms` or `--pulse-300ms` argument.
It reads back the output before, during and after the pulse.
Normal completion, handled errors, SIGINT and SIGTERM return the output low.
It is not started automatically or included in the normal image.

The pulse uses userspace timing, not a hardware watchdog: scheduler stalls,
SIGKILL or a kernel crash can prevent timely cleanup. Do not use this as the
production haptics service. The first 100 ms test returned successfully but produced no perceptible
vibration. A later 300 ms test, with pull disabled and output readback,
produced a physically confirmed vibration and returned low successfully.
Because both pulse length and pull configuration changed, this does not
establish a minimum effective duration. The eventual haptics interface must
provide kernel-managed timed effects.
