# Boot and NAND performance

## Runtime configuration

The normal runtime runs at 800 MHz. CPU-frequency scaling and 1 GHz operation
are unqualified. Factory CE suspend/resume is not comparable to a full reboot;
no matched stock-CE cold-boot measurement is available.

The standard boot path uses:

- A/B U-Boot with instruction caching, aligned word FIFO reads, corrected BCH
  subpage reads and one reusable Linux UBI attachment.
- Software BCH8, specialized for M=13, T=8: 512-byte steps and 13 parity bytes.
- A root-handoff initramfs with RAM-resident startup audio.
- A gzip SquashFS systembase with full static-volume CRC verification.
- Device-only udev coldplug with four workers and an early maintenance-tty event.

Data caching remains disabled in U-Boot. On-flash parity, correction strength
and image checks are unchanged. Hardware ECC is not a drop-in replacement
without parity-compatibility qualification.

The distribution layer owns
[runtime discovery policy](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/runtime.md#device-discovery)
and [startup audio](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/system-sounds.md).
Non-filesystem UBI image and bootstate volumes are excluded from generic udev
filesystem probing; selected-root verification, block-device discovery and
normal hotplug remain enabled.

## Standalone console policy

The normal NAND kernel uses `console=tty0`, `loglevel=4` and
`systemd.show_status=no`. Kernel messages remain available through `dmesg`;
systemd logs remain in the journal. The explicitly enabled `ttyGS0` maintenance
shell is independent of the boot console and does not require a host reader
for local braille startup.

Using the USB gadget as `/dev/console` makes systemd status-line closes wait
for unread serial output to drain. The gadget driver permits a 15-second
close timeout, so an unattended boot can accumulate minutes of delay.
Diagnostic profiles retain their explicit USB console; keep a reader attached
when using those profiles. Normal boot qualification must include a run with
no host console reader.

## Qualified measurements

These are samples from one board, not performance guarantees. Exact image
hashes and test scope are in the
[hardware validation records](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md).
A complete boot below 30 seconds has not been demonstrated.

### Persistent loader

Instrumented ordinary NAND boots of the same 9,054,208-byte kernel bundle
measured the following intervals. Both carriers used the same timing hooks.

| Measurement | Generic BCH | Fixed M=13, T=8 |
| --- | ---: | ---: |
| UBI attachment | 15.496 s | 13.054 s |
| Kernel load and checked handoff | 18.939 s | 16.808 s |
| Total measured loader interval | 34.724 s | 30.118 s |
| BCH calculation, scan plus load | 21.618 s | 17.093 s |
| FIFO transfers, scan plus load | 4.290 s | 4.290 s |
| CRC32, load interval | 3.861 s | 3.861 s |

Specialization reduced the measured interval by 4.606 seconds (13.3%).
Recovery-assisted launch measured 30.134 seconds. These intervals exclude
factory boot, bootstrap initialization and final Linux entry.
Software BCH calculation remains the dominant measured loader cost.

The A/B build compares the fetched upstream generic and specialized BCH
implementations across 8,448 deterministic corruption cases, including parity
equality and one-through-eight-bit repair. This is algorithmic regression
coverage, not physical power-loss or NAND-aging qualification.

### Linux startup

The standard systembase trades image size for lower decompression cost.
Two consecutive normal NAND boots with matched contents measured:

| Measurement | XZ systembase | Gzip systembase |
| --- | ---: | ---: |
| Systembase bytes | 33,562,624 | 42,700,800 |
| Selected-root block creation | 16.7 s | 19.4 s |
| BRLTTY virtual input | 55.6 s | 42.9–43.4 s |
| D-Bus startup duration | 14.8 s | 3.0–3.2 s |
| Multi-user activation | 56.2 s | 53.2–53.7 s |

Startup milestones are relative to Linux entry; D-Bus is a service duration.
They must not be added together. The larger gzip image increases the full-root
verification cost but reaches the braille input milestone earlier.

Queuing the maintenance tty before bulk discovery moved its device-ready event
to 36.4–36.5 seconds and shell activation to 40.3–40.4 seconds, from approximately
50–52 and 53–54 seconds respectively. BRLTTY remained around 43 seconds; this
did not demonstrate an overall multi-user speedup.

Root handoff waits for the startup cue, which completes around 14 seconds
after Linux entry. Removing the full-root scan alone therefore cannot reclaim
its entire duration: at the measured 19.4-second block-creation milestone,
audio would become the limiting wait after roughly five seconds of improvement.
On-demand verified-root support is not implemented.

### NAND transfer

| Linux measurement | Byte reads | Aligned word reads |
| --- | ---: | ---: |
| Corrected 4 MiB MTD read | 1.993 s | 1.145 s |
| 4 MiB UBI read | 2.006 s | 1.162–1.163 s |

Subsequent matched nanddump samples measured 1.02–1.05 seconds without
correction and 1.24–1.25 seconds with correction. These measurements do not
authorize disabling ECC or establish the cost of bootloader BCH.

## Loader instrumentation

Linux receives `openh432.loader_attach_ms`, `openh432.loader_kernel_ms` and
`openh432.loader_total_ms` in its command line. They use the PWM4 hardware clock:

- Attach covers the boot-state command's UBI attachment.
- Kernel covers the selected kernel command through checked handoff.
- Total begins at the boot-state command and includes attempt persistence.

Detailed NAND profiling is disabled by default. Set `H432B_NAND_TIMING = "1"`
in the Yocto build configuration to enable `CONFIG_H432B_NAND_TIMING` in the
A/B loader. Then `openh432.nand_scan` and `openh432.nand_load` each contain
seven comma-separated values:

1. FIFO bytes read, including spare-area transfers.
2. FIFO-read milliseconds.
3. NAND command-callback milliseconds, including ready waits.
4. BCH calculation milliseconds.
5. BCH correction milliseconds.
6. Bytes passed through CRC32.
7. CRC32 milliseconds.

Scan covers UBI attachment; load begins immediately afterward and includes
boot-state access and kernel handoff preparation. Callback timings can overlap,
and sampling adds overhead. Do not sum them as independent CPU costs.
Disabled instrumentation does not access the hardware timer from CRC32.

## Measurement and diagnostic tools

Record image hashes, start/end events, cache state, byte counts and ECC policy.
Use Linux timing for Linux operations and host monotonic time for complete
reboot/console intervals. Host timeouts do not cancel target commands.

The A/B loader uses PWM4 elapsed time. Older bootstrap profiles inherit a
software call counter; their nominal milliseconds are not elapsed-time evidence.
PWM4 preserves channels 0–3 and the shared prescaler, uses a channel-local
divide-by-16 clock and derives the inherited PSYS rate. Polling must extend
counter wraps at least once per rollover (over 343 seconds at the maximum
accepted rate).

Opt-in profiles remain available for isolated tests:

| Recipe | Interface |
| --- | --- |
| `u-boot-h432b-nand-profile` | `u2icache status/on/off`; separate attach/read/load operations |
| `u-boot-h432b-nand-timer` | `u2timer status` and a five-second `u2timer test` |
| `u-boot-h432b-nand-subpage` | Corrected partial-page reads and injected-error checks |

Use the [boot contract](boot-contract.md) to select the correct high-RAM
artifact. Profiles are not installers or authorization to overwrite a running
stage. Explicit maintenance `ubi part` commands retain forced reattachment.

Clock references:
[Linux S5PV210 clock driver](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/samsung/clk-s5pv210.c),
[Samsung PWM clocksource](https://github.com/torvalds/linux/blob/v6.12/drivers/clocksource/samsung_pwm_timer.c).
