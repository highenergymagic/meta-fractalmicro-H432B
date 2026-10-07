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

## Remaining milestones

- Recover or implement chip-specific SDIO initialization and firmware loading.
- Qualify interrupt acknowledgement and block-mode FIFO transfers.
- Integrate a maintained wireless userspace interface, then scan and association.
- Validate security capabilities, regulatory behavior, RT locking and power saving.

The source-contract tests are offline checks. They do not emulate the device
or qualify these milestones.
