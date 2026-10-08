# GPS receiver

## Availability

The normal NAND runtime includes UART and power support for the GlobalTop
Ivory 3 GMM-U2P (MediaTek MT3339). Systembase includes local-only gpsd and
optional RAM assistance. Transport, firmware queries and assistance
acknowledgements have been tested; a navigation fix and acquisition-time
improvement have not been demonstrated.

## Hardware configuration

| Resource | Configuration |
| --- | --- |
| UART | S5PV210 UART1, physical 0xe2900400 |
| Serial format | 9600 baud, 8N1; no flow control |
| RX / TX | GPA0[4] / GPA0[5], function 2, pulls disabled |
| Enable | GPE1[0], active high |
| Reset | GPH3[3], active low |

The shared `h432b-gps-power` sequencer asserts reset, enables the supply,
waits 100 ms and releases reset. Removal asserts reset and disables the
supply. It performs no PMIC transaction and leaves the receiver powered while
bound. NMEA reception after runtime deep sleep has passed. Inactivity power saving, assistance retention and automatic
re-aiding after resume remain unqualified.
The UART is not a console.

The receiver's PMTK705 reply identifies firmware
`AXN_2.31_3339_13082100`, build `5464`, product `Gmm-u2p`, version
`1.0`. This and the [manufacturer description](https://www.gtop-tech.com/en/product/Ivory-3-GMM-U2P/MT3339_GPS_Module_01.html)
identify the module; no package inspection is required.

## Linux interface

The OS policy identifies the UART by physical address and supplies
`/dev/openh432-gps`. gpsd and the assistance uploader share exclusive
ownership; do not open or configure the tty concurrently.

See [GPS service configuration](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/gps.md)
for service ordering, cache paths, endpoint policy and time validity.
GPS reports can contain location and time. Keep diagnostic captures private.

## Assistance protocol

The qualified path supplies UTC through PMTK740 and the current six-hour
GPS-only EPO set through PMTK721. Each nonzero satellite record requires a
checksum-valid success acknowledgement. No reference position is invented,
and receiver flash is not written. PMTK607 reports stored EPO, not RAM
host-aiding readback; its count need not increase after RAM assistance.

The 72-byte record layout is described in the
[Quectel AGNSS note](https://forums.quectel.com/uploads/short-url/gh4kD8zTLOYZN4L0k5y4NepN1Jm.pdf)
and [Sierra Wireless aiding note](https://www.blitzortung.org/Compendium/Hardware/GlobalTop/mendip_defender/AirPrime_GMM_G3_XA11xx_and_XM11xx_GNSS_Aiding_Application_Note_Rev2_2.pdf).
These documents do not imply support for every vendor command on this module.

The upstream data source is MediaTek EPO.DAT with a companion MD5 value.
Clients use only the proxy endpoints configured by the OS layer; there is no
direct-source fallback. Public availability does not establish redistribution
rights or an upstream service guarantee. Prediction data are runtime cache
contents, not build inputs or bundled assets. TLS authenticates the transport;
MD5 checks file/sidecar consistency, not publisher authenticity.

## Diagnostics

The optional `linux-h432b-gps-test` / `openh432-gps-test` profile adds
explicit diagnostic markers and uses the installed read-only slot-B root.
It shares receiver sequencing with the runtime kernel.

- `tests/capture-gps.sh --receive-30s`: receive-only NMEA capture.
- `tests/capture-gps-startup.sh`: bounded capture across a GPS-driver power cycle.
  Stage `capture-gps.sh` at `/tmp/openh432-stage/capture-gps.sh` first.
- `tests/query-gps-version.sh --query-version`: one PMTK605 request and reply check.

These scripts verify their diagnostic prerequisites and reject a console
UART. Do not bypass their profile checks on the normal system. The host
tools repository provides offline checksum, EPO-layout and test-script
preparation utilities. Generated assistance tests have time-limited inputs
and are not production services.

## Validation limits

NMEA reception and firmware replies passed checksum checks at 9600 baud.
A complete set of 31 nonzero satellite records and UTC assistance received
success acknowledgements, with valid NMEA reception afterward. The normal
NAND system also passed local gpsd reporting, proxy refresh and service-managed
upload. Concurrent uploader access was rejected without UART transmission.

These results establish transport and service integration, not RF sensitivity,
navigation accuracy, a satellite fix, faster time to first fix or receiver
power-management behavior.
