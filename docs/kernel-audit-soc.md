# Kernel integration audit: storage, audio, USB and power

This reference identifies the ownership and upstream limitations of the
non-Wi-Fi kernel integration patches. It covers the CIP 6.12.111-cip32 source
used by this BSP; PREEMPT_RT and target compilation are validated separately
by the normal kernel build. Passing a source audit does not establish
hardware qualification for a newly built image.

## Ownership and compatibility

SoC register behavior belongs in the Samsung controller, clock, pinctrl and
PWM drivers. Amplifier routing, factory NAND format, retained peripheral
power and the factory bootloader's resume entry are board contracts. These
must not be generalized to every S5PV210 system from testing one H432B board.

The canonical board audio and NAND compatibles are `hims,h432b-audio` and
`hims,h432b-nand`. The previous `hims,u2-audio-diagnostic` and
`hims,u2-nand-bch` strings remain accepted for existing device trees.
Historical filenames and Kconfig symbols are not evidence that the runtime
uses diagnostic-only behavior.

The NAND driver is deliberately board-specific: it validates the factory
geometry and inherits controller timing. It is not a complete, generic
S5PV210 NAND controller binding.

## Patch review matrix

| Patch | Owner and reviewed contract | Upstream limitation |
|---|---|---|
| `0001-readonly-storage-integration` | Diagnostic raw NAND selection and opt-in MMC write/ioctl rejection | The private MMC property is a diagnostic policy, not a generic MMC binding. |
| `0002-nand-reader` | Raw inspection only; whole-operation validation, no data-out, no ECC | Not selected as the standard runtime controller. |
| `0004-usb-host-required-power-bit` | S5PV210 host PHY power bit preserved without disturbing other power bits | The SoC manual requirement needs an upstream submission with its reference. |
| `0005-sdio-cis-end` | Exact RTL8712 identity, function, tuple length and address checks; no suppression of parse errors | The malformed-CIS workaround remains explicitly board-selected. Successful application is debug-only. |
| `0006-audio-clock-codec` | S5PV210 EPLL table gated on a 24 MHz reference; WM8983 SPI OF match | The rate table supports the board's clock family, not every audio rate. Clock and codec additions should be separate upstream submissions. |
| `0007-hims-u2-audio` | Board clock routing, DAPM amplifier sequence, managed resources and analogue gain limit | Physical codec-output routing is not fully characterized. Canonical compatible retains the old alias. |
| `0008-nand-bch-window` | Whole-operation validation before MMIO, bounded program/erase, BCH8/512, RAM BBT and cleanup | Fixed factory geometry and inherited timings prevent a generic SoC-driver claim. |
| `0009-factory-resume-test` | Reserved factory resume entry, layout validation and single external wake source | Factory boot contract, not generic S5PV210 resume behavior. |
| `0011-usb-host-phy-lifecycle` | EHCI/OHCI init and power references, reverse-order unwind on both failure paths | Generic fix candidate; needs independent Exynos host testing before submission. |
| `0012-onboard-h432b-usb-hub` | Existing onboard-device regulator/reset lifecycle, qualified reset hold | Reset timing is board-qualified, not a documented NEC silicon minimum. |
| `0015-nand-word-reads` | Word transfers only for aligned buffers and lengths, with forced-byte fallback | Applies only to the qualified S5PV210 FIFO path. ECC and writes are unchanged. |
| `0020-samsung-gpio-direction-readback` | Peripheral clock and bank-lock lifetime, split configuration registers, EINT input interpretation | Generic fix candidate; validate other Samsung bank variants before submission. |
| `0021-h432b-suspend-peripheral-retention` | Restore SROM timing before width and peripheral callbacks; reset SDHCI host while preserving card power | Host state loss has not been established for every S5PV210 board. Board selection is retained. |
| `0023-smsc911x-bound-loopback-rx` | Aligned buffers, FCS capacity and length validation before FIFO copy | Generic fix candidate; unexpected packets are drained rather than copied out of bounds. |
| `0024-h432b-ethernet-full-resume` | RTNL-protected close/reopen, IRQ removal, NAPI drain, reset and reopen failure handling | Board-specific loss-of-state behavior. A device-property binding requires independent lifecycle validation. |
| `0025-h432b-rtc-no-alarm-wake` | RTC timekeeping retained; board wake policy excludes alarms | Appliance policy remains selected by machine identity rather than an upstream binding. |
| `0026-h432b-audio-dma-request-clock` | Open-stream clock reference, negative-only constraint errors, balanced release | PDMA0 dependency is board-measured and must not be assumed for all SoCs. |
| `0027-h432b-audio-capture` | Duplex rate/width symmetry and DAPM microphone bias | Qualified PCM format is 44.1 kHz, stereo, S16_LE. Other formats are not silently advertised. |
| `0028-h432b-audio-jacks` | Managed GPIO/IRQ lifecycle, notifier cleanup, serialized amplifier state and no jack wake | Headphone and microphone routing still require accessory qualification. |
| `0030-h432b-hub-runtime-pm` | Board-and-device match, USB-core autosuspend policy; system sleep remains enabled | Necessary workaround for the empty-hub host-access stall; not a generic NEC quirk. |
| `0031-h432b-wakeable-poweroff` | Shutdown separate from suspend; release debounce, masked VIC/PMU sources, stackless reset target | Retained-memory soft-off is not electrical isolation. Resume bridge remains board-specific. |
| `0032-pwm-samsung-honor-disabled-state` | Explicit disable reaches hardware despite initially stale cached state; channel-local resume mask | Generic fix candidate; no global timer reset or clock-policy override. |
| `0033-irqchip-pinctrl-pm-debug` | Successful VIC and EINT-wake tracing is opt-in; error reporting unchanged | Logging-only generic fix candidate. |
| `0036-fw-devlink-consumer` | Preserve device-link errors and include the actual consumer firmware node | Generic diagnostic enhancement; identifying a broken dependency is not a lifecycle fix. |
| `0037-nand-clock-ownership` | Managed NFCON and NANDXL references before the first MMIO, in both NAND drivers | Both gates are required by the H432B register aperture; inherited controller timing remains a separate limitation. |

