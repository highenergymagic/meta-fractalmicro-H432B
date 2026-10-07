# Boot and NAND performance

## Operating conditions

The normal runtime runs at 800 MHz, confirmed by the Linux clock tree.
CPU-frequency scaling and 1 GHz operation are not qualified. Factory CE
suspend/resume is not comparable to a full Linux reboot.

The persistent loader and Linux driver use aligned 32-bit FIFO reads with
byte fallback. Status/ID accesses remain byte-sized. Software BCH8 and the
installed OOB layout are unchanged; hardware ECC is not a drop-in replacement
without parity-compatibility qualification.

## Measured baseline

Measurements below are qualification samples, not performance guarantees.
They describe the word-read implementation tested on one board, not every
image produced by subsequent source revisions.

| Measurement | Byte-read baseline | Word-read implementation |
| --- | ---: | ---: |
| Linux corrected 4 MiB MTD read | 1.993 s | 1.145 s |
| Linux 4 MiB UBI volume read | 2.006 s | 1.162–1.163 s |
| Comparable software reboot to executable USB-shell command | 121.476 s | 107.461 s |

The Linux MTD change reduced read time by about 43% (throughput from about
2.1 to 3.7 MB/s). The reboot comparison improved by about 14 seconds; it
includes shutdown and host console handshake, not physical power-on timing.
A complete boot below 30 seconds has not been demonstrated.

Matched nanddump tests measured 1.02–1.05 seconds without correction and
1.24–1.25 seconds with correction after the word-read change. ECC is not the
sole bottleneck. These measurements do not authorize disabling correction.

A controlled-cache Python import sample improved from 9.40 to 8.30 seconds;
warm imports remained about 3.8 seconds. A separate RAM XZ sample decoded
1,390,508 compressed bytes to 4,763,296 bytes in 0.84 seconds with matching
hash. Neither is an isolated measure of all boot-time SquashFS decompression.

## Measurement procedure

Use host monotonic time for bootloader command intervals and Linux timing
for Linux operations. Record the exact image, start/end events, cache state,
byte count and ECC policy. Unit durations overlap and must not be summed
as CPU time. Compare identical payloads before attributing a timing difference
to one implementation change.

The inherited persistent-loader timer is a software call counter, not elapsed
milliseconds. Its readings are unsuitable for performance claims. Host timeout
does not cancel a target command; retrieve the outstanding result before
sending another operation.

## Optional loader experiments

These are RAM-only profiles, not default persistent-loader behavior.

| Recipe | Purpose and measured scope |
| --- | --- |
| `u-boot-h432b-nand-profile` | Instruction-cache comparison; complete attach/read/check fell from 95.823 to 73.088 s for the same 15,509,504-byte development bundle |
| `u-boot-h432b-nand-timer` | PWM4 hardware timing; requested 5 s measured 5.013 s with I-cache off and 5.012 s with it on |
| `u-boot-h432b-nand-subpage` | Corrected partial-page BCH reads; attach fell from 34.981 to 22.178 s, with full bundle read/check about 37.37 s |

The profile exposes `u2icache status/on/off` and separate
`u2nandboot a attach|read|load` operations after NAND identification/ECC
setup. Use the [boot contract](boot-contract.md) to select the correct
high-RAM artifact.

The timer uses PWM4 without an output pin or interrupt. It preserves channels
0–3 and the shared prescaler, uses a channel-local divide-by-16 clock and
derives the inherited PSYS rate. Polling must extend counter wraps at least
once per rollover (over 343 seconds at the maximum accepted rate).
`u2timer status` reports configuration; `u2timer test` requests a five-second
delay. Data caching remains disabled.

The partial-page profile permits the existing software BCH callback without
partial writes or OOB changes. Fifteen corrected short reads matched full-page
reads, and injected one-through-eight-bit repair tests passed. The measured
59.546-second attach/read total excludes initialization and Linux startup.
These results do not establish a faster persistent boot.

Clock references:
[Linux S5PV210 clock driver](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/samsung/clk-s5pv210.c),
[Samsung PWM clocksource](https://github.com/torvalds/linux/blob/v6.12/drivers/clocksource/samsung_pwm_timer.c).

## Remaining limits

The loader scans the UBI pool and performs software ECC and image checks.
Storage access, decompression and service ordering all contribute to startup.
Optimization must preserve bad-block handling, correction and integrity checks.

Factory NK archive size is not the boot working set; applications and suspend
state further complicate CE comparisons. No matched stock-CE cold-boot timing
is available, so no overall CE/Linux speed ratio is established.
