# SPDX-License-Identifier: MIT
# Boot/kernel only. Userland uses the OE-Core compiler and sysroot.
H432B_ARM_GNU_VERSION = "14.3.rel1"
H432B_ARM_GNU_SHA256 = "3ec0113af5154a2573b3851d74d9e9501a805abf9dfa0f82b04ef26fa0e6fc35"
H432B_ARM_GNU_ROOT = "/opt/arm-gnu/${H432B_ARM_GNU_VERSION}"
H432B_CROSS = "${H432B_ARM_GNU_ROOT}/bin/arm-none-linux-gnueabihf-"
# Wrynose defers toolchain/gcc inheritance until after recipe parsing.
# Target overrides keep packaging/QA on the pinned binutils even then.
export AR:class-target = "${H432B_CROSS}ar"
export AS:class-target = "${H432B_CROSS}as"
export LD:class-target = "${H432B_CROSS}ld.bfd"
export NM:class-target = "${H432B_CROSS}nm"
export OBJCOPY:class-target = "${H432B_CROSS}objcopy"
export OBJDUMP:class-target = "${H432B_CROSS}objdump"
export RANLIB:class-target = "${H432B_CROSS}ranlib"
export READELF:class-target = "${H432B_CROSS}readelf"
export STRIP:class-target = "${H432B_CROSS}strip"
export STRINGS:class-target = "${H432B_CROSS}strings"
export KBUILD_BUILD_USER = "builder"
export KBUILD_BUILD_HOST = "openh432-builder"
export KBUILD_BUILD_VERSION = "1"
do_configure[vardeps] += "H432B_ARM_GNU_VERSION H432B_ARM_GNU_SHA256"
do_compile[vardeps] += "H432B_ARM_GNU_VERSION H432B_ARM_GNU_SHA256"
do_configure:prepend() {
    test "$(${H432B_CROSS}gcc -dumpmachine)" = "arm-none-linux-gnueabihf" ||
        bbfatal "Use the pinned openh432-build container (wrong compiler target)"
    ${H432B_CROSS}gcc --version | grep -i "Arm GNU Toolchain ${H432B_ARM_GNU_VERSION}" ||
        bbfatal "Wrong Arm GNU compiler release"
}
