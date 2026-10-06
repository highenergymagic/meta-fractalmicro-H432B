#!/bin/sh
# SPDX-License-Identifier: MIT
# Run on an explicitly selected test device. Read-only; no source switching.
set -eu
export SYSTEMD_PAGER=cat PAGER=cat SYSTEMD_COLORS=0
p=/sys/class/power_supply/h432b-battery
test "$(cat "$p/type")" = Battery
test "$(stat -c %a "$p/capacity")" = 444
test "$(stat -c %a "$p/status")" = 444
for unsupported in health present model_name serial_number; do
    test ! -e "$p/$unsupported"
done
for sample in 1 2 3; do
    capacity=$(cat "$p/capacity")
    test "$capacity" -ge 0
    test "$capacity" -le 100
    voltage=$(cat "$p/voltage_now")
    temperature=$(cat "$p/temp")
    current=$(cat "$p/current_now")
    average=$(cat "$p/current_avg")
    test "$voltage" -gt 0
    test "$voltage" -le 4992240
    test "$temperature" -ge -400
    test "$temperature" -le 850
    for prop in voltage_now temp current_now current_avg; do
        test "$(stat -c %a "$p/$prop")" = 444
    done
    printf 'voltage_uV=%s temp_deciC=%s current_uA=%s average_uA=%s\n' "$voltage" "$temperature" "$current" "$average"
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
