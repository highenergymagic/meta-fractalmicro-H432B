# Internal SD storage

The internal flashdisk is an SD card connected to the S5PV210 controller
at physical address `0xeb100000` (`sdhci1`). It is separate from the raw
NAND containing the boot chain and Linux volumes, and from the Wi-Fi SDIO
controller at `0xeb300000`. Linux block numbering is not a stable identity;
diagnostics resolve the card through its physical controller.

The tested card reports 62,521,344 512-byte sectors (32,010,928,128 bytes).
Its factory layout contains a FAT32 logical partition starting at sector 2.
This capacity and layout are observations from one device, not universal
requirements for replacement cards.

## Baseline

The historical base device tree retains `hims,read-only-probe` on the
internal controller. A board-scoped MMC block-layer guard prevents writes
and raw MMC command ioctls in that profile. The normal NAND runtime removes
this property and permits internal SD writes; it does not repartition the card.

The configured frequency ceiling is 25 MHz; the tested controller runs at
24 MHz with four data lines and 3.3 V signaling. Runtime clock gating can
show a zero clock in debugfs while the card is idle. No 1.8 V or higher-speed
mode has been qualified.

## Opt-in write test

`linux-h432b-sd-rw-test` removes the read-only property only from the
internal SD controller, includes FAT32 support, and rejects any non-read-only
NAND build profile. `openh432-sd-rw-test` packages this kernel for RAM boot
using the installed read-only NAND system root. It does not repartition,
format, or install anything as part of the build or boot.

After explicit operator authorization, run
`tests/check-internal-sd-rw.sh --write-test`. The script:

1. Checks the board, opt-in device-tree marker, physical card controller,
   NAND read-only status, and absence of mounts or block-device holders.
2. Mounts the existing FAT32 logical partition and creates a unique directory.
3. Writes eight distinct 8 MiB random files with direct I/O and fsync.
4. Unmounts and remounts read-only, then directly reads each file and verifies
   its size and SHA-256 hash.
5. Removes only its own files and directory, then unmounts.

The test requires at least 128 MiB of free space. On failure, it attempts to
unmount but retains test files and RAM evidence for investigation. A reset
loses the RAM evidence. A successful run is a bounded filesystem integrity
test, not a whole-card endurance test or power-loss qualification.

## Hardware result

The opt-in RAM image passed the complete 64 MiB test on the internal card:
all eight hashes matched after unmount/remount and direct readback. Individual
8 MiB transfers measured approximately 5.1–6.1 MB/s for writes including
fsync and 7.5–7.7 MB/s for reads. These are bounded filesystem-test timings,
not a maximum-throughput benchmark.

No MMC or FAT errors were reported during the test. The kernel remained
untainted, no systemd services were failed, NAND remained read-only, and
the SD filesystem was unmounted after test-file cleanup. Existing partitions
were retained. That diagnostic test did not alter the NAND image. Internal
SD write support was subsequently enabled in the normal NAND runtime.

The pinned build and 106 source tests passed. A fresh cross-host bit-for-bit
comparison has not been performed for this new diagnostic image. Whole-card
capacity verification, sustained workloads, higher clocks, and power-loss
behavior remain unqualified.
