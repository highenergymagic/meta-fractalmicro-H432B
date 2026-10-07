# Boot performance investigation

## Runtime NAND measurements (2026-10-06)

The normal NAND-root system runs the CPU at 800 MHz. This is confirmed by
the Linux clock tree, not inferred from BogoMIPS. The retained factory first
stage also explicitly selects this rate. A later stock CE transition to the
advertised 1 GHz has not been established.

On the byte-transfer Linux driver, repeated 4 MiB reads measured:

| Path | Elapsed | Kernel CPU |
| --- | ---: | ---: |
| MTD corrected read | 1.993 s | about 1.9 s |
| UBI character-volume read | 2.006 s | about 1.9 s |
| nanddump without ECC | 1.85 s | 1.73 s |
| nanddump with ECC | 2.09–2.10 s | 1.94–1.96 s |

The two nanddump cases use the same address, length and bad-block policy.
Raw reads bypass correction only for measurement; runtime reads retain ECC.
These results do not support blaming most of the delay on BCH: bypassing it
saves roughly 12% here. They also show little additional UBI overhead.

Python startup without imports took 0.64–0.66 s. The assistance program's
standard-library imports took 3.73–4.37 s warm. After syncing and dropping
clean filesystem caches, the same imports took 9.40 s (3.48 s user CPU,
3.50 s kernel CPU), followed by 3.74 s warm. The difference includes NAND,
filesystem and decompression costs; it is not a standalone XZ benchmark.

The factory NAND implementation uses word-sized data FIFO transfers and a
hardware error-correction engine. The Linux optimization uses word reads
only for aligned bulk operations that do not require byte access. It retains
byte fallback, existing software BCH/OOB format and the unchanged write path.
Hardware ECC cannot simply replace software BCH without proving parity
compatibility with the installed Linux volumes.

CPU-frequency support is not enabled. The upstream S5PV210 driver requires
working ARM/internal-supply regulators and DMC information, and coordinates
voltage, PLL, bus dividers and DRAM refresh. A PLL-only write is not an
implementation of board DVFS. The PMIC control-bus address alone does not
identify the chip or establish its rail voltages.

EBOOT contains menu-selectable 1000/800/400/100 MHz clock profiles and a
routine that changes PLLs, dividers and memory refresh. The existence of
that menu is evidence for a factory clock transition implementation, not
proof of the operating voltage or a normal CE boot selecting 1 GHz.

## Word-read result

The aligned word-read kernel was installed in kernel_B and booted normally
from NAND. Full SHA256 checks of both kernel volumes and both system-base
volumes passed. NAND corrected-bit and ECC-failure counters were zero;
the kernel was untainted, systemd had no failed units and gpsd was active.
The root images, slot A and persistent bootstrap were not changed.

Repeated 4 MiB MTD reads fell from 1.993 s to 1.145 s, approximately **74%
higher throughput** (2.1 to 3.7 MB/s), or **43% less read time**. UBI reads
fell from 2.006 s to 1.162–1.163 s. Identical nanddump tests measured
1.02–1.05 s without correction and 1.24–1.25 s with correction.

The cold-import test fell from 9.40 s to 8.30 s; warm imports remained about
3.8 s. These are individual controlled-cache samples, not statistical
end-to-end boot results.

A separate RAM-only XZ benchmark decoded a 1,390,508-byte compressed Python
shared library to 4,763,296 bytes in 0.84 s warm, with matching decoded
SHA256. It used BusyBox xzcat, CRC32 and 256 KiB dictionary/blocks. This
isolates a representative decoder workload from NAND I/O but is not the
kernel SquashFS decoder or a measurement of all boot-time decompression.

The patch builds with the pinned OE container; 124 hardware-layer tests
pass, including byte-fallback and patch-order regressions. Cross-host
bit-for-bit reproduction has not been repeated for this change.
The bootloader still uses its previous transfer path: this Linux result
does not establish a faster complete boot or meet the 30-second target.

## Persistent bootloader word reads

The maintenance bootstrap now uses aligned 32-bit FIFO reads, with byte
reads for unaligned heads and short tails. NAND status/ID access remains
byte-sized; ECC, write restrictions and image checks are unchanged.
An exhaustive FIFO test covers lengths 0–4096 at four buffer alignments
inside the pinned OE build. The layer has 126 passing regression tests.

The CE-carried bootstrap was installed and read back exactly, including
its embedded high-RAM stage. The retained factory StepLoader/EBOOT and
their OOB bytes match the private pre-conversion backup. Both the
recovery-assisted launch and a subsequent ordinary software reboot reached
the existing NAND-root Linux system, untainted and without failed units.

One comparable host-monotonic sample measured reboot-request to executable
USB-shell command at **107.46 s**, versus **121.48 s** with the byte-read
bootstrap and the same Linux word-read kernel/root. This interval includes
shutdown and console-handshake overhead; it is not power-on-to-ready time.
Linux uptime at the measurement was 44.21 s versus 46.58 s. The reduction
is about 14 s (12%) end to end, not the 74% Linux read-throughput result.
No sub-30-second boot is claimed.

The bootstrap still needs separate integration/qualification of the
hardware timer, partial-page BCH reads and cache-aware execution. The
earlier experiments below must not be mistaken for enabled default behavior.

## Historical bootloader measurements

The measurements below used earlier, larger development initramfs bundles.
They remain useful comparisons for those exact experiments, not current
end-to-end boot measurements of the split NAND kernel/root filesystem.

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

## Comparison with Windows CE

Package size does not explain the current boot latency by itself. The examined
factory NK package is 34,463,699 bytes (32.9 MiB), whereas the measured Linux
kernel bundle plus compressed base is 33,038,336 bytes (31.5 MiB). The factory
application package is a separate ZIP archive, not evidence that all its bytes
are read at every boot. These products contain different functionality; package
size is neither RAM consumption nor the startup working set.

CE uses a board-specific image construction and loader path. Microsoft describes
[Romimage as the locator that creates NK images](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938650%28v%3Dmsdn.10%29).
The recovered factory NAND driver uses word transfers and hardware ECC. Linux's
measured word-transfer gain is documented above; the current software-ECC cost
alone does not explain the full difference. No matched stock-CE cold-boot timing
has been captured, so an overall CE/Linux speed ratio is not established.

A representative optimized-loader Linux boot reported 13.690 seconds in the
kernel and 60.064 seconds in userspace from systemd-analyze, with multi-user.target
reached 47.544 seconds into userspace. This excludes the bootloader and is not
the earlier USB-shell readiness metric. The reported critical chain includes
udev-trigger (13.571 seconds) and D-Bus activation (15.347 seconds). Individual
unit durations overlap and include waits; they must not be summed or treated as
CPU execution time. Investigate cold storage access, decompression and service
ordering before attributing delays to systemd itself.

Finally, the factory power switch normally suspended and resumed the device.
Comparing that wake-up with a full Linux reboot is not a cold-boot comparison.
Full suspend/resume support remains separate work.
