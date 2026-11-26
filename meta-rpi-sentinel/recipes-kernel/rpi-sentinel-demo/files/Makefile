# SPDX-License-Identifier: GPL-2.0-only
obj-m := rpi_sentinel_demo.o

.PHONY: all modules_install

all:
	$(MAKE) -C "$(KERNEL_SRC)" M="$(CURDIR)" modules

modules_install:
	$(MAKE) -C "$(KERNEL_SRC)" M="$(CURDIR)" modules_install
