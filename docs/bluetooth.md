# Internal Bluetooth

## Hardware evidence

The factory Bluetooth BuiltIn transport selects CSR BCSP on COM1. The serial
registry maps COM1 to S5PV210 UART0 at `0xe2900000`; COM2/UART1 is the separate
GPS path. Although the registry contains 115200 baud, the transport code
overrides it with **1,382,400 baud, 8 data bits, even parity, one stop bit**.
Live HCI readback identifies Cambridge Silicon Radio, HCI/LMP 2.0,
revision/subversion `0x0c5c`; the controller reports the local name `CSR - bc4`.

The board-control Bluetooth enable is GPE0[2], active high, followed by a
300 ms delay. Its shutdown path coordinates with factory software state;
that handshake is not a Linux power-management interface. Initial Linux
integration holds this enable on and does not claim suspend or power cycling.

The factory transport also applies five CSR parameter values before a warm
reset: 0x0217=0xffff, 0x01f6=0x0017, 0x01fe=26000, 0x0254=0x770e and
0x03c9=4. Live defaults differ from these values. All five values have been
written to the volatile PSRAM store and successfully read back on hardware.
It sources the Bluetooth address from a factory NAND file when available;
a shared fallback address in the driver must not be copied into Linux.

## Linux integration

The runtime device tree enables UART0 and its data/flow-control pins.
The kernel uses upstream `BT_HCIUART_BCSP`. A small board-layer BlueZ patch
sets the nonstandard baud rate with Linux termios2/BOTHER and verifies the
readback; BCSP framing and link establishment remain in upstream BlueZ/Linux.

The OS layer packages BlueZ and `FMBluetoothTransport.service`. The transport
service is initially disabled for explicit hardware qualification. It waits
five seconds, uses 8E1 through BlueZ's BCSP setup, and has no automatic restart
loop. GPS, Wi-Fi and other power rails are not modified by this service.

A NAND-booted kernel and base image have established BCSP communication and
returned the controller version and address. Bench initialization in volatile
PSRAM restored the recovered factory address; a controller warm reset and
reattachment retained it. Automatic factory-identity provisioning and startup
configuration are not yet integrated. Hardware testing with a Noxgear 39g speaker has now passed BR/EDR discovery,
legacy pairing/bonding, SDP service discovery and ten L2CAP echo exchanges
(10/10 replies). The audio backend is not included, so this does not qualify
A2DP playback. Repeat NAND boot with automatic initialization, persistent
identity provisioning and Bluetooth power management remain unqualified.

BlueZ references: [BCSP attachment](https://github.com/bluez/bluez/blob/5.86/tools/hciattach.c),
[historical CSR parameter definitions](https://github.com/bluez/bluez/blob/5.50/tools/csr.h).
Factory binaries, registry contents, disassembly and device identifiers remain
private; they are not part of this source layer.
