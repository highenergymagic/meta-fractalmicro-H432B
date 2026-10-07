#!/bin/sh
# SPDX-License-Identifier: MIT
# Explicit target-side PM diagnostics. Never request real CPU sleep.
set -eu
case "${1-}" in
    --freezer) level=freezer ;;
    --devices-without-sdio|--devices-sdio-detached|--devices-with-sdio|--devices-with-sdio-async) level=devices ;;
    *) echo "Usage: $0 --freezer | --devices-without-sdio | --devices-sdio-detached | --devices-with-sdio | --devices-with-sdio-async" >&2; exit 2 ;;
esac
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible |
    grep -qx 'hims,braillesense-u2'
test -e /sys/firmware/devicetree/base/hims,factory-resume-test
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
grep -q '\[none\]' /sys/power/pm_test
grep -qw mem /sys/power/state
test "$(cat /proc/sys/kernel/tainted)" = 0
old_async=$(cat /sys/power/pm_async)
old_times=$(cat /sys/power/pm_print_times)
cleanup() {
    printf 'none\n' > /sys/power/pm_test
    printf '%s\n' "$old_async" > /sys/power/pm_async
    printf '%s\n' "$old_times" > /sys/power/pm_print_times
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
if test "$level" = devices && test "$1" = --devices-sdio-detached; then
    test ! -L /sys/bus/platform/devices/eb300000.mmc/driver
    test -L /sys/bus/platform/devices/eb100000.mmc/driver
elif test "$level" = devices; then
    # Physical controller identity, never asynchronous mmcN numbering.
    host=/sys/bus/platform/devices/eb300000.mmc
    driver=$(readlink -f "$host/driver")
    test "$driver" = /sys/bus/platform/drivers/s3c-sdhci
    found=0
    for fn in /sys/bus/sdio/devices/*; do
        test -e "$fn/uevent" || continue
        case "$(readlink -f "$fn")" in
            */eb300000.mmc/*)
                grep -qx 'SDIO_ID=024C:8712' "$fn/uevent"
                found=$((found+1))
                ;;
        esac
    done
    test "$found" = 1
    test -L /sys/bus/platform/devices/eb100000.mmc/driver
    if test "$1" = --devices-without-sdio; then
        printf 'eb300000.mmc\n' > "$driver/unbind"
        test ! -L "$host/driver"
        test -L /sys/bus/platform/devices/eb100000.mmc/driver
        echo "WiFi host detached; internal SD still bound. Rebind is separate."
    fi
fi
if test "$1" = --devices-with-sdio-async; then
    printf '1\n' > /sys/power/pm_async
else
    printf '0\n' > /sys/power/pm_async
fi
printf '1\n' > /sys/power/pm_print_times
printf '%s\n' "$level" > /sys/power/pm_test
grep -q "\\[$level\\]" /sys/power/pm_test
echo "BEGIN guarded PM test: $level"
cat /proc/uptime
# The preceding readback is mandatory: never write mem with pm_test=none.
printf 'mem\n' > /sys/power/state
cat /proc/uptime
echo "RETURNED guarded PM test: $level"
cleanup
trap - EXIT
cat /sys/power/pm_test
cat /proc/sys/kernel/tainted
cat /sys/class/ubi/ubi0/ro_mode
echo "PM_DIAGNOSTIC_RETURNED"
