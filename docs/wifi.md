# Internal Wi-Fi

## Support status

The RTL8712 SDIO driver exposes a cfg80211 station interface, `wlan0`.
WPA2-Personal association, host-managed CCMP keys, DHCP, IPv6 router
advertisements and interface-bound Internet pings have passed on a NAND-booted
H432B. Wi-Fi-only, checksum-verified TCP transfers in both directions and an
explicit disconnect/reconnect have also passed; see the
[validation record](wifi-qualification.md).

The default runtime includes the driver. Systembase supplies `iw`,
`wpa_supplicant`, `wpa_cli`, the signed regulatory database and
`FMWiFi.service`. Radio firmware is extracted from an operator-supplied stock CE image at build time.
With it, startup initializes the radio and creates the interface automatically;
without it, initialization is skipped. Building never accesses hardware.

The service requests the driver's `initialize` operation once firmware is
available on systembase. The driver validates firmware, performs power and
firmware setup, acknowledges the active state and registers the interface.
Repeating a successful request is harmless; a failed hardware sequence still
requires a fresh boot. Routine scan and association traces use debug logging;
initialization and transport failures remain error reports.

This is a limited development station profile, not a production-ready driver:

- WPA2-PSK with CCMP only. WEP, TKIP, WPA3/SAE, enterprise authentication,
  PMF, AP mode, roaming and power saving are not implemented.
- Scans are passive. Connection uses a recently observed BSS; hidden networks
  requiring directed probes are unsupported. Scan requests while connected
  return busy.
- TX currently uses a fixed 1 Mb/s legacy rate. HT, aggregation, fragmentation,
  A-MSDU and rate adaptation are not enabled. Throughput is not a release claim.
- Firmware or SDIO stream faults require a fresh boot. Warm initialization,
  radio power saving and long-duration operation remain unqualified.
- Deep-sleep resume retains radio power and permits station reassociation.
  Automatic WPA2 reassociation and interface-bound pings have passed; sustained
  traffic across sleep and repeated-cycle endurance remain unqualified.
- RSSI units are unqualified, so the driver does not invent signal-strength
  values. Some empty work invocations remain visible in diagnostic counters.

Qualification is specific to the artifacts and hardware recorded in the
validation record. Later builds and other devices do not inherit those results.

## Configure a station

Select the actual operating country using the build launcher's
`--wifi-country` option, or the installed Wi-Fi configuration. Do not infer a
country from an SSID. Keep the signed regulatory database and its signature
together; the kernel retains signature verification. The driver ignores AP
country-IE hints so they cannot override the operator-selected country.

Create a root-owned, mode-0600 file at
`/etc/wpa_supplicant/wpa_supplicant-wlan0.conf`. It needs
`ctrl_interface=/run/wpa_supplicant`, `passive_scan=1`, and a network block
containing the operator's SSID and PSK with `key_mgmt=WPA-PSK`,
`proto=RSN`, `pairwise=CCMP`, `group=CCMP` and `ieee80211w=0`.
Do not put network credentials in a public layer or build log.

Start the standard service after provisioning the file:

```sh
systemctl start wpa_supplicant@wlan0.service
wpa_cli -i wlan0 status
networkctl status wlan0
```

The packaged drop-in orders the supplicant after `FMWiFi.service`.
Systemd-networkd requests DHCP and accepts IPv6 router advertisements once the
link connects. Its Wi-Fi route metric is higher than wired Ethernet's, so wired
access remains preferred when both are available. No network profile is shipped
and the supplicant instance is not enabled automatically.

The current development root uses a volatile writable overlay. Files created
there, including a station profile, disappear on reboot. Persistent credential
provisioning belongs in the eventual userdata policy; it is not silently
embedded in the kernel or public systembase.

## Firmware and hardware

