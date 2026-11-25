require recipes-core/images/core-image-minimal.bb

SUMMARY = "Minimalny obraz Raspberry Pi z monitorem stanu"
IMAGE_INSTALL:append = " rpi-sentinel"
