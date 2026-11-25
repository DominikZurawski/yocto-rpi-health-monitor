SUMMARY = "Uprawnienia do testów sprzętowych RPi Sentinel"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://rpi-sentinel-test.sudoers"
S = "${UNPACKDIR}"

RDEPENDS:${PN} = "sudo"
FILES:${PN} = "${sysconfdir}/sudoers.d/rpi-sentinel-test"

do_install() {
    # Tryb katalogu musi odpowiadać wpisowi z pakietu sudo-lib (0750).
    install -d -m 0750 "${D}${sysconfdir}/sudoers.d"
    # Sudo pomija pliki z kropką w nazwie w katalogu sudoers.d.
    sed 's|@SYSTEMCTL@|${base_bindir}/systemctl|g' "${UNPACKDIR}/rpi-sentinel-test.sudoers" \
        > "${D}${sysconfdir}/sudoers.d/rpi-sentinel-test"
    chmod 0440 "${D}${sysconfdir}/sudoers.d/rpi-sentinel-test"
}
