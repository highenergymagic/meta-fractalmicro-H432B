# H432B test suite

Offline checks for BSP metadata, image layouts and selected driver logic.
Live device scripts are separate operator tools and are not run by this suite.

## Run offline checks

From the layer root:

```sh
python3 -m unittest discover -s tests -v
```

For compiled C vectors, run the tests inside the pinned build container with
`WIFI_RX_NATIVE_CC=/usr/bin/gcc`. This setting selects the compiler for Wi-Fi
vectors, LED register operations, and audio clock/gating tests; those compiled
tests are skipped when it is absent. Audio patch-stack checks also require
`patch`. Bootstate C vectors use `BOOTSTATE_NATIVE_CC` (or `cc` when available).

## Coverage

| Check | Scope |
| --- | --- |
| Boot state | CRC/schema checks, serial wrap, alternate-copy commit/readback failure, exhaustion, stale success and NAND write boundaries |
| NAND loader timing | Hardware-tick counter phases, bounded reporting and retained I/O/check operations |
| Fixed BCH | Build-time generic/fixed parity and decoding comparison using fetched upstream source |
| Audio | Positive ALSA constraint results, clock references, zero-fuzz patch application, jack polarity and compiled speaker-gating callbacks |
| Suspend integration | Runtime recipe, wake-source, display-power and peripheral-retention contracts |
| Source contracts | Recipe relationships, protocol constants, bounds and cleanup structure |
| Wi-Fi receive vectors | Actual C parser: padding, lengths, truncation and CRC/ICV flags |
| Wi-Fi station vectors | H2C framing, CCMP nonce/header construction and BSS-cache selection |
| Runtime Wi-Fi initialization | Firmware validation ordering, idempotence and separation of optional diagnostics |
| LED outputs | Compiled register writes, nibble preservation and error handling |
| Motion sensor | Runtime integration, factory-parameter conversion and power-management contracts |

The A/B loader build runs `check-bch-equivalence.py` with its pinned native
compiler. It compiles the fetched BCH library twice (generic and M=13, T=8),
then compares 8,448 deterministic corruption cases. The source-only unit suite
checks the recipe contract; it does not substitute for that build-time test.

Bootstate torn-write vectors model interrupted payloads and UBI update markers;
they are not physical power-cut qualification.

The crypto-provider stub verifies framing and error propagation, not
AES/CCM correctness or security. Offline tests do not exercise electrical
interfaces or establish image reproducibility.

The build repository's x86-64 and ARM64 CI runs layer contracts and resolves
diagnostic target dependencies. Kernel compilation, reproducibility
comparisons and device qualification are separate checks.

## Live device tests

Read the applicable [hardware reference](../docs/index.md) and individual
script before execution. Scripts can request playback, radio assistance
or storage writes. Do not run them as a batch or expose hardware to the
build/CI container.

[Wi-Fi qualification](../docs/wifi-qualification.md) and the
[hardware validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md)
document tested artifacts and results. Keep firmware, raw device logs,
credentials and identifiers outside the repository.
