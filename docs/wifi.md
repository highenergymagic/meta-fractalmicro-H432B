# Internal Wi-Fi

## Interface and support

The RTL8712S driver provides a standard cfg80211 station interface, `wlan0`.
It uses the SDIO function's device-tree profile to select the H432B module's
firmware and RF configuration. It is independent of the MMC host-controller
driver and is not interchangeable with the USB-only `r8712u` driver.

| Function | Implementation |
| --- | --- |
| Station authentication | WPA2-Personal with CCMP; standard `wpa_supplicant` |
| Scanning | Passive supplicant discovery; driver-level wildcard-active and single-SSID directed requests |
| Legacy rates | Firmware-selected 802.11b/g rates; basic-rate handling for control traffic |
| HT | 20 MHz, one TX/two RX spatial streams, QoS, Block Ack receive reordering and A-MSDU reception |
| Recovery | Bounded firmware reload after transport faults; explicit interface down/up retry |
| Power management | System suspend with station disconnection and reassociation on resume |
| Statistics | Standard link information and `ethtool -S` software counters |

Implementation is not a hardware qualification claim. Tested artifacts,
measurements and remaining qualification work are recorded separately in the
[Wi-Fi validation record](wifi-qualification.md).

Scans while associated, roaming, AP mode, WEP, TKIP, WPA3/SAE, enterprise
authentication, PMF, 40 MHz channels, a second transmit stream and radio power
saving are not supported. RSSI calibration and transmit completion-rate
telemetry are not established; the driver does not invent those values.
The chip's nominal PHY rate is not an application-throughput guarantee.

## Installation and startup

A firmware-equipped standard image installs the `rtl8712s` kernel module and
its firmware on systembase. Normal SDIO discovery loads the module after root
handoff. Probe validates the firmware, initializes the radio and registers the
interface; no manual initialization service or sysfs request is required.
Firmware-free images omit both the radio module package and proprietary input.
Building images never accesses or flashes hardware.

The systembase provides `iw`, `wpa_supplicant`, `wpa_cli` and the signed
regulatory database. `FMRegulatory.service` applies the configured operating
country before the station service starts. No network credentials are shipped,
and the supplicant instance is not automatically enabled.

## Configure a station

Select the actual operating country with the build launcher's `--wifi-country`
option or the installed regulatory configuration. Do not infer it from an SSID.
Keep the regulatory database and signature together; signature verification
remains enabled. AP country-IE hints cannot override the selected country.

Create a root-owned, mode-0600 file at
`/etc/wpa_supplicant/wpa_supplicant-wlan0.conf`. Set
`ctrl_interface=/run/wpa_supplicant` and `passive_scan=1`, then configure the network with the
operator's SSID and PSK, `key_mgmt=WPA-PSK`, `proto=RSN`, `pairwise=CCMP`,
`group=CCMP` and `ieee80211w=0`. The supported supplicant configuration uses
passive discovery. Do not publish credentials in a layer or build log.

```sh
systemctl start wpa_supplicant@wlan0.service
wpa_cli -i wlan0 status
networkctl status wlan0
```

Active probes are sent only on channels allowed to initiate radiation by the
regulatory core. Channels 12–13 are always scanned passively because the
supplied firmware suppresses probes there, including in countries that permit
them. Hidden-network discovery on those channels is consequently limited.
Additional scan-request IEs are not supported. The supplicant integration
omits probe-request IEs for passive scans, which transmit no probes. Active
supplicant discovery with extra IEs and hidden-network interoperability remain
unqualified; accepting a driver-level directed request does not qualify them.

Systemd-networkd requests DHCP and accepts IPv6 router advertisements after
association. Its Wi-Fi route metric is higher than Ethernet's, so wired access
is preferred when both are connected. The development image's writable overlay
is volatile: station profiles created there do not survive reboot. Persistent
credential provisioning requires a userdata policy.

## Firmware and device profile

