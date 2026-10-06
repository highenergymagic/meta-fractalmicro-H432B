#!/bin/sh
# SPDX-License-Identifier: MIT
# Run on an explicitly selected test device. Read-only; no source switching.
set -eu
export SYSTEMD_PAGER=cat PAGER=cat SYSTEMD_COLORS=0
p=/sys/class/power_supply/h432b-battery
test "$(cat "$p/type")" = Battery
test "$(stat -c %a "$p/capacity")" = 444
test "$(stat -c %a "$p/status")" = 444
for unsupported in voltage_now current_now temp health present model_name serial_number; do
    test ! -e "$p/$unsupported"
done
for sample in 1 2 3; do
    capacity=$(cat "$p/capacity")
    test "$capacity" -ge 0
    test "$capacity" -le 100
    status=$(cat "$p/status")
    case "$status" in
        Charging|Discharging|"Not charging") ;;
        *) echo "Unexpected/unavailable status: $status"; exit 1 ;;
    esac
    printf 'sample=%s capacity=%s status=%s\n' "$sample" "$capacity" "$status"
    test "$sample" = 3 || sleep 6
done
cat "$p/uevent"
udevadm info --query=property --path="$p"
test "$(cat /proc/sys/kernel/tainted)" = 0
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
echo H432B_BATTERY_POWER_SUPPLY_PASS
