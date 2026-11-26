SUMMARY = "Moduł testowy udostępniający czas od załadowania przez sysfs"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = "file://Makefile file://rpi_sentinel_demo.c file://COPYING"
S = "${UNPACKDIR}"

inherit module
