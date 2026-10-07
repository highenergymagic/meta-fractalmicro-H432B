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

Compiler changes require renewed memory-layout checks, artifact comparisons
and device qualification. Build success and equal hashes do not establish
hardware behavior.

The measured native amd64/ARM64 comparison and its exact input revisions are
recorded in [cross-host validation](https://github.com/highenergymagic/openh432-build/blob/main/docs/cross-host-validation.md).
A newer U-Boot port must preserve the factory image contract, recovery access,
accessible startup feedback and memory/NAND layout independently of compiler
qualification.
