# H432B test suite

Offline checks for BSP metadata, image layouts and selected driver logic.
Live device scripts are separate operator tools and are not run by this suite.

## Run offline checks

From the layer root:

```sh
python3 -m unittest discover -s tests -v
```

For compiled C vectors, run the tests inside the pinned build container with
`WIFI_RX_NATIVE_CC=/usr/bin/gcc`. Without that setting, compiled Wi-Fi
vectors are explicitly skipped.

## Coverage

| Check | Scope |
| --- | --- |
| Source contracts | Recipe relationships, protocol constants, bounds and cleanup structure |
| Wi-Fi receive vectors | Actual C parser: padding, lengths, truncation and CRC/ICV flags |
| Wi-Fi station vectors | H2C framing, CCMP nonce/header construction and BSS-cache selection |

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