The BSP does not contain, fetch or redistribute the factory Wi-Fi firmware.
Supply the qualified stock `nk.bin` to the build launcher with `--stock-nk`.
The launcher extracts the named ROM file inside its pinned container and
verifies both stock-image and extracted-firmware hashes. Linux requests
`h432b/rtl8712s.bin`. See the supported input digest and build commands in
[openh432-build](https://github.com/highenergymagic/openh432-build).
USB firmware compatibility has not been established.

Keep factory images, extracted firmware, disassembly, raw device logs,
identifiers and credentials out of Git. Diagnostic text can expose private
firmware data and must not be treated as a publishable log.

The radio is SDIO `024c:8712`, function 1, class 07, on controller
`eb300000`, with 512-byte blocks. MMC host numbers are asynchronous.
Linux's [r8712u driver](https://github.com/torvalds/linux/blob/v6.12/drivers/staging/rtl8712/Kconfig)
is USB-only. The pinned
[Realtek source mirror](https://github.com/ronangaillard/rtl8712-driver-src/tree/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8)
has useful protocol definitions but omits referenced SDIO HAL files.
This implementation combines those definitions with factory-path analysis.

Function registers use byte-mode CMD53, including one-byte operations;
standard CCCR access uses the SDIO core. FIFO requests explicitly use block
mode. H2C command sequencing, C2H event sequencing and RX/C2H FIFO port
sequencing are separate.

## Packet and interrupt ownership

One ordered workqueue and owner mutex serialize command, receive and transmit
consumers. The native IRQ callback reads status before masking. Consumers drain
queues and verify the mask on re-arm. RX and C2H have separate cumulative block
counters and port sequences. Receive descriptors, optional driver information,
frame lengths and 512-byte record padding are validated.

Association uses the firmware's full-MAC join command. CCMP encryption and MIC
verification use Linux's synchronous CCM implementation, not an unqualified
firmware CAM offload. The host owns transmit PNs and per-key/per-TID replay
counters. Reinstalling identical key material preserves those counters.
Plaintext non-EAPOL traffic and ordinary traffic on an unauthorized port are
rejected. This is implementation scope, not an independent security audit.

The read-only `network_result` attribute reports fault, association,
authorization, interrupt and packet counters without payloads or identifiers.
A failed transfer is not blindly retried. Teardown cancels consumers before
releasing the IRQ and SDIO function.

## Optional diagnostics

The runtime is the primary implementation. `openh432-wifi-test` is a
diagnostic kernel bundle using the same driver, a root-handoff initramfs and
an installed slot-matched systembase. It is not a standalone recovery image. Build it through the pinned launcher only when that
specific diagnostic target is needed.

On a normal firmware-equipped installation, `FMWiFi.service` owns startup.
Do not replay low-level initialization writes after it starts. The root-only
experiment interface is excluded from the standard kernel. Build the explicit
`linux-h432b-wifi-test` target to enable `CONFIG_H432B_WIFI_DIAGNOSTICS`;
prevent automatic initialization on that boot with the kernel command-line
option `systemd.mask=FMWiFi.service`. Stopping
that service after initialization does not reset the chip or release hardware
ownership. The early userspace in
`openh432-wifi-test` still hands off to the installed systembase, so building
that target alone does not suppress its startup services.
Diagnostic register snapshots are also disabled in the standard kernel.
The diagnostic controls are:

| Write attribute | Request | Purpose |
| --- | --- | --- |
| `sample` | `1` | Initial CMD52/CMD53 comparison |
| `power_init` | `1` | Chip clocks and power |
| `firmware_load` | `memory` or `full` | Code-only or full firmware startup |
| `power_ack` | `1` | Active-state acknowledgement |
| `event_read` | `1` | Initial event batch |
| `command_test` | See below | Bounded command/scan diagnostic |
| `network_start` | `1` | Register the station interface |

Actions are one-shot per binding. Firmware `memory` and `full` are alternatives.
Event/command diagnostics and network ownership are mutually exclusive.
Check operation and cleanup errors; cleanup does not undo volatile chip setup.

### Command actions

| Action | Behavior |
| --- | --- |
| `opmode` | Sixteen infrastructure-mode requests, polling |
| `survey` | Two normal requests and one passive scan, polling |
| `opmode-irq` | Sixteen native-interrupt requests |
| `survey-irq` | Two normal requests and one interrupt-driven scan |
| `stress-irq` | 256 requests across sequence wrap |
| `survey-repeat-irq` | Two normal requests and three passive scans |
| `loopback` | Historical first-reply/second-timeout failure, not acceptance |

Normal-command replies in these diagnostics are exact factory debug events,
not generic protocol acknowledgements. An intermittent diagnostic error-report
value `08` remains unexplained; it was also observed in successful scans.

## Tests

```sh
python3 -m unittest discover -s tests
```

The tests check source and metadata contracts; they do not emulate the radio.
With `WIFI_RX_NATIVE_CC` set to a compiler inside the pinned build container,
additional tests execute the actual C framing, RX parser, CCMP nonce/AAD and
BSS-cache selection code. Compilation, deterministic artifacts, hardware
qualification and an independent security review are distinct claims.
