# Boot and image contract

- Factory first-stage loader and EBOOT are retained.
- The proven NAND51 U-Boot payload links at physical 0x40021000.
  Its CE container metadata must describe the correct load address;
  earlier incorrect headers caused recovery-only boots.
- RAM52 links at 0x46000000 and is a separate RAM-only Linux loader.
- Physical DRAM starts at 0x40000000 and totals 256 MiB.
- Linux uses zImage plus a separate DTB and compressed RAM root filesystem.
  The currently tested loader limits the compressed rootfs slot to 16 MiB.
- Source builds are not automatically device-qualified.
- No provisioning/update tool or CE carrier generator is shipped here yet.

Storage installation is deliberately absent. Raw NAND ECC is not yet
qualified; raw reads are not a corrected backup. Physical bootloader offsets
must be verified including bad-block handling before defining partitions.
The A/B NAND/SD design is a proposal, not a ready-to-use partition table.

USB diagnostic interfaces provide privileged local access and are NOT
production authentication boundaries. Do not expose them through a network.
