SUMMARY = "Biblioteka odczytu temperatury, pamięci i czasu pracy Linuksa"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=a11ad737a0f388023ecc5e5bc24c4d5a"

FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:"
SRC_URI = "file://libsentinel-metrics file://common;subdir=libsentinel-metrics file://LICENSE;subdir=libsentinel-metrics"
S = "${UNPACKDIR}/libsentinel-metrics"

inherit cmake

# Prywatny parser znajduje się pod S, więc Yocto mapuje jego ścieżkę debugowania.
EXTRA_OECMAKE += "-DSENTINEL_COMMON_INCLUDE_DIR=${S}/common"
