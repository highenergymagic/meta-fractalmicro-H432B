# SPDX-License-Identifier: MIT
SUMMARY = "BRLTTY with the H432B internal-display backend"
HOMEPAGE = "https://brltty.app/"
LICENSE = "LGPL-2.1-or-later"
LIC_FILES_CHKSUM = "file://LICENSE-LGPL;md5=4bf661c1e3793e55c8d1051bc5e0ae21"
SRC_URI = "git://github.com/brltty/brltty.git;protocol=https;branch=master \
           file://0001-h432b-backend.patch \
           file://braille.c file://h432b-keys.h file://Makefile.in file://all.ktb"
SRCREV = "4fddd533bf6023820266879203839db511808d63"
COMPATIBLE_MACHINE = "^h432b$"
DEPENDS = "tcl-native autoconf-native automake-native pkgconfig-native systemd ncurses"
inherit autotools-brokensep pkgconfig
EXTRA_OECONF = "--with-braille-driver=h4,-all --with-screen-driver=lx,-all \
    --with-braille-device=/dev/h432b-braille \
    --with-tables-directory=${datadir}/brltty \
    --with-writable-directory=/run/brltty \
    --with-api-socket-path=/run/brltty \
    --disable-stripping --disable-speech-support --disable-x \
    --disable-icu --disable-polkit --disable-expat --disable-liblouis \
    --disable-gpm --disable-i18n --disable-emacs-bindings \
    --disable-java-bindings --disable-lisp-bindings --disable-lua-bindings \
    --disable-ocaml-bindings --disable-python-bindings --disable-tcl-bindings"
do_configure() {
    install -d ${S}/Drivers/Braille/H432B ${S}/Tables/Input/h4
    install -m 0644 ${UNPACKDIR}/braille.c ${UNPACKDIR}/h432b-keys.h ${UNPACKDIR}/Makefile.in ${S}/Drivers/Braille/H432B/
    install -m 0644 ${UNPACKDIR}/all.ktb ${S}/Tables/Input/h4/
    cd ${S}
    ./autogen
    oe_runconf
}
do_install() {
    oe_runmake INSTALL_ROOT=${D} install
    # This root-run appliance does not use the optional capability helper or
    # user-creation script or external Python/latex-access translator. Keep their unsupported runtime
    # dependencies out of the base image.
    rm ${D}${bindir}/brltty-mkuser ${D}${bindir}/brltty-prologue.bash
    rm ${D}${bindir}/brltty-setcaps ${D}${datadir}/brltty/Contraction/latex-access.ctb
    # RuntimeDirectory in the OS service owns these empty volatile directories.
    rmdir ${D}/run/brltty ${D}/run
}
FILES:${PN} += "${libdir}/brltty ${datadir}/brltty ${libexecdir}/brltty"
