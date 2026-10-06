# Boot performance investigation

The qualified development NAND boot is about 138 seconds end to end, including
roughly 29 seconds in Linux/systemd. A sub-30-second complete boot is a target, not
a current result.

A RAM-only profiling build preserves the installed Linux volumes and all NAND
write guards. With the same 15,509,504-byte kernel bundle, host-timed complete
UBI attach/read/validation/CRC commands measured:

- Caches disabled: 95.823 seconds.
- Instruction cache enabled, data cache disabled: 73.088 seconds.
- Both returned bundle CRC32 f426a2d1.

This is about 24 percent less elapsed time, not an end-to-end boot measurement.
It does not qualify data-cache coherency or a persistent bootloader update.

## Timing trap

The inherited board timer is a software call counter. Its get_timer values
are not elapsed milliseconds. The early profile build incorrectly labelled
them milliseconds; those readings are discarded. Its busy-wait delay loop
also depends on instruction/cache behavior.

The corrected RAM-only u-boot-h432b-nand-profile recipe offers:

- u2nandinit ident, then u2nandinit ecc.
- u2icache status/on/off (instruction cache only).
- u2nandboot a attach: attach the read-only UBI pool, then return.
- u2nandboot a read: read/check/CRC the selected bundle after attachment.
- u2nandboot a load: both operations without executing Linux.

Use host monotonic/wall time around separate commands. Never infer completion
from a host timeout or resend blindly; retrieve the pending result first.

## Remaining candidates

The current path scans the entire 507 MiB pool, reads through byte PIO, performs
software BCH, and loads the complete development initramfs. Correct hardware
timing, cache-aware execution, measured I/O optimization and a smaller
production boot payload are distinct work items. Keep bad-block, ECC and
image-integrity checks when optimizing; a speedup that drops them is invalid.

Separate host-timed operations with instruction cache on measured 35.444 s
for attachment and 37.653 s for read/validation/CRC, again with matching CRC.
These are diagnostic command times, not a qualified faster NAND boot.
The persistent baseline was not replaced by an optimization experiment.

## Hardware-timer candidate

Build `u-boot-h432b-nand-timer` through the pinned launcher to produce
`ram-nand-timer/u-boot.bin`. This is a separate RAM-only role; the installed
NAND bootstrap and default recipes retain their qualified behavior.

The candidate uses the 32-bit PWM4 downcounter with no output pin or timer
interrupt. It preserves the shared prescaler and channels 0–3, selects a
channel-local divide-by-16 clock, and derives the input rate from the inherited
PSYS mux/dividers. Timer state lives in U-Boot global data across early/late
initialization rather than BSS. Polling extends counter wraps; calls must occur
at least once per hardware rollover (over 343 seconds at the maximum accepted
clock and smallest prescaler).

The source builds and mock-register tests pass for clock selection, register
isolation, rollover, millisecond conversion, minimum delay and a stopped timer.
Device tests measured a five-second delay at 5.013 seconds with instruction
caching off and 5.012 seconds with it on, against the host clock. NAND identity,
BCH parity/repair and full kernel-bundle CRC checks passed. `u2timer status` reports
the rate and counter; host-time `u2timer test` (a five-second delay) with
instruction caching off and on before relying on its elapsed-time readings.
Data caching remains disabled and all NAND write guards remain active.

Register behavior and clock-tree derivation follow the upstream Linux
[S5PV210 clock driver](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/samsung/clk-s5pv210.c)
and [Samsung PWM clocksource](https://github.com/torvalds/linux/blob/v6.12/drivers/clocksource/samsung_pwm_timer.c).
The implementation does not retune PLLs or change shared clock divisors.

## Partial-page BCH read experiment

The 2012.10 NAND core installs a BCH-capable partial-page callback, but its
selection macro admits only software Hamming ECC. The isolated
`u-boot-h432b-nand-subpage` recipe permits the existing BCH callback too.
It does not enable partial writes, bypass ECC or alter OOB parity layout.

With the hardware timer and instruction caching enabled, UBI attachment fell
from 34.981 to 22.178 seconds (about 37% faster for that phase). Full bundle
read/validation took 37.368 seconds, versus 37.383 without this change.
The combined phases total 59.546 seconds; this excludes loader initialization,
Linux startup and host upload. The sub-30-second complete-boot target is unmet.

`u2nandslices` compared 15 corrected short reads against full-page reads over
EC-header, VID-header and scratch pages, including ECC-sector boundaries.
All matched, with zero ECC failures. The existing all-sector BCH parity and
1–8-bit RAM repair tests also passed. The kernel-bundle CRC remained f426a2d1.

The large development initramfs and uncached software ECC remain major costs.
The experiment does not change the default persistent bootstrap.

The partial-page candidate also loaded and booted the existing NAND kernel
through to a Linux USB shell: zero failed systemd units and kernel taint 0.
Linux/systemd took 28.774 seconds, excluding the loader. This was a RAM-staged
loader test, not a normal-reset qualification of an optimized persistent carrier.
