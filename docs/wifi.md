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

## Remaining milestones

- Resolve board radio configuration, upload DMEM and qualify final firmware readiness.
- Qualify the warm chip initialization path and repeated power cycles.
- Qualify interrupt acknowledgement and packet FIFO traffic; firmware block uploads pass.
- Integrate a maintained wireless userspace interface, then scan and association.
- Validate security capabilities, regulatory behavior, RT locking and power saving.

The source-contract tests are offline checks. They do not emulate the device
or qualify these milestones.
