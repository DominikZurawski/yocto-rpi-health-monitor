FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Na Pi 3 wbuduj dostęp do konfiguracji uruchomionego kernela.
SRC_URI:append:raspberrypi3-64 = " file://ikconfig.cfg"