### Ethernet interrupt patch removal

The former `0022-smsc911x-suspend-irq-order` patch is not part of the series.
Masking the chip's interrupt output and calling `synchronize_irq()` does not
prevent a different device on a shared IRQ from subsequently invoking its
handler. Its generic path also did not drain NAPI before D1 sleep.

The supported board path already calls `dev_close()` before sleep. Therefore
`netif_running()` is false before the removed suspend block; on resume the
board reset/reopen path returns before the removed restore block. Removing
the patch leaves that board path unchanged and avoids applying an incomplete
generic lifecycle modification to other systems. Wake-on-LAN is not a
supported board wake source.

## Storage protections and runtime policy

The standard runtime exposes the complete 507 MiB Linux UBI pool for writes.
The factory prefix and reserved tail remain protected because they have a
different boot/ECC contract. Removing those boundaries would not improve
Linux filesystem write support.

Read-only raw inspection and single-block scratch modes are explicit
diagnostic profiles. A read-only SquashFS base is an image-format contract,
not a NAND-controller restriction. See [NAND](nand.md) for volume limits and
the distinction between UBI updates and mutable user data.

Both NAND controller implementations use kernel-style indentation and
control flow. The production controller uses standard `NAND_CMD_*` names;
the separately testable board guard retains wire opcodes without kernel
header dependencies. Successful register and writable-window traces use
debug logging. Rejected operations and probe failures remain errors.

## Clock ownership

The NAND node provides NFCON and NANDXL clocks, named `nand` and `bus`.
Both implementations acquire and enable both clocks before reading the
controller. Managed cleanup balances probe failure and removal; both remain
enabled while registered MTD users exist.

NFCON is the controller clock. Although the Samsung clock table names the
other gate after OneNAND, the H432B raw-NAND register aperture also depends
on it: with NFCON enabled, disabling only NANDXL makes NFCONF and NFCONT
read zero; restoring NANDXL restores their unchanged values. The factory
kernel enables both gates. This board-qualified dependency must not be
replaced with a global unused-clock override or generalized to other boards
without evidence.

Ethernet owns SROMC, the DMA and sound drivers own PDMA0, and the PWM and
clocksource drivers each own their timer reference. USB, MMC, UART, RTC and
I2S clocks use their corresponding peripheral drivers. Runtime-suspended
devices can legitimately have a registered consumer with no enabled clock.

Not every gate named in the S5PV210 clock-ID header is registered by the
pinned provider. In particular, GPIO, normal VIC, DMC and SYSCON gates are
not subject to common-clock unused-gate cleanup. TZIC gates are separate
from VIC gates; no new critical-clock exceptions are justified by their
inherited enabled state alone. The CPU PLL implementation has no disable
callback. The AUDSS provider's existing per-gate `CLK_IGNORE_UNUSED` policy
is independent of the global boot argument.

Removing the global `clk_ignore_unused` override requires installed-image
qualification: NAND reads and updates, interrupt/timer progress, audio,
network and USB operation, suspend/resume, software reboot and wakeable
shutdown. Static ownership review is not a substitute for those checks.

## Verification and remaining work

The storage, audio and power integration series is replayed with zero patch
fuzz against the pinned pristine CIP source. Whole-file upstream
`checkpatch.pl --file` checks cover both NAND controllers, both NAND policy
headers, the final audio machine driver and the final S5PV210 PM source.
Checking only mail-patch syntax is insufficient for older patches without
`diff --git` headers.

Native tests execute the actual NAND policy header over all 262,144 page
addresses, including factory and tail exclusion, full-page/OOB bounds,
split-operation status and completed-operation rejection. Existing erase
tests cover all 4,096 eraseblock addresses. Audio tests execute the stream
clock and headphone-gate code, and source tests check the power and IRQ
contracts. These are software checks; target builds and installed-image
tests are separate evidence.

The layer is not claimed upstream-ready. Remaining integration work includes
formal board bindings, eliminating machine-name checks where a documented
device capability can replace them, generic NAND timing management,
and qualification beyond this board. In particular, the Cortex-A8 Spectre v2
IBE prerequisite belongs to the CPU's early firmware entry, not a Samsung
board driver or a DT property. The common U-Boot entry supplies it and Linux
retains its independent validation. See the [boot contract](boot-contract.md)
for privilege requirements and installed-firmware verification.

Firmware-node dependency ownership must also match the driver model. Samsung
pinctrl consumes its wakeup interrupt-controller child internally, while GPIO
banks have separate GPIO-chip devices. The VIC interrupt controllers are
initialized before platform-device population. A dependency involving those
nodes must be repaired in the responsible driver/core lifecycle, not hidden
with a global `fw_devlink` override or an invented board supply. The upstream
consumer-name diagnostic keeps such failures visible and attributable.
