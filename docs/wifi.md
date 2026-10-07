# Internal Wi-Fi

## Support status

The RTL8712 SDIO radio can load firmware, execute normal commands and perform
passive scans under Linux. This is an **optional hardware-qualification driver**,
not a usable wireless network interface. There is no `wlan0`, association,
encryption-key installation or packet TX/RX support yet.

The default NAND kernel does not enable this diagnostic driver. The optional
`openh432-wifi-test` bundle uses the existing slot-B systembase; it is not a
standalone recovery image. Building or running offline tests does not access
the device.

| Capability | Device-qualified scope |
| --- | --- |
| SDIO transport | Function-register CMD53 and explicit block-mode FIFO transfers |
| Power and firmware | Cold initialization, firmware upload and ready handshake |
| Power acknowledgement | Native SDIO interrupt with verified active-state response |
| Normal commands | 256 consecutive requests and exact replies across seven-bit sequence wrap |
| Firmware events | Sequenced FIFO consumption and native C2H notifications |
| Passive scanning | Channels 1, 6 and 11; three consecutive surveys without radio reset |
| Networking | Not implemented: Linux wireless interface, association, keys and data traffic |
| Power saving | Warm initialization, suspend and resume remain unqualified |

Native scan notifications include empty wakeups. Interrupt acknowledgement and
efficient continuous event processing remain unresolved. An intermittent
`08` error-report value has appeared during successful scans; its meaning is
not established. Neither observation should be hidden or described as fixed.

Qualification applies to the exact images in the
[validation record](wifi-qualification.md), on one H432B device. Later source
changes, other firmware and other devices do not inherit those results.

## Hardware and protocol references

The radio is SDIO vendor/device `024c:8712`, function 1, class 07, on the
controller at `eb300000`, with 512-byte maximum blocks. MMC host numbering is
asynchronous; do not identify the radio or storage by an assumed `mmc0` number.

Linux's [r8712u driver](https://github.com/torvalds/linux/blob/v6.12/drivers/staging/rtl8712/Kconfig)
is USB-only and is not interchangeable with this SDIO device. The pinned
[Realtek source mirror](https://github.com/ronangaillard/rtl8712-driver-src/tree/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8)
provides protocol definitions but omits referenced SDIO HAL files. The board
implementation combines those definitions with analysis of the factory path;
it is not a port of a complete upstream SDIO driver.

Translated function-register accesses use byte-mode CMD53, including one-byte
operations. Standard function-zero CCCR operations use the SDIO core APIs.
FIFO transfers explicitly use block mode even when only one block is sent.
H2C commands, C2H events and FIFO transactions have distinct framing and
sequence counters; see the source and qualification record for tested details.

## Firmware and publication boundary

The BSP does not contain, fetch or redistribute the factory Wi-Fi firmware.
Testing requires an independently obtained, compatible firmware file with
established provenance. Linux requests `h432b/rtl8712s.bin` through the direct
firmware loader. A valid header is not proof of compatibility or permission
to redistribute the binary. Compatibility with publicly distributed USB
firmware has not been established.

Keep extracted firmware, factory images, disassembly, raw device logs,
network identifiers and credentials out of Git. Diagnostic output can contain
firmware bytes or debug text and must be treated as private evidence.

## Build and diagnostic interface

Use the pinned launcher in
[openh432-build](https://github.com/highenergymagic/openh432-build):

```sh
python3 scripts/bsp.py build openh432-wifi-test
```

For local layer development, use the launcher's documented `--local-layers`
option. This changes build inputs, not the default deployment target.
Deployment follows the [fastboot RAM-boot guide](fastboot.md); building is not
authorization to install firmware or replace NAND images.

The driver binds only to the H432B board and matching SDIO function. Binding
does not automatically initialize the chip. Root-only sysfs write attributes
request explicit operations; corresponding read attributes report errors,
cleanup status and measured results.

| Write attribute | Accepted request | Prerequisite / purpose |
| --- | --- | --- |
| `sample` | `1` | Compare initial CMD52/CMD53 register reads |
| `power_init` | `1` | Successful sample; initialize chip power and clocks |
| `firmware_load` | `memory` or `full` | Successful power initialization; code-only test or full firmware startup |
| `power_ack` | `1` | Full startup; request and verify active-state acknowledgement |
| `event_read` | `1` | Full cold startup; inspect the first pending event batch |
| `command_test` | See below | Full cold startup and successful acknowledgement; own the event stream |

The read attributes are `result`, `power_result`, `firmware_result`,
`power_ack_result`, `event_result` and `command_result`. Check both operation
and cleanup errors. Successful transport is not proof of successful firmware
execution, and successful cleanup does not undo volatile chip initialization.

Each action is one-shot per binding. Firmware `memory` and `full` are
alternatives, not successive steps. `event_read` and `command_test` are
mutually exclusive because both consume the initial FIFO sequence. Reboot
between independent tests; do not retry a partially completed sequence.

### Command actions

| Action | Behavior |
| --- | --- |
| `opmode` | Sixteen infrastructure-mode requests; polling event reads |
| `survey` | Two normal requests, then one passive scan; polling |
| `opmode-irq` | Sixteen normal requests using native C2H notifications |
| `survey-irq` | Two normal requests, then one interrupt-driven passive scan |
| `stress-irq` | 256 normal requests across sequence wraparound |
| `survey-repeat-irq` | Two normal requests, then three consecutive passive scans |
| `loopback` | Historical diagnostic: first reply matches, second times out; not a passing acceptance test |

Normal-command replies are validated factory debug events, not generic
protocol acknowledgements. Passive scans set no SSID, request no probes and
do not associate. BSSID record lengths and completion counts are checked;
network identifiers are not included in the survey counters. Aggregate
reports from several scans are not a count of unique networks.

Interrupt actions mask the source in the callback, release the MMC host while
waiting, and fail on timeout rather than falling back to polling. Callback
counts, empty wakeups and IRQ errors are separate results. RX and miscellaneous
sources are not enabled without handlers.

## Implementation and tests

The optional recipe is `linux-h432b-wifi-test_6.12.111.bb`. Its implementation
is intentionally separate from the default runtime provider:

- `h432b-wifi-transport.c`: board/function binding, serialized sysfs requests
  and result reporting.
- `h432b-wifi-power.h`: register access and chip power sequencing.
- `h432b-wifi-firmware.h`: image validation, upload and readiness checks.
- `h432b-wifi-irq.h`: bounded active-state acknowledgement.
- `h432b-wifi-events.h`: FIFO framing and initial event inspection.
- `h432b-wifi-command.h`: command framing, event matching, scan validation
  and bounded native-interrupt tests.

Run the source-contract suite from this layer:

```sh
python3 -m unittest discover -s tests
```

These tests check source and metadata contracts; they do not emulate the radio.
The [qualification record](wifi-qualification.md) preserves successful and
failed hardware experiments separately.

## Next development work

1. Resolve redundant event notifications and build a persistent command/event
   engine with correct interrupt, cancellation and teardown behavior.
2. Integrate a Linux wireless interface using cfg80211 and the firmware's
   full-MAC command protocol.
3. Implement and qualify association, authentication/key installation and
   packet TX/RX, including regulatory and security behavior.
4. Qualify power saving, warm initialization, suspend/resume and endurance.

No production-readiness, throughput, complete regulatory compliance or
firmware-redistribution claim is made.
