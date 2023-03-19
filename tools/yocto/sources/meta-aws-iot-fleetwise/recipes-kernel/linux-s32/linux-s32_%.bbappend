FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

inherit kernel

SRC_URI += " \
    file://can-isotp.cfg"

DELTA_KERNEL_DEFCONFIG_append += "can-isotp.cfg"
