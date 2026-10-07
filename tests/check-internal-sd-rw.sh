#!/bin/sh
# SPDX-License-Identifier: MIT
# Explicit filesystem write test; never format or write a raw block device.
set -eu
test "${1-}" = --write-test || {
    echo "Usage: $0 --write-test (creates and removes 64 MiB of FAT32 test files)" >&2
    exit 2
}
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible |
    grep -qx 'hims,braillesense-u2'
test -e /sys/firmware/devicetree/base/hims,internal-sd-write-test
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
test "$(cat /proc/sys/kernel/tainted)" = 0
# Resolve by physical controller, never assume mmcblk enumeration order.
disk=
for candidate in /sys/class/block/mmcblk*; do
    test -e "$candidate/partition" && continue
    case "$(readlink -f "$candidate")" in
        */eb100000.mmc/mmc_host/*/block/mmcblk*)
            test -z "$disk"
            disk=$(basename "$candidate")
            ;;
    esac
done
test -n "$disk"
test "$(cat /sys/class/block/"$disk"/device/type)" = SD
test "$(cat /sys/class/block/"$disk"/ro)" = 0
test "$(cat /sys/class/block/"$disk"/size)" -ge 2097152
# This test targets the existing factory logical FAT partition only.
part=${disk}p5
test -e /sys/class/block/"$part"/partition
test "$(cat /sys/class/block/"$part"/ro)" = 0
for node in /sys/class/block/"$disk" /sys/class/block/"$disk"p*; do
    test -e "$node/dev" || continue
    devno=$(cat "$node/dev")
    if awk -v devno="$devno" '$3 == devno {found=1} END {exit !found}' /proc/self/mountinfo; then
        echo "Refusing mounted storage: $node" >&2
        exit 1
    fi
    for holder in "$node"/holders/*; do
        test ! -e "$holder" || { echo "Storage has holders" >&2; exit 1; }
    done
done
work=$(mktemp -d /run/h432b-sd-rw.XXXXXX)
mnt=$work/card
mkdir "$mnt"
mounted=0
cleanup() {
    if test "$mounted" = 1; then
        umount "$mnt" || echo "WARNING: test mount still active at $mnt" >&2
    fi
    echo "RAM evidence retained at $work"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
mount -t vfat -o rw,nosuid,nodev,noexec "/dev/$part" "$mnt"
mounted=1
available=$(df -Pk "$mnt" | awk 'END {print $4}')
test "$available" -ge 131072
scratch=$(mktemp -d "$mnt/H432BTEST.XXXXXX")
name=${scratch##*/}
echo "Internal SD filesystem test: $disk / $part, directory $name"
# Unique data in each file detects accidental address aliasing across files.
for n in 0 1 2 3 4 5 6 7; do
    dd if=/dev/urandom of="$work/pattern" bs=1M count=8 iflag=fullblock status=none
    hash=$(sha256sum "$work/pattern" | awk '{print $1}')
    printf '%s\n' "$hash" > "$work/hash$n"
    echo "WRITE file$n.bin expected $hash"
    dd if="$work/pattern" of="$scratch/file$n.bin" bs=1M oflag=direct conv=excl,fsync
done
sync
umount "$mnt"
mounted=0
mount -t vfat -o ro,nosuid,nodev,noexec "/dev/$part" "$mnt"
mounted=1
for n in 0 1 2 3 4 5 6 7; do
    dd if="$mnt/$name/file$n.bin" of="$work/readback" bs=1M iflag=direct
    test "$(stat -c %s "$work/readback")" = 8388608
    actual=$(sha256sum "$work/readback" | awk '{print $1}')
    test "$actual" = "$(cat "$work/hash$n")"
    echo "VERIFIED file$n.bin $actual"
done
umount "$mnt"
mounted=0
mount -t vfat -o rw,nosuid,nodev,noexec "/dev/$part" "$mnt"
mounted=1
# Exact files created above only: no recursive removal or raw device writes.
for n in 0 1 2 3 4 5 6 7; do
    rm -- "$mnt/$name/file$n.bin"
done
rmdir "$mnt/$name"
sync
umount "$mnt"
mounted=0
test "$(cat /proc/sys/kernel/tainted)" = 0
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1
echo "INTERNAL_SD_64M_WRITE_READBACK_PASSED"
