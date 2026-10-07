#!/bin/sh
# SPDX-License-Identifier: MIT
# Fixed EPO-state query; no stored state is changed.
set -eu
test "${1-}" = --query-epo || exit 2
test -e /sys/firmware/devicetree/base/hims,gps-query-test
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
test "$(cat /proc/sys/kernel/tainted)" = 0
! pidof gpsd >/dev/null
tty=
for candidate in /sys/class/tty/ttySAC*; do
    case "$(readlink -f "$candidate")" in
        */e2900400.serial/*) test -z "$tty"; tty=$(basename "$candidate");;
    esac
done
test -n "$tty"
! grep -q "console=$tty" /proc/cmdline
stty -F "/dev/$tty" 9600 raw -echo -ixon -ixoff -crtscts clocal cread cs8 -parenb -cstopb -hupcl
work=$(mktemp -d /run/h432b-epo-query.XXXXXX)
timeout 10 cat "/dev/$tty" > "$work/nmea.raw" &
reader=$!
trap 'kill "$reader" 2>/dev/null || true' EXIT
sleep 1
kill -0 "$reader"
printf '$PMTK607*33\r\n' > "/dev/$tty"
status=0
wait "$reader" || status=$?
test "$status" = 0 || test "$status" = 124
echo GPS_CAPTURE_BEGIN
cat "$work/nmea.raw"
printf '\nGPS_CAPTURE_END\n'
echo GPS_EPO_QUERY_COMPLETED
