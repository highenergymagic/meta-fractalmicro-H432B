# RTL8712 SDIO Wi-Fi

## Current state

The internal radio enumerates as SDIO vendor/device `024c:8712`,
function 1, class 07, on the controller at `eb300000`. Its function CIS
advertises 512-byte maximum blocks. MMC host numbers are asynchronous; do
not identify the radio or internal storage by an assumed `mmc0` number.

Board power, clock selection and the narrowly scoped malformed-CIS workaround
are implemented. Enumeration alone does not establish firmware loading,
interrupt delivery, packet transfer or Wi-Fi connectivity.

## Source investigation

Linux 6.12's [r8712u configuration](https://github.com/torvalds/linux/blob/v6.12/drivers/staging/rtl8712/Kconfig)
requires USB and retains Wireless Extensions. It is not an SDIO driver.

The [vendor-source mirror](https://github.com/ronangaillard/rtl8712-driver-src/tree/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8)
contains SDIO register headers and an `8712s` Makefile branch, but lacks the
referenced SDIO HAL implementation files. The build switch is not sufficient.

A later [Realtek Linux SDIO glue file](https://github.com/ksclarke/rtl8192cu/blob/32063554366e3f7fa72eed0fd8412c0012043268/os_dep/linux/sdio_intf.c)
explicitly matches `024c:8712`. It provides useful function-enable,
block-size, interrupt and receive-path references, but depends on other
missing chipset-specific routines. It is not a complete working driver.
Its GPL-2.0 notice does not establish licensing for unrelated firmware files.

USB firmware compatibility and redistribution rights for firmware extracted
from CE have not been established. Do not publish those extracted binaries.

## Transport qualification

Build `openh432-wifi-test` through the pinned build launcher with local
hardware and OS layers. The bundle uses `linux-h432b-wifi-test` and the
existing slot-B root handoff. It does not change the default kernel or build
targets. Its Linux storage policy is the same as runtime, not globally read-only.

The test driver binds only the H432B board and exact function identity. Binding
does not enable the function or issue register transfers. For an explicitly
selected matching sysfs device, root can write `1` to its `sample` attribute
and read `result`. One request is allowed per binding; a second is rejected.

The request:

1. Saves function-enable state and current block size.
2. Enables the function if necessary and selects 512-byte blocks.
3. Reads local offsets 0 through 3 using CMD52, a four-byte CMD53 transfer,
   and CMD52 again.
4. Restores block size and function-enable state, reporting transfer and
   cleanup errors separately.

Static inspection of the factory driver's address conversion confirms that
local-window addresses map to their low 12 bits, supporting the offset range
used here. This does not establish live register behavior.

Offset zero is the vendor-defined local TX control register; the following
three bytes are free-page counters. The test does not write these registers,
access packet FIFOs, acknowledge interrupts, load firmware, program efuses
or transmit radio traffic. Transfer buffers are heap allocated.

A successful four-byte CMD53 request establishes only byte-mode transport,
not 512-byte block transfers. Counters may change between samples. All-zero
or all-ones data is inconclusive even if the host reports no command error.
A matching byte sequence is not proof of initialized firmware or working RF.

## Hardware result

The first test bundle (SHA256
`8680eb106f4d00f21c62a02c7ae5866baaa293bbc4312c9fe10b82cae32ca333`)
booted through fastboot without replacing the installed NAND images.

The explicit sample returned `24 00 00 00` for both CMD52 snapshots and
the intervening CMD53 read, with transfer error 0 and cleanup error 0.
A duplicate request was rejected. Kernel taint remained zero and systemd
reported no failed services.

This qualifies byte-mode transfers over the existing board SDIO host and
the local register window. It does not qualify block-mode FIFO access,
interrupts, firmware startup or any network operation. A successful restore
API return is not an independent electrical measurement of power state.

## Explicit chip power initialization

The optional test kernel also exposes root-write-only `power_init` and
read-only `power_result` attributes. A successful transport sample is required
first. Writing `1` runs one attempt per binding; it never runs automatically.

The implementation selects the factory warm/cold path from PLL, crystal and
clock register values, then initializes the chip's power and clock domains.
Delay values are in microseconds, with Linux sleep-range slack. It stops at
the first transfer error and bounds the warm-path polling loop. Final command
and scratch-register readbacks check that MAC register access responds.

This changes volatile chip registers, including the factory power-path bit
in EFUSE_TEST; it does not issue an efuse-programming command. It does not
load firmware, handle interrupts, send packets or create a network interface.
Function-enable cleanup is reported separately. **Chip register changes are
not rolled back**; reboot after the experiment. Do not confuse successful
bus cleanup with restored chip power state.

The register sequence is reconstructed for this board. Byte accesses currently
use Linux CMD52, while word/dword accesses use CMD53. The factory implementation
uses CMD53 for its translated register accesses; the prototype is therefore
not an instruction- or bus-transaction-exact clone.

The cold power path passed on hardware with bundle SHA256
`161ca0963c6c1cd051ec41b82935c308e25f3cd6621bb9fbc1629a4dedb5705c`.
Transfer and function-enable cleanup errors were both zero. PLL/crystal/clock
readbacks changed from `6900/ff0e/70a4` to `6911/fb8f/b8a0`; command
and scratch readbacks were `3fff` and `5678`. The duplicate request was
rejected, kernel taint remained zero, and no systemd units failed.

This qualifies the implemented cold path, including its CMD52 byte accesses.
The warm path is implemented but untested. Neither result establishes
firmware readiness, interrupt delivery or wireless connectivity.

## Explicit firmware memory test

After a successful power test, writing `memory` to `firmware_load` requests
the external file `h432b/rtl8712s.bin` through Linux's direct firmware loader.
The BSP does not distribute or fetch that file. Operators must establish
provenance and verify the chosen firmware separately; a matching file header
alone does not establish compatibility or redistribution rights.

The diagnostic validates section lengths before I/O. It sends IMEM and EMEM
through the SDIO firmware FIFO, using zero-padded 512-byte transfers and the
chip's 32-byte download descriptor. Host limits must permit each packet in
one CMD53 transaction. It checks each section's completion and checksum bits,
enables the Wi-Fi CPU, and waits for instruction-memory readiness.

`firmware_result` reports errors, bus cleanup, stage, payload bytes, packet
count, version and readiness registers. Stage 8 means both code sections and
CPU readiness passed. It does **not** mean fully initialized wireless firmware:
the test deliberately stops before DMEM radio configuration. No interrupt
handler, scan, association or network interface is added.

Stages 1–7 identify setup, IMEM transfer, IMEM check, EMEM transfer, EMEM check,
CPU enable and CPU-ready polling respectively. A transfer attempt is one-shot;
reboot after success or failure. As with the power test, restoring the SDIO
function settings does not undo volatile chip state.

The memory stage passed on hardware with test bundle SHA256
`dd988aa6780329437591382cbda975ec9468af7492ac5e594993178b29f9116f`.
It transferred 129,688 payload bytes in four packets, with transfer and cleanup
errors both zero. TCR progressed from `000a` to `000b` (IMEM checked),
`000f` (EMEM checked), and `002f` (CPU instruction-memory ready).
Kernel taint remained zero, no systemd units failed, and a duplicate request
was rejected.

The initial `000a` is important: checksum-result bits 1 and 3 are already set
before upload. The stale-state check therefore rejects DONE/READY bits
0, 2, 4 and 5, not the initial checksum-result bits. The first test exposed
this distinction and stopped before sending any firmware.

This qualifies the tested block-mode upload and CPU-ready handshake, not
the final firmware-ready flag or networking. The private firmware used for
qualification remains outside the public source and build inputs.

## Full firmware-start test

Alternatively, writing `full` to `firmware_load` runs the same code upload
and then sends a generated 48-byte SDIO configuration. The configuration
matches the factory board registry, including its virtual carrier-sense
override. It is not copied from the firmware file's USB private block.

Stages 9–12 mean DMEM transfer, DMEM completion, final readiness polling,
and full firmware readiness respectively. The final check uses TCR bit 7,
distinct from CPU readiness at bit 5. The boot-source register selects a
bounded three- or six-second readiness wait. The diagnostic reports DMEM
and final TCR values separately.

The full path passed on hardware with bundle SHA256
`1cb7b490aa5e087a4351fa14ad05d7c5a69453e38f6545a8b4fb31704fa134dd`.
It transferred 129,736 payload bytes in five packets, with transfer and
cleanup errors both zero. TCR reached `003f` after DMEM and `02ff` at final
readiness. A duplicate request was rejected; kernel taint remained zero and
no systemd units failed.

This qualifies firmware startup on the tested cold initialization path,
not interrupt delivery, scan, association or network traffic. The prototype
still has no network interface.

## Active-state interrupt test

After full firmware startup, root can write `1` to `power_ack` and inspect
`power_ack_result`. This optional diagnostic requests the active state with
an acknowledgement and a changed request toggle. It arms only the SDIO CPWM
source; receive and firmware-event queues remain disabled.

The handler masks the source, records status and acknowledgement, and wakes
the waiting request. The request releases the MMC host during its bounded
two-second wait, then removes the handler and restores the saved interrupt
mask and function-enable state. A reply must have a changed toggle and the
requested active-state value. A duplicate request is rejected.

The `native` field reports host SDIO interrupt capability. A callback on a
polling-only host would not prove native interrupt delivery. Cleanup errors
are separate from request errors; returning bus settings does not reverse
the firmware's requested active state. No suspend request is issued.

The acknowledgement test passed with bundle SHA256
`ad2024d15624d8649b02b307e757b9c753973adf5c664bd11b6f04c842ef5750`:
one callback on a native-capable host, request `cc`, reply `8c` from a
`00` baseline, and no request, handler or cleanup errors. The original
interrupt mask was zero. Duplicate requests were rejected; kernel taint and
failed-service counts remained zero.

HISR was `04fe` before and after cleanup. This includes other flags,
including the documented C2H and CPU-error bits, which remain unexplained.
The acknowledgement result does not establish general firmware health or
prove that reading HCPWM clears HISR.

Register definitions and SDIO acknowledgement semantics are documented
in the pinned [vendor power-control source](https://github.com/ronangaillard/rtl8712-driver-src/blob/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8/pwrctrl/rtl871x_pwrctrl.c).
The USB implementation does not request acknowledgements in the same way.

## Pending firmware-event inspection

The optional `event_read` request inspects the C2H FIFO once after a cold
firmware startup. `event_result` reports the pre-upload counter baseline,
current and final cumulative block counts, transfer size, packet validation
result and first-event metadata. It also exposes the first 64 bytes for
diagnosis; treat such device output as private data.

The reader requires an empty interrupt mask and no registered handler.
It accepts at most 32 new 512-byte blocks and transfers them with one CMD53
using the initial two-bit FIFO sequence. It validates descriptor markers,
packet lengths, event lengths and aligned strides before advancing.
Warm initialization, repeated reads and concurrent consumers are unsupported.
An empty queue returns `ENODATA`, not a successful FIFO-transfer result.

The first-batch path passed on hardware with bundle SHA256
`3ecc3ef3821f488f702bad4657326c5132191e54c96ba2590aecfd78f41cd654`.
The counter baseline was zero; four pending blocks produced a 2,048-byte
transfer containing four valid events. The first event had code 19
(firmware debug), sequence zero and a 15-byte payload. The cumulative
counter had advanced to eight after the read, so this was not a full drain.

Transfer and cleanup errors were zero. Full firmware startup and the native
CPWM acknowledgement also passed on this second fresh boot, with no kernel
taint or failed services. This qualifies one first-sequence FIFO read, not
continuous reception, C2H interrupt delivery or host-command responses.
No host scan or association command was sent.

## Host-command loopback test

After a successful active-state acknowledgement, writing `loopback` to
`command_test` runs a bounded command/event test. It cannot follow or precede
`event_read` on the same binding: each operation owns the initial FIFO
sequence. Reboot between tests.

The test drains startup events, checks command queue space, then sends two
differently tagged H2C loopback requests. Its descriptor selects the command queue;
the SDIO transfer is one incrementing 512-byte block-mode CMD53. No scan, association or RF manufacturing command
is issued.

The vendor header describes a transformed 28-byte reply, but the tested
factory firmware returned a prefix matching the command header and initial
parameters instead. The first test correctly rejected that mismatch. The
current test checks a proposed 12-byte echo format: exact command length,
command code, sequence, reserved word and four parameter bytes, for both
requests. One matching response has been observed; the second request times out.
Event reads track a two-bit FIFO port sequence
and a seven-bit event sequence. The consumed block count advances only to the
pre-transfer snapshot, preserving events arriving during a read. Transfer sizes,
batch counts and the two-second response wait are bounded.

`command_result` reports whether a command was sent, whether its reply matched,
stream counts, cleanup errors and limited private diagnostic bytes. A successful
SDIO write alone is not a successful firmware command. The first test received 21 correctly sequenced events across six FIFO
batches, including a loopback event, but did not pass its response-content
check. This qualifies successive event reads, not an exact command round trip.

The revised bundle (SHA256
`83cd6149e7a60b2ce6f51b0058f620d80b26195d42949ecf669345ccf39dece4`)
matched the first request's exact 12-byte echo, including sequence and tags.
The second request was transmitted but timed out: `error=-110`, `cleanup=0`,
`replies=1`, `reply_length=12`. Across the test, six FIFO batches contained
21 correctly sequenced events (20 debug events and one loopback response).
HISR changed from `04fe` to `05fe`; its additional flags remain unresolved.
Kernel taint and failed-service counts remained zero. This establishes one
correlated command response, **not a repeatable command channel**. No scan,
association or wireless networking has been qualified.

## Factory command-transfer correction

A deeper trace corrected the initial byte-mode interpretation. The factory
interface embeds its callback table at offset 0x20: the H2C callback at
interface offset 0x38 is therefore the block-write member at table offset
0x18, not the byte-write member. Its final zero argument selects synchronous
operation. For function 1 and FIFO address 0x18c80, the reconstructed CMD53
argument is `0x9f190001`: write, block mode, incrementing address, one block.

The implementation now explicitly requests one 512-byte block. Earlier
command-test results above used byte mode and remain historical evidence,
not qualification of the correction. Descriptor, payload and reply checks
are unchanged.

The corrected bundle (SHA256
`3fe44b7b084ee31d87ce1f11a8dc16a31576f965cdb6c4f0a669f866398887dc`)
was built and tested on the device. All five error-report snapshots now
remain `00 00 00`, removing the earlier `08` report. The first exact echo
still matches, but the second command still times out (`error=-110`,
`cleanup=0`, one reply). RX remains zero, C2H stops at 21, command free
pages fall from 203 to 202 to 201, and HISR ends at `05fe`. Kernel taint
and failed-service counts remain zero. This validates removal of the
observed transfer error, not a working repeated command channel. Command
processing and queue consumption still need investigation.

## Queue/error snapshots

The command diagnostic records five ordered snapshots: before the startup
drain (phase 0), before/after command 1 (phases 1/2), and before/after
command 2 (phases 3/4). Each includes SDIO interrupt status, cumulative RX
and C2H block counts, TX control, public/command free-page counts, and
SDIO error report/command-error/data-error bytes. Snapshot read errors
are reported independently; an incomplete snapshot must not be interpreted
as zero-valued successful reads.

These are sequential register reads, not an atomic hardware snapshot.
They add timing overhead but no RX drain or status-clear writes. The
instrumented image is qualified separately below.

The [vendor SDIO bit definitions](https://github.com/ronangaillard/rtl8712-driver-src/blob/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8/include/rtl8712_spec/sdio_reg/rtl8712_sdio_bitdef.h)
name bit 8 RX overflow. Its appearance in the previous failing test is a
diagnostic clue, not proof that unread RX caused the command timeout.
The factory interrupt path services both RX and C2H; this prototype only
drains C2H. No explicit overflow-clear operation was found in the inspected
factory interrupt dispatcher.

The instrumented bundle SHA256
`c5b4e1479094ab6879aec22dd0e8f7a938df23514764c88baf44fd605f55bfb3`
reproduced the first matching echo and second-command timeout. All five
snapshots succeeded. RX count stayed zero; C2H advanced from 4 to 20 before
the first command and to 21 after its reply, then stopped advancing.
Command free pages fell from 203 to 202 to 201 while public pages stayed 196.
The error-report byte changed from `00` to `08` after command 1, before the
final HISR change to `05fe`. Command/data error counters remained zero.
TX control remained `24`; kernel taint and failed-service counts were zero.

This does not support a growing unread RX queue during the observed interval.
The error-report bit's meaning is not established: do not label it a CRC
error or apply HISR bit definitions to it. Next investigate command framing,
queue consumption and the factory SDIO transfer/error-report path.

## Post-firmware hardware setup

The optional command test now applies the factory HAL's post-firmware
register sequence before issuing commands: enable appended PHY status,
clear the command register's high byte, write PMC_FSM+2 to 0x3b, set the
command register's low byte to 0xfc, clear TX pause, and select normal
SDIO debug status. Access widths and order match the factory path and
[Realtek's GPL HAL](https://github.com/ronangaillard/rtl8712-driver-src/blob/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8/hal/rtl8712/hal_init.c).
Stage and register readbacks are included in `command_result`.

The factory then unmasks interrupts. The polling diagnostic deliberately
does not: it has no general RX/C2H handler yet. This is not a claim that
the complete factory runtime has been reproduced. The first test applied the writes but stopped before sending commands:
an incorrect latch-style readback check was applied to PMC_FSM+2, which
returned live status 0x71 after a 0x3b write. That assumption and its earlier
RF-control label have been removed; the status remains reported. RCR and
command-register readbacks matched.

The corrected image (SHA256
`78d14ea766e33de245c486e9f4a5ebce18f26e1bab075df468b05bf21f4ee2fa`)
passes setup stage 7 and all checked readbacks. Loopback still matches one
event and times out on the second request. The error-report byte becomes
`08` again after command 1; the earlier zero result is not stable across
this changed setup. Kernel taint and failed-service counts remain zero.
This qualifies the tested setup sequence, not repeated loopback commands.

The factory firmware's command table and loopback handler were also traced.
Its loopback event is explicitly 12 bytes, unlike the transformed structure
described in the public header. A loopback stall alone is therefore not
sufficient evidence that all normal firmware commands are broken.

## Normal-command diagnostic

Writing `opmode` to `command_test` instead of `loopback` requests
infrastructure mode sixteen times, one request at a time. It does not scan,
join a network, set keys or transmit test frames. Each request must produce
the exact factory debug event (including its terminating NUL), while the
C2H event sequence remains continuous. This observable handler trace is not
a protocol-level command acknowledgement or evidence of working networking.

The test shares the one-shot stream ownership and bounded waits with
loopback. Reboot between actions. The image builds and offline tests pass;
hardware qualification is pending.

## Remaining milestones

- Qualify the warm chip initialization path and repeated power cycles.
- Resolve additional status flags; qualify subsequent C2H sequences and event interrupts.
- Implement host-command round trips and RX packet handling.
- Integrate a maintained wireless userspace interface, then scan and association.
- Validate security capabilities, regulatory behavior, RT locking and power saving.

The source-contract tests are offline checks. They do not emulate the device
or qualify these milestones.
