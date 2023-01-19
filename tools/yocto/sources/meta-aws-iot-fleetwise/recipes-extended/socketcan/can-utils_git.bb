SUMMARY = "Linux CAN network development utilities"
DESCRIPTION = "Linux CAN network development"
LICENSE = "GPLv2 & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://include/linux/can.h;endline=43;md5=a6b6c0dcc6ff5e00989430eac32bd842"

DEPENDS = "libsocketcan"

SRC_URI = "git://github.com/linux-can/${BPN}.git;protocol=git;branch=master"

SRCREV = "eb66451df280f95a9a12e78b151b8d867e1b78ed"

PV = "0.0+gitr${SRCPV}"

S = "${WORKDIR}/git"

inherit autotools pkgconfig
