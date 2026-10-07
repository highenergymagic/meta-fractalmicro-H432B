# BSP tests

Run the offline source-contract tests from the layer root:

```sh
python3 -m unittest discover -s tests -v
```

These Python tests check recipe/source relationships, fixed image-layout and
protocol constants, bounds, prerequisites and cleanup structure. They do not
boot an emulator or a device, exercise electrical behavior, prove correctness
of every C execution path, or establish image reproducibility.

The build repository's two-architecture CI runs this suite against the pinned
layer and resolves the optional diagnostic targets. Actual kernel compilation,
device testing and independent reproducibility comparisons are separate checks.

## Receive-parser vectors

`test_wifi_rx.py` can compile and run synthetic packets through the actual C
receive parser. Set `WIFI_RX_NATIVE_CC=/usr/bin/gcc` when running that test
inside the pinned build container. Without the variable, the compiled test
is explicitly skipped; source-contract checks still run. The vectors cover
record padding, driver-information lengths, truncated input and CRC/ICV flags.
They do not test the SDIO controller or radio firmware.

`test_wifi_station.py` uses the same compiler setting for actual C H2C framing,
CCMP nonce/authenticated-header construction, and BSS-cache selection across
initial jiffies wrap. Its crypto-provider stub checks framing and error
propagation; it does not substitute for testing AES/CCM or a security audit.

## Device scripts

Shell scripts in this directory are explicit operator tools, not part of the
offline suite. Read the applicable hardware guide and script before running
one. Some request live device operations, including audible playback, firmware
assistance or bounded storage writes. Do not run the directory as a batch of
hardware tests or expose devices to a build/CI container.

Wi-Fi requests and prerequisites are documented in the
[Wi-Fi guide](../docs/wifi.md). Historical device results and exact image hashes
are maintained in the [qualification record](../docs/wifi-qualification.md).
Raw logs, extracted firmware and device/network identifiers do not belong in
this repository.
