# Status indicators

The standard kernel exposes the RTL8712's two LED outputs through the Linux
LED class after Wi-Fi network registration:

- `/sys/class/leds/rtl8712::led0`
- `/sys/class/leds/rtl8712::led1`

These names identify chip outputs, not verified front-panel positions.
The [manufacturer's manual](https://fccid.io/QJCH432B/User-Manual/Manual-1627098.pdf)
describes Wi-Fi, Bluetooth, GPS and power indicators. Their complete wiring
and independent software control are not established. No generic GPIO LEDs
are declared for unidentified outputs.

## Control

With the Wi-Fi interface running, write `0` or `1` to an output's
`brightness` attribute. Maximum brightness is `1`; there is no dimming
or default activity trigger.

```sh
echo 1 > /sys/class/leds/rtl8712::led0/brightness
echo 0 > /sys/class/leds/rtl8712::led0/brightness
```

The driver serializes access with Wi-Fi operations, preserves the other
output's register nibble and reads back each write. LED-class errors appear
in the kernel log. A successful sysfs write or cached brightness value alone
is not an optical confirmation.

The LED core requests off during suspend and restores requested brightness
on resume. The interface rejects hardware access while the network is down
or faulted. Registration does not establish a new initial hardware state;
set an explicit brightness when taking software ownership.

## Implementation

The outputs occupy the low and high nibbles of RTL8712 `LEDCFG`
(logical address `0x102502f2`). Access uses the WLAN SDIO window through the
existing byte-mode transport. Software drive clears the selected nibble for
on and sets its bit 3 for off. The register definition and operations agree
with the factory Wi-Fi driver and the GPL Realtek source used by the
[Wi-Fi implementation](wifi.md).

Register-access qualification does not establish visible colour, position,
brightness, or suspend/resume behaviour. Those require separate hardware
checks. Bluetooth, GPS and power indicator control remains unqualified.
