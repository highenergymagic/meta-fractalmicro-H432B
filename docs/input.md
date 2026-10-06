# Input and power support

The front power switch has been identified as GPH2[6] (EINT22), active-high.
Read-only observation correlated four physical press/release pairs with this
line. It rests low. This does not establish the hardware's long-hold behavior,
power-cutoff mechanism or wake-from-off sequence.

The device tree uses the standard gpio-keys driver and KEY_POWER, with 20 ms
software debounce and no autorepeat or unqualified wakeup declaration.
It retains the inherited pull configuration. Interrupt-driven evdev delivery
still requires qualification on hardware. The matching development OS policy
ignores power-key actions until that test is complete.

The input-only discovery tool is in tools/power-input/read-power-input.c.
It maps one GPIO register page read-only, checks the pin mux, and reports
transitions for a bounded interval. It neither configures GPIO nor shuts down
the board. Build with the pinned Arm GNU container, not a host cross-compiler.

## Remaining controls

Perkins dots should emit KEY_BRL_DOT1 through KEY_BRL_DOT6, alongside normal
Backspace, Space and Enter events. Chord assembly and braille translation
belong in userspace. Function and media keys need individual physical mapping.

The two three-position selectors need truth-table capture before an ABI is
chosen. They are state selectors, not momentary keys; EV_SW alone cannot
represent three states. Lock transitions must release/cancel held chords.

The stock scan circuitry shares GPIO banks with the braille display.
Do not blanket-configure those banks or infer wiring from a reference board.
Final power cutoff is a separate task from recognizing KEY_POWER.

## Build validation

Pinned Yocto build and package QA passed. This source has not yet been
deployed for live event/sound qualification. Offline layer tests also pass.
