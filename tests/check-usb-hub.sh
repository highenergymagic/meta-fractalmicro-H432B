#!/bin/sh
# SPDX-License-Identifier: MIT
# Read-only acceptance with the known PL2303 adapter plugged into the board.
set -eu
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible |
    grep -qx 'hims,braillesense-u2'
test "$(cat /proc/sys/kernel/tainted)" = 0
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
hubs=0
adapters=0
for dev in /sys/bus/usb/devices/*; do
    test -f "$dev/idVendor" || continue
    case "$(readlink -f "$dev")" in
        */ec200000.usb/*) ;;
        *) continue ;;
    esac
    vid=$(cat "$dev/idVendor")
    pid=$(cat "$dev/idProduct")
    if test "$vid:$pid" = 0409:005a; then
        test "$(cat "$dev/maxchild")" = 4
        test "$(cat "$dev/speed")" = 480
        test "$(basename "$(readlink -f "$dev/driver")")" = onboard-usb-dev
        hubs=$((hubs+1))
        echo "Qualified hub: $(basename "$dev"), four ports, high-speed"
    fi
    if test "$vid:$pid" = 067b:2303; then
        test "$(cat "$dev/speed")" = 12
        bound=0
        for iface in "$dev":*; do
            test -d "$iface" || continue
            if test "$(basename "$(readlink -f "$iface/driver")")" = pl2303; then
                bound=$((bound+1))
            fi
        done
        test "$bound" = 1
        adapters=$((adapters+1))
        echo "Qualified PL2303 binding: $(basename "$dev"), full-speed"
    fi
done
test "$hubs" = 1
test "$adapters" = 1
echo "USB_HUB_AND_PL2303_ACCEPTANCE_PASSED"
