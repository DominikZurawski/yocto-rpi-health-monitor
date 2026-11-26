SUMMARY = "Monitor temperatury, pamięci i czasu pracy Raspberry Pi"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=a11ad737a0f388023ecc5e5bc24c4d5a"

# Receptura pobiera kod aplikacji z tego samego repozytorium co warstwa.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:"
SRC_URI = " \
    file://app \
    file://common;subdir=app \
    file://LICENSE;subdir=app \
    file://rpi-sentinel.service \
"
S = "${UNPACKDIR}/app"

inherit cmake pkgconfig systemd features_check
REQUIRED_DISTRO_FEATURES = "systemd"
DEPENDS += "libsentinel-metrics systemd"

EXTRA_OECMAKE += "-DBUILD_TESTING=OFF -DSENTINEL_CONFIG_INSTALL_DIR=${sysconfdir}"
EXTRA_OECMAKE += "-DSENTINEL_COMMON_INCLUDE_DIR=${S}/common"

SYSTEMD_SERVICE:${PN} = "rpi-sentinel.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
CONFFILES:${PN} += "${sysconfdir}/rpi-sentinel.conf"

do_install:append() {
    install -Dm0644 "${UNPACKDIR}/rpi-sentinel.service" \
        "${D}${systemd_system_unitdir}/rpi-sentinel.service"
}
