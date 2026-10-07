#!/bin/sh
# SPDX-License-Identifier: MIT
# Receive only; raw NMEA may contain private location information.
set -eu
test "${1-}" = --receive-30s || { echo "Usage: $0 --receive-30s" >&2; exit 2; }
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible |
    grep -qx 'hims,braillesense-u2'
test -e /sys/firmware/devicetree/base/hims,gps-receive-test
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
test "$(cat /proc/sys/kernel/tainted)" = 0
tty=
for candidate in /sys/class/tty/ttySAC*; do
    case "$(readlink -f "$candidate")" in
        */e2900400.serial/*)
            test -z "$tty"
            tty=$(basename "$candidate")
            ;;
    esac
done
test -n "$tty"
if grep -q "console=$tty" /proc/cmdline; then
    echo "Refusing console UART" >&2
    exit 1
fi
# This capture sends no GPS command, even when the DT also enables TX.
stty -F "/dev/$tty" 9600 raw -echo -ixon -ixoff -crtscts clocal cread cs8 -parenb -cstopb -hupcl
work=$(mktemp -d /run/h432b-gps.XXXXXX)
echo "GPS receive-only capture: UART1 $tty, 9600 8N1, 30 seconds"
status=0
timeout 30 cat "/dev/$tty" > "$work/nmea.raw" || status=$?
test "$status" = 0 || test "$status" = 124
echo "GPS_CAPTURE_BEGIN"
cat "$work/nmea.raw"
printf '\nGPS_CAPTURE_END\n'
echo "Capture retained at $work/nmea.raw; UART left raw with echo disabled"
test "$(cat /proc/sys/kernel/tainted)" = 0
