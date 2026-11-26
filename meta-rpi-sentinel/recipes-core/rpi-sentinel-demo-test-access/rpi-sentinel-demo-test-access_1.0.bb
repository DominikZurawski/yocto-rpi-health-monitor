SUMMARY = "Uprawnienia do testu modułu demonstracyjnego przez SSH"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://rpi-sentinel-demo.sudoers"
S = "${UNPACKDIR}"

RDEPENDS:${PN} = "sudo kmod rpi-sentinel-demo"
FILES:${PN} = "${sysconfdir}/sudoers.d/rpi-sentinel-demo"

do_install() {
    # Tryb katalogu odpowiada wpisowi z pakietu sudo-lib.
    install -d -m 0750 "${D}${sysconfdir}/sudoers.d"
    sed 's|@MODPROBE@|${base_sbindir}/modprobe|g' "${UNPACKDIR}/rpi-sentinel-demo.sudoers" \
        > "${D}${sysconfdir}/sudoers.d/rpi-sentinel-demo"
    chmod 0440 "${D}${sysconfdir}/sudoers.d/rpi-sentinel-demo"
}
