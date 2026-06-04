SUMMARY = "Event-driven emergency notification daemon"
DESCRIPTION = "Modular event-driven emergency notification and alarm response framework for embedded Linux security panels."
HOMEPAGE = "https://github.com/dimitrijemarkovic/emergency-notifyd"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=6dbd2560b6458f6e47e08473fc9622d1"

SRC_URI = "git://github.com/dimitrijemarkovic/emergency-notifyd.git;protocol=https;branch=main \
           file://emergency-notifyd.service"

SRCREV = "${AUTOREV}"

PV = "0.1.0+git${SRCPV}"

S = "${WORKDIR}/git"

inherit cmake systemd

DEPENDS += "ubus libubox json-c"
RDEPENDS:${PN} += "ubus libubox json-c"

EXTRA_OECMAKE += "-DENABLE_UBUS=ON"

SYSTEMD_SERVICE:${PN} = "emergency-notifyd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/emergency-notifyd.service ${D}${systemd_system_unitdir}/
}

FILES:${PN} += "${systemd_system_unitdir}/emergency-notifyd.service"