The BSP does not contain, fetch or redistribute factory Wi-Fi firmware.
Supply the qualified stock `nk.bin` to the build launcher with `--stock-nk`.
Extraction runs in the pinned container and verifies both stock-image and
extracted-firmware hashes. Linux requests `h432b/rtl8712s.bin`; supported input
digests and build commands are documented in
[openh432-build](https://github.com/highenergymagic/openh432-build).

The module is SDIO `024c:8712`, function 1, on controller `eb300000`, using
512-byte blocks. MMC host numbers are assigned asynchronously. The
`hims,h432b-rtl8712s` compatible identifies the board module's RF topology,
firmware configuration and calibration assumptions; the SDIO ID alone does not
identify those properties. Supplies and bus pin control belong to the parent
MMC controller. USB firmware compatibility has not been established.

The factory 1T2R RF profile supports receive MCS 0–15 and transmit MCS 0–7.
Firmware constructs its own association MCS bitmap from that profile, rather
than preserving the host's JoinBss bitmap. The driver's HT20 policy therefore
also disables the firmware's bandwidth-enable input; clearing the host's
40 MHz capability alone would not constrain the on-air association.

Protocol definitions are supported by the pinned
[Realtek GPL source](https://github.com/ronangaillard/rtl8712-driver-src/tree/2237e98dacd8421b38beb2d1aad88aa2b9f79dd8)
and analysis of the factory SDIO implementation. Keep stock images, extracted
firmware, disassembly, raw device logs, identifiers and credentials out of Git.

## Data path and recovery

An ordered workqueue and owner mutex serialize configuration, command, receive
and transmit state. Standard CCCR operations use the SDIO core. Chip registers
use byte-mode CMD53 and FIFO transfers use block-mode CMD53. H2C command,
C2H event and FIFO-port sequencing are independent.

QoS descriptors carry the packet's 12-bit sequence number separately from its
traffic identifier and queue selection. The selected SDIO firmware operation
mode uses host-supplied QoS sequences; the USB driver's priority-as-sequence
convention does not apply. Normal traffic uses firmware rate adaptation, not a
fixed basic rate.

Linux's synchronous CCM implementation performs CCMP encryption and MIC
verification; firmware CAM offload is not assumed. Authentication precedes
receive reorder admission, and the replay check is repeated at delivery.
Replay state is separate for each key and QoS TID, including non-QoS traffic.
Reinstalling identical key material preserves packet numbers; changing or
deleting a key discards queued plaintext. A-MSDU conversion uses cfg80211's
frame validation helper. Plaintext non-EAPOL traffic and ordinary traffic on
an unauthorized port are rejected.

Known transport or stream faults disconnect the station and schedule a full
firmware restart. Recovery stops packet work and IRQ ownership before replaying
the chip's SDIO shutdown and initialization sequence. Automatic recovery is
limited to three attempts per administrative interface-up cycle. Allocation
failures and transmit congestion do not cause automatic radio resets. A failed
packet is never blindly retransmitted after uncertain FIFO acceptance.
Malformed individual BSS reports are counted and discarded without aborting
the enclosing scan. Invalid C2H stream framing remains a transport error.

After correcting a persistent fault, request another attempt using standard
administrative control:

```sh
ip link set wlan0 down
ip link set wlan0 up
```

Removing the module drains work, disconnects cfg80211, unregisters the LEDs and
interface, and shuts the radio down. Reloading performs a fresh initialization.
Only do this when the wireless interface is not the management connection.
Recovery and reload qualification is artifact-specific.

## Observability

```sh
iw dev wlan0 link
ethtool -S wlan0
journalctl -k
```

`rx_ht_authenticated`, `rx_ht_mcs0` through `rx_ht_mcs15`, and
`rx_amsdu_authenticated` count MIC-verified frames, not successful application
delivery. Subsequent reorder, replay or aggregate validation can still reject
them. `rx_addba_reports` counts accepted firmware receive-agreement reports;
`rx_ba_tid_mask` and `tx_addba_tid_mask` show current host agreement/request
state. `ht_requested` and `qos_requested` describe the requested join profile,
not proof of over-the-air negotiation or acknowledged transmit rates.

Replay, MIC, malformed-frame, transmit-failure and recovery counters expose
operational failures without packet contents or identifiers. Statistics are
also available for discarded survey records (`scan_reports_dropped`). These
include malformed BSS data and failed BSS-cache allocations. Statistics are
software snapshots and can be read while the interface is down. Initialization
and transport errors remain visible in the kernel log; routine packet and
register tracing is not enabled in the standard image.

## Diagnostic profile and tests

`openh432-wifi-test` is an explicit development kernel bundle using the same
driver and an installed slot-matched systembase. It is not a standalone
recovery image. Its built-in `CONFIG_H432B_WIFI_DIAGNOSTICS` profile exposes
manual transport, power, firmware and command experiments; normal images do not
expose these controls. The diagnostic driver does not initialize automatically.
Its `initialize` operation performs normal startup when firmware is available.

Low-level experiment actions are mutually exclusive with network ownership.
They can change chip state, and cleanup does not reverse every volatile write.
Do not replay register or firmware experiments on an active station. Exact
factory debug-message matching is confined to this diagnostic profile and is
not a dependency of normal scans or association.

### Command actions

The diagnostic-only `command_test` control accepts these actions:

| Action | Behavior |
| --- | --- |
| `opmode` | Sixteen infrastructure-mode requests, polling |
| `survey` | Two normal requests and one passive scan, polling |
| `opmode-irq` | Sixteen native-interrupt requests |
| `survey-irq` | Two normal requests and one interrupt-driven scan |
| `stress-irq` | 256 requests across sequence wrap |
| `survey-repeat-irq` | Two normal requests and three passive scans |
| `loopback` | Firmware loopback experiment; not a station acceptance test |

Normal-command replies in these experiments are exact factory debug events,
not generic protocol acknowledgements.

### Native tests

Run tests through the pinned build environment. With `WIFI_RX_NATIVE_CC`
configured there, native tests execute the actual framing, bounds validation,
CCMP, QoS, reordering, key-lifetime, scan-planning, recovery and statistics
helpers. Fault-injection tests exercise software cleanup contracts; they do not
replace radio, module-lifecycle, regulatory or interoperability testing.
