# Wi-Fi validation record

## Scope and artifacts

This record covers the limited station profile in the [Wi-Fi reference](wifi.md),
tested on one H432B with operator-supplied radio firmware. It does not qualify
other devices, arbitrary firmware, all access points or later source revisions.

| Artifact | SHA-256 |
| --- | --- |
| Kernel bundle | `3285150959a3013ead008552daca86bc4eb2545c63ff74264c089840b645e495` |
| Separate systembase | `5572f5e057d78635dae5ffbbaf51ddc0a44882bb5b6800e05c61550b7f8ebe22` |

The source composition is recorded in the
[build validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md#wi-fi).
Slot-B images passed full readback verification; slot A and factory boot
regions were preserved. No bootloader replacement was part of these tests.

## Results

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

## Component and build coverage

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

## Limits

TX uses a fixed 1 Mb/s legacy rate. Warm recovery, suspend, power saving,
long-duration reliability, roaming, WPA3, enterprise authentication and PMF
are unqualified. RSSI units are unqualified. Empty firmware-event notifications
and intermittent diagnostic error-report value `08` remain under investigation;
the latter also occurred in successful scans.

The optional loopback diagnostic has not passed repeated command acceptance.
It is not the acceptance criterion for the station interface. Superseded
implementation narratives and intermediate artifact hashes remain in Git history.
