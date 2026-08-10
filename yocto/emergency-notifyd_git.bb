SUMMARY = "Event-driven emergency notification daemon"
DESCRIPTION = "Modular event-driven emergency notification and alarm response framework for embedded Linux security panels."
HOMEPAGE = "https://github.com/dimitrijemarkovic/emergency-notifyd"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=6dbd2560b6458f6e47e08473fc9622d1"

SRC_URI = "git://github.com/dimitrijemarkovic/emergency-notifyd.git;protocol=https;branch=main \
           file://emergency-notifyd.service \
           file://emergency-siren-service.service \
           file://emergency-led-service.service \
           file://emergency-mqtt-service.service \
           file://zlog.conf"

SRCREV = "${AUTOREV}"

PV = "0.1.0+git${SRCPV}"

S = "${WORKDIR}/git"

inherit cmake systemd

DEPENDS += "ubus libubox json-c zlog mosquitto"

# TODO: RDEPENDS runtime package name for libmosquitto is NOT verified --
# could not be determined from this repo (no sources/meta-openembedded
# checkout available here). Check
# sources/meta-openembedded/meta-networking/recipes-connectivity/mosquitto/mosquitto_2.0.18.bb
# on the build server (PACKAGES/FILES split) before this line is trusted;
# do not assume "libmosquitto1" without confirming it there.
RDEPENDS:${PN} += "ubus libubox json-c zlog libmosquitto1"

EXTRA_OECMAKE += "-DENABLE_UBUS=ON -DENABLE_ZLOG=ON"

SYSTEMD_SERVICE:${PN} = "emergency-notifyd.service emergency-siren-service.service emergency-led-service.service emergency-mqtt-service.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/emergency-notifyd.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/emergency-siren-service.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/emergency-led-service.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/emergency-mqtt-service.service ${D}${systemd_system_unitdir}/

    install -d ${D}${sysconfdir}/emergency
    install -m 0644 ${WORKDIR}/zlog.conf ${D}${sysconfdir}/emergency/zlog.conf
}

FILES:${PN} += "${systemd_system_unitdir}/emergency-notifyd.service"
FILES:${PN} += "${systemd_system_unitdir}/emergency-siren-service.service"
FILES:${PN} += "${systemd_system_unitdir}/emergency-led-service.service"
FILES:${PN} += "${systemd_system_unitdir}/emergency-mqtt-service.service"
FILES:${PN} += "${sysconfdir}/emergency/zlog.conf"
