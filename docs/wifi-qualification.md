# Wi-Fi validation record

## Scope

This record covers the station profile in the [Wi-Fi reference](wifi.md),
tested on one H432B with operator-supplied radio firmware. Each result applies
only to its listed artifacts. It does not qualify other devices, arbitrary
firmware, all access points or later source revisions.

## Rate-adapting NAND station profile

| Artifact | SHA-256 |
| --- | --- |
| Kernel bundle | `f2971915eea2e03eac165a84fe6c5213f95789ba4530699743d7f04a58f60dfb` |
| OpenEVV systembase, including radio module and supplicant | `40294feb9e9c4ad6db2639fe11cc33c62482b7efa029ace6ab3b73faf1a1e0f1` |

Both images were installed in inactive slot A and passed full NAND readback.
The existing slot B and factory boot regions were preserved. The normal NAND
boot loaded `rtl8712s` automatically and created `wlan0`; no temporary module,
manual initialization control or replacement supplicant was used.

| Test | Result |
| --- | --- |
| Station setup | Shipped supplicant, passive discovery, WPA2-PSK/CCMP, DHCP, NZ regulatory policy |
| TCP download / upload | 16 MiB each way, matching SHA-256; 10.13 / 9.24 Mbps |
| Explicit disconnect / reconnect | New association followed by 4 MiB each way, matching SHA-256; 10.41 / 9.03 Mbps |
| Interface down, module removal / reload | Fresh association followed by 4 MiB each way, matching SHA-256; 10.49 / 9.32 Mbps |
| Receive capabilities | 17,109 MIC-authenticated HT frames, including MCS15; 123 authenticated A-MSDU frames |
| Health through reconnect | No kernel taint, MIC errors, reorder drops or firmware recovery attempts; one failed-transmit counter increment |

Transfers used RAM buffers and sockets explicitly bound to `wlan0`. Ethernet
remained available for management; the throughput figures exclude storage and
SSH encryption. Replay rejection and unsupported-frame counters were nonzero.
The failed-transmit counter was sampled across the deliberate disconnect, so
the record does not claim error-free transmission. There was also one failed
Ethernet SSH diagnostic handshake; subsequent management sessions succeeded.

These measurements establish working rate-adapting traffic, not a measured
transmit PHY rate or the chipset's maximum throughput. Active supplicant scans,
hidden-network interoperability, full fault recovery, suspend/resume with this
image and long-duration reliability remain unqualified. This image was built
with pinned-container local layers; these tests do not establish a new
independent clean-cache or cross-host reproduction.

Native tests cover sequence wrap independently of traffic priority, malformed
BSS reports, descriptor fields, reordering and module cleanup contracts.
All 21 Wi-Fi C/header files passed the pinned kernel's `checkpatch.pl --file`
with no errors or warnings. Style checks are not an upstream acceptance or
security-audit claim.

## Historical basic-rate station profile

The following artifacts predate firmware TX rate adaptation and HT receive
support. Their results do not qualify the current driver or its new features.

| Artifact | SHA-256 |
| --- | --- |
| Kernel bundle | `3285150959a3013ead008552daca86bc4eb2545c63ff74264c089840b645e495` |
| Separate systembase | `5572f5e057d78635dae5ffbbaf51ddc0a44882bb5b6800e05c61550b7f8ebe22` |

The source composition is recorded in the
[build validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md#wi-fi).
Slot-B images passed full readback verification; slot A and factory boot
regions were preserved. No bootloader replacement was part of these tests.

### Results

| Test | Result |
| --- | --- |
| Normal NAND boot | Automatic radio initialization on two boots |
| Station setup | Standard supplicant passive scan, WPA2-PSK/CCMP handshake and DHCP |
| Regulatory policy | Configured NZ domain retained despite a conflicting AP country IE |
| Wi-Fi-only TCP | Two 2,689,160-byte SSH downloads and one upload matched source SHA-256 |
| Reconnection | New handshake and Internet pings after explicit disconnect/reconnect |
| Extended connectivity | 60 interface-bound Internet pings without loss |
| Driver health | No service fault, CCMP MIC failure, TX failure or kernel taint |

Ethernet was disabled for data-transfer checks. Credentials were provisioned
again after reboot because the overlay is volatile. Receive batches reached
68 blocks without overflow. Replay/duplicate frames were rejected; that counter
need not be zero. Successful traffic is not an independent security audit.

### Component and build coverage

Supporting hardware tests exercised CMD53 register/block transport, firmware
startup, native event delivery, sequence wrap and repeated passive scans.
A 256-command test passed exact replies across wrap. Interface down/up,
cancellation and unbind were tested; a faulted stream requires fresh
initialization rather than blind request replay.

Pinned-container native C tests cover RX bounds/padding and CRC/ICV flags,
H2C framing, CCMP nonce/AAD construction and BSS-cache selection.
Source-contract tests do not emulate the radio.

A pinned-public-source rebuild without local-layer overrides matched both
artifacts above on the same host with existing caches and the same private
firmware input. This establishes committed-input coverage, not an independent
clean-cache or cross-host reproduction.

### Limits of this profile

TX uses a fixed 1 Mb/s legacy rate. Warm recovery, suspend, power saving,
long-duration reliability, roaming, WPA3, enterprise authentication and PMF
are unqualified. RSSI units are unqualified. Empty firmware-event notifications
and intermittent diagnostic error-report value `08` remain under investigation;
the latter also occurred in successful scans.

The optional loopback diagnostic has not passed repeated command acceptance.
It is not the acceptance criterion for the station interface. Superseded
implementation narratives and intermediate artifact hashes remain in Git history.
