# Target toolchain

Linux, U-Boot and userland use the GNU cross-toolchain built by the pinned
OE-Core recipes. Native amd64 and ARM64 build containers supply only host
build prerequisites; they do not supply target compiler binaries or sysroots.

The version-specific gcc-source_15.3.bbappend applies an ARM backend patch
to the shared compiler source. ASHIFT, ASHIFTRT and LSHIFTRT expansion allocate
scratch registers in explicit statements instead of relying on unspecified
function-argument evaluation order. Compiler version updates must review and
revalidate that patch; it is not a wildcard append.

The historical U-Boot source is compiled with an explicit GNU89 dialect and
OE's target prefix, sysroot and binutils. Its libgcc dependency is explicit.
Linux uses the standard kernel class toolchain selection. Fixed build identity
and source timestamps remain in effect.

## Qualification

Switching toolchains changes generated code. Passing unit tests, compilation
or cross-host hash comparison does not inherit the old artifacts' device
qualification. Retain the working boot artifacts, verify memory-layout limits,
then qualify replacement images through the recovery/RAM path before any
persistent installation.

A newer U-Boot port is a separate migration after this toolchain baseline.
It must retain the factory boot-image contract, recovery access, accessible
startup feedback and tested memory/NAND layout. Do not combine the compiler
migration with a persistent bootloader replacement.
