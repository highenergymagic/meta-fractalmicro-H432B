#!/bin/sh
# SPDX-License-Identifier: MIT
# Restart only the opt-in GPS power driver while receive capture is active.
set -eu
test -e /sys/firmware/devicetree/base/hims,gps-receive-test
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
test "$(cat /proc/sys/kernel/tainted)" = 0
driver=/sys/bus/platform/drivers/h432b-gps-power
device=/sys/bus/platform/devices/gps_power
test "$(readlink -f "$device/driver")" = "$driver"
test -f /tmp/openh432-stage/capture-gps.sh
work=$(mktemp -d /run/h432b-gps-startup.XXXXXX)
restore() {
    if test ! -L "$device/driver"; then
        printf 'gps_power\n' > "$driver/bind"
    fi
}
trap restore EXIT
trap 'exit 1' HUP INT TERM
sh /tmp/openh432-stage/capture-gps.sh --receive-30s > "$work/capture.log" 2>&1 &
reader=$!
sleep 2
kill -0 "$reader"
printf 'gps_power\n' > "$driver/unbind"
sleep 1
printf 'gps_power\n' > "$driver/bind"
test "$(readlink -f "$device/driver")" = "$driver"
wait "$reader"
cat "$work/capture.log"
test "$(cat /proc/sys/kernel/tainted)" = 0
echo "GPS_STARTUP_CAPTURE_COMPLETED"
