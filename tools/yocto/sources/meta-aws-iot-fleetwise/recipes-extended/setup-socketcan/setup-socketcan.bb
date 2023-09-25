DESCRIPTION = "Setup SocketCAN at Startup"
LICENSE = "CLOSED"

SRC_URI = "file://setup-socketcan.sh \
           file://setup-socketcan.service"

S = "${WORKDIR}"

SYSTEMD_SERVICE_${PN} = "${PN}.service"

inherit systemd

do_install() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0755 ${S}/setup-socketcan.service ${D}${systemd_system_unitdir}
    install -d ${D}${bindir}
    install -m 0755 ${S}/setup-socketcan.sh ${D}${bindir}
}
