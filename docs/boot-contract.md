# Boot and image contract

- Factory first-stage loader and EBOOT are retained.
- NAND51 and NAND56 bootstrap code link at physical 0x40021000. A factory
  NK carrier loads at CE address 0x80020000: ECEC at +0x40, ROMHDR pointer
  0x80020100 at +0x44 and relative offset 0x100 at +0x48. Both pointers are
  required; omitting the relative offset caused recovery-only boots.
- RAM52/53/54/55 link at 0x46000000. Never flash one directly or disguise it
  as low-address code.
- NAND56 is a separate low-address bootstrap that embeds a checked RAM55
  image, copies it to 0x46000000 and uses the existing go handoff. Its CE
  carrier is explicitly bounded. The packager reproduces the historical
  NAND51 carrier byte-for-byte; that does not qualify NAND56 on hardware.
- Physical DRAM starts at 0x40000000 and totals 256 MiB. Image/heap/stack
  overlap checks run during the NAND-reader and bootstrap builds.
- Linux uses zImage, DTB and compressed initramfs. The fastboot and NAND
  loader use a bounded Android-v2 envelope; the compressed root slot is
  limited to 16 MiB. NAND kernel volumes have a 132 x 124 KiB capacity.
- NAND boot currently means a NAND-resident kernel + DEBUG initramfs.
  It does not yet switch the production root to SquashFS or implement A/B
  selection/rollback. Base SquashFS is separately mount-tested on ubiblock.
- Build targets do not access USB, install, erase or flash anything.
  No end-user installer is supplied. Source build and device qualification
  are separate, and a new artifact never inherits old qualification.

The corrected NAND format and bounded UBI layout are described in
[nand.md](nand.md). The factory prefix uses a different ECC format and must
not be rewritten using the Linux data driver. The installed EBOOT may differ
from vendor download images; preserve the actual raw+OOB backup privately.
Internal SD remains read-only and has not been repartitioned.

USB diagnostic interfaces provide privileged local access and are NOT
production authentication boundaries. Do not expose them through a network.
