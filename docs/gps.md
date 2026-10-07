# GPS receive bring-up

## Recovered hardware mapping

The factory registry's `Serial1` device is exposed as `COM2:` and maps
to physical address `0xe2900400`: S5PV210 UART1. Its device-array index is 1
and CE stream index is 2. Application evidence identifies 9600 baud; the
registry's generic serial defaults alone are not the application's settings.

The board-control driver's GPS enable sets GPE1[0] high, and disables it by
clearing that bit. Its initializer configures that pin as an output with
pulls disabled. A separate GPS reset action drives GPH3[3] low for 100 ms, then high.
The recovered sequence does not require a PMIC transaction.

## Diagnostic image

`linux-h432b-gps-test` inherits the read-only external-SD diagnostic profile.
It enables UART1 receive on GPA0[4] and, in the query-capable revision,
transmit on GPA0[5]. Both use UART function 2 with pulls disabled.
An opt-in board-power diagnostic driver asserts reset, enables a GPIO-backed
supply, waits 100 ms and releases reset. On driver removal it asserts reset
and disables its supply. No unmeasured voltage or receiver model is specified. Flow-control pins are not configured, and the UART is not a console.
Enabling TX does not automatically send commands.

This boot-time diagnostic is not a production GNSS power-management
driver. Suspend behavior and navigation accuracy remain to be qualified. A diagnostic driver unbind/rebind power cycle has
been tested as described below.

`openh432-gps-test` is a RAM-boot image using the installed read-only NAND
root. It does not install firmware or change either SD card.

## Receive test

Run `tests/capture-gps.sh --receive-30s` only on the opt-in image. It resolves
the tty by physical UART address, refuses a console UART, selects 9600 baud,
8 data bits, no parity, one stop bit, and disables echo and flow control.
It captures for 30 seconds without sending GPS commands. The UART is left
in raw mode with echo disabled.

Raw output is retained in RAM and sent to the diagnostic console. It may
contain location and time, so keep captures private. The companion
`openh432-tools/scripts/check-gps-capture.py` validates NMEA XOR checksums
and prints only sentence counts/types and a count of fix-indicating
sentences. A valid stream without a fix still establishes receiver
communication; it does not establish working navigation.

The initial enable-only image received zero bytes during a 30-second
capture (UART TX and RX counters both zero). The reset-enabled image subsequently received 126 checksum-valid NMEA
sentences in 30 seconds at 9600 baud, with zero checksum failures: 30 each
of GPGGA, GPGSA, GPRMC and GPVTG, plus six GPGSV sentences. UART counters
reported 4,758 received bytes and zero transmitted bytes.

This qualifies the recovered power/reset path and receive transport. The
capture reported no position fix and zero satellites in view, so RF reception,
antenna performance and navigation accuracy remain unqualified. The kernel stayed untainted, NAND remained read-only, and no
systemd services failed. No GPS configuration command was transmitted.

The pinned image build, 112 hardware-layer source tests and the host-tool
container test suite passed. This diagnostic image has not yet undergone a
fresh cross-host reproducibility comparison. Installed NAND firmware and
the SD cards were unchanged; the GPS remains powered in the RAM diagnostic
image.

## Receiver identification and assisted GPS

Capturing UART output while cycling only the diagnostic GPS power driver
produced the startup messages `PMTK011,MTKGPS`, `PMTK010,001`, and
`PMTK010,002`. This confirms the MediaTek/PMTK receiver family, not an
exact chip or module model. The capture contained 118 checksum-valid
sentences and zero checksum failures; no navigation fix was reported.

