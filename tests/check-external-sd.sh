#!/bin/sh
# SPDX-License-Identifier: MIT
# Read-only inventory and repeatability check at three card locations.
set -eu
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible |
    grep -qx 'hims,braillesense-u2'
test -e /sys/firmware/devicetree/base/hims,external-sd-read-test
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
test "$(cat /proc/sys/kernel/tainted)" = 0
external=
internal=
for candidate in /sys/class/block/mmcblk*; do
    test -e "$candidate/partition" && continue
    path=$(readlink -f "$candidate")
    case "$path" in
        */eb100000.mmc/mmc_host/*/block/mmcblk*)
            test "$(cat "$candidate/ro")" = 1
            internal=$(basename "$candidate")
            ;;
        */eb200000.mmc/mmc_host/*/block/mmcblk*)
            test -z "$external"
            test "$(cat "$candidate/ro")" = 1
            test "$(cat "$candidate/device/type")" = SD
            external=$(basename "$candidate")
            ;;
    esac
done
test -n "$internal"
test -n "$external" || { echo "No external SD card enumerated" >&2; exit 1; }
sectors=$(cat /sys/class/block/"$external"/size)
mib=$((sectors / 2048))
test "$mib" -ge 16
work=$(mktemp -d /run/h432b-external-sd.XXXXXX)
echo "External SD: $external, $sectors sectors; internal: $internal; both read-only"
for offset in 0 $((mib / 2)) $((mib - 4)); do
    dd if="/dev/$external" of="$work/read-a" bs=1M skip="$offset" count=4 iflag=direct
    test "$(stat -c %s "$work/read-a")" = 4194304
    hash=$(sha256sum "$work/read-a" | awk '{print $1}')
    dd if="/dev/$external" of="$work/read-b" bs=1M skip="$offset" count=4 iflag=direct
    test "$(stat -c %s "$work/read-b")" = 4194304
    test "$(sha256sum "$work/read-b" | awk '{print $1}')" = "$hash"
    echo "REPEAT_READ_MATCH offset_MiB=$offset sha256=$hash"
done
test "$(cat /proc/sys/kernel/tainted)" = 0
echo "EXTERNAL_SD_READ_ACCEPTANCE_PASSED"
echo "RAM evidence retained at $work"