The manufacturer-authored [Quectel MC60 GNSS protocol specification](https://www.quectel.com/content/uploads/2024/04/Quectel_MC60_Series_GNSS_Protocol_Specification_V1.1.pdf),
sections 4.1 and 4.2, documents the same startup messages and identifies
system message 002 as a host EPO assistance notification. This is evidence
of an advertised assistance path. It does not establish that this board
uses an MC60 module, that every command in that document is supported, or
that an assistance upload has succeeded.

EPO supplies predicted satellite-orbit data to help acquisition. Assistance
can reduce acquisition time but cannot replace adequate satellite signal
reception. Compatible data format and fresh assistance-data availability remain
prerequisites for an upload test. Firmware identification is recorded below.
The documented PMTK605 firmware-release query (PMTK705 reply) is a useful
next identification step, and is implemented by the explicit query test below.

An initial string inspection of the factory GPS application binaries found
COM2 and baud-rate handling but no explicit PMTK assistance commands. This
is not a complete code audit and does not prove the original software
lacked assistance.

For a startup capture, stage `tests/capture-gps.sh` at
`/tmp/openh432-stage/capture-gps.sh`, then run
`tests/capture-gps-startup.sh` on the opt-in diagnostic image. It starts
reception before unbinding/rebinding only the GPS power driver and attempts
to restore the binding on exit. Captures can contain location and time;
keep them private. This changes GPS power state, not stored firmware.

## Explicit firmware query

The query-capable RAM diagnostic adds `hims,gps-query-test` and muxes
UART1 TX. Run `tests/query-gps-version.sh --query-version` explicitly;
nothing sends this query automatically on boot. The script verifies the
diagnostic marker, read-only NAND and untainted kernel, resolves the UART
by physical address, rejects a console UART and starts a bounded receive
capture before sending one `$PMTK605*31` request with CRLF termination.
It does not reset the receiver, change configuration or upload assistance.
Keep the captured NMEA private. A checksum-valid PMTK705 reply is required
to qualify the command/response transport.

## Firmware-query qualification

The transmit-enabled RAM image returned one checksum-valid PMTK705 reply:

- Firmware: `AXN_2.31_3339_13082100`
- Build ID: `5464`
- Product: `Gmm-u2p`
- Additional version field: `1.0`

The receiver identifies as a GlobalTop Ivory 3 (GMM-U2P). The
[manufacturer product page](https://www.gtop-tech.com/en/product/Ivory-3-GMM-U2P/MT3339_GPS_Module_01.html)
specifies the MediaTek MT3339 chipset and offline EPO assisted-GPS support.
This identification is based on the live firmware reply and matching vendor
documentation, not inspection of the physical package.

The single query transmitted exactly 13 bytes. The bounded capture
contained 43 valid sentences, including the firmware reply, and no checksum
failures. No position fix was obtained. Post-test UART counters were
TX 13 / RX 1639, kernel taint was zero, NAND UBI remained read-only and no
systemd services failed. No configuration, restart or assistance-upload
command was sent.

The pinned image build, 114 hardware-layer source tests and pinned host-tool
test suite (including firmware-reply parsing) passed. This revision has not
undergone a fresh cross-host bit-for-bit comparison. Installed NAND and SD
contents were not modified. RAM host-aiding acceptance was subsequently qualified below. Aided
acquisition and receiver-flash assistance remain unqualified.

## Assistance data source

The first-party source candidate is
[MediaTek EPO.DAT](https://epodownload.mediatek.com/EPO.DAT), with
[EPO.MD5](https://epodownload.mediatek.com/EPO.MD5) as its companion integrity
check. Direct HTTPS retrieval with normal certificate validation succeeded
on 2026-10-06, without credentials, a device identifier or location.
No redirect or insecure fallback was needed. Public availability is not a
service-level commitment or a redistribution license; neither has been
established for this endpoint. Do not bundle or mirror the data by default.

The privately retained sample was 276480 bytes: 120 six-hour GPS-only sets,
32 records per set, 72 bytes per record. Its companion MD5 matched. All
record timestamps followed the expected six-hour sequence, covering
2026-10-06 00:00 through 2026-11-05 00:00 in GPS calendar time (not UTC).
There were 337 zero-ID records; all remaining IDs matched their satellite
slots. Zero-ID handling must be established before upload, not guessed.
Structural consistency and a matching MD5 do not establish acceptance by
the installed 2013 firmware or validate every internal record checksum.

The GPS-only 72-byte layout is documented in the manufacturer-authored
[Quectel AGNSS note, sections 2.2 and 3.2](https://forums.quectel.com/uploads/short-url/gh4kD8zTLOYZN4L0k5y4NepN1Jm.pdf)
and [Sierra Wireless GNSS aiding note, section 5.2](https://www.blitzortung.org/Compendium/Hardware/GlobalTop/mendip_defender/AirPrime_GMM_G3_XA11xx_and_XM11xx_GNSS_Aiding_Application_Note_Rev2_2.pdf).
Those references establish the format, not universal command compatibility
with every MediaTek firmware. Do not confuse this data with the older
60-byte record format, or use newer vendor/project credentials from another
manufacturer's examples.

The offline `openh432-tools/scripts/inspect-epo.py` checks bounded file size,
set layout, timestamps and nonzero satellite-slot IDs. It reports GPS times
without pretending they are UTC, and explicitly reports upload qualification
as false. It does not open a receiver or perform network requests.

Service design requirements (implemented in the OS layer; qualification below):

- Download over verified HTTPS into a bounded temporary file; reject
  redirects/downgrades, unexpected format, expired data and implausible dates.
- Verify the companion integrity value, but rely on TLS rather than MD5 for
  transport authentication. Handle an upstream file/checksum update race
  with a bounded retry and retain the previous valid cache.
- Validate trusted time and select the current six-hour set if the firmware's
  RAM host-aiding path is qualified. Do not assume the whole 30-day file fits
  receiver flash.
- Separate background cache refresh from explicit receiver activation.
  GPS startup must not depend on network availability.
- Give the uploader exclusive UART ownership before gpsd, including when
  gpsd is socket-activated; use timeouts and verified NMEA restoration.
- Keep predicted data out of build inputs and Git. Runtime refresh must not
  change the reproducibility of BSP images.

No EPO data was sent during source qualification. The subsequent receiver
test below qualifies RAM host-aiding acceptance. Automatic refresh/upload
units are now packaged and RAM-overlay tested; they are not installed in NAND.

## Client privacy endpoint

Clients must use the operator-controlled endpoints:

- https://gpsdata-proxy-openh432.highenergymagic.net/EPO.DAT
- https://gpsdata-proxy-openh432.highenergymagic.net/EPO.MD5

Both were retrieved from Carbon with normal HTTPS certificate validation
and no redirect following on 2026-10-06. The checksum matched and the data
matched the previously inspected 276480-byte upstream sample.

Do not fall back to MediaTek or another direct data source on failure.
Use still-valid cached data or continue without assistance. The upstream
URLs above document provenance and proxy operation, not client fallbacks.
The packaged client downloader rejects redirects.

This verifies endpoint reachability and content integrity, not the proxy's
outgoing-header privacy. That requires server-side configuration inspection
or a controlled upstream header-capture test; response headers alone cannot
prove that client IP headers were removed. The proxy operator can still see
client connection IPs. Automatic refresh/upload is packaged in the OS layer.

## RAM host-aiding qualification

On firmware `AXN_2.31_3339_13082100`, a fixed PMTK607 query initially
reported zero stored EPO sets. A single current satellite record sent using
PMTK721 returned a checksum-valid `PMTK001,721,3` success acknowledgement.
PMTK740 UTC assistance also returned success after a recent successful
systemd-timesyncd synchronization was verified. No reference position was
invented or transmitted.

A complete current six-hour set then supplied 31 nonzero-ID satellite records,
paced one second apart. All 31 received success acknowledgements; the
45-second capture contained 220 valid NMEA/PMTK frames and no checksum
failures. The zero-ID record was omitted, not rewritten. This establishes
acceptance of time and RAM host-EPO assistance on the installed firmware,
not improved time to first fix: there was still no navigation fix.

The offline host tool `prepare-host-epo-test.py` prepares this bounded test.
It verifies the supplied input SHA256, full-file record XOR consistency,
record layout and current set coverage before producing a target script.
It never downloads data or opens hardware. Its explicit GPS/UTC offset must
be independently current; the diagnostic does not provide a maintained leap
second database. Generated scripts expire and must be staged into target
RAM when they exceed the console's command-length limit. They are test
artifacts, not production services.

The receiver stayed at 9600 baud in NMEA mode. No receiver flash upload,
EPO erase, restart command, NAND write or SD write was performed.
PMTK607 is a stored-EPO query, not a readback proof for the RAM host-aiding
path; do not require its count to increase for this test.

The OS layer now packages gpsd and bounded refresh/upload services, with
exclusive UART ownership and per-packet acknowledgements. The initial
time-trust/expiry policy is deliberately limited; maintained leap-second
policy and receiver power-lifecycle handling remain future work.
No RF-fix or TTFF claim is made.

Post-upload PMTK707 retained a stored-set count of zero but changed its
current-use time fields from zero to nonzero values. This is additional
receiver-state evidence, not a byte-for-byte readback of the supplied
satellite records. Continued NMEA reception passed checksum validation;
the kernel remained untainted, NAND UBI remained read-only and no systemd
services failed. All 115 hardware-layer source tests and the pinned
host-tool test suite passed. No image rebuild was needed for these
RAM-script tests.

## Base userspace service integration

The pinned base-image build includes gpsd 3.27.5 and the OS-layer AGPS
package. Its packaged services passed volatile-overlay tests on the GPS
kernel profile: empty-cache startup, verified HTTPS refresh through the
privacy proxy, a complete 31-satellite acknowledged upload, local gpsd
reports, and lock-based refusal of concurrent aiding without UART writes.
NAND was unchanged during that test.

The `linux-h432b-runtime` NAND recipe inherits the historical reboot support and
includes the shared `s5pv210-hims-u2-gps.dtsi` and `h432b-gps-power`
sequencer. The GPS diagnostic recipe inherits those same components and
adds only explicit diagnostic permission markers. Normal runtime trees do
not grant those permissions. The runtime tree enables writable Linux UBI and
internal SD directly, without importing diagnostic permission markers.
Receiver PM is not introduced. The enable rail remains on
while the driver is bound; suspend/resume and inactivity power policy
remain unqualified. Installation/boot results must be recorded separately.

Service configuration and current limitations are documented in
[the OS layer GPS guide](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/gps.md).

The normal NAND runtime pair has passed installed-image readback and software
reboot testing. The final default-build kernel was updated directly from
NAND-root Linux while its kernel volume was unmounted, then booted from NAND.
gpsd returned local receiver reports; UBI and internal SD were writable, the
kernel was untainted, and no services were failed. This does not establish
suspend/resume, a satellite fix, or new cross-host image reproducibility.
