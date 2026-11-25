ROOTFS_POSTPROCESS_COMMAND += "stage2_wifi_ssh_setup "

stage2_wifi_ssh_setup() {
    install -d "${IMAGE_ROOTFS}${sysconfdir}/ssh/sshd_config.d"
    install -d "${IMAGE_ROOTFS}${sysconfdir}/ssh/authorized_keys"
    install -d "${IMAGE_ROOTFS}${sysconfdir}/systemd/network"
    install -d "${IMAGE_ROOTFS}${sysconfdir}/systemd/system/multi-user.target.wants"
    install -d -m 0700 "${IMAGE_ROOTFS}${sysconfdir}/rpi-health-monitor"

    cat > "${IMAGE_ROOTFS}${sysconfdir}/ssh/sshd_config.d/10-stage2.conf" <<'CONFIG'
AllowUsers developer
PermitRootLogin no
PubkeyAuthentication yes
AuthenticationMethods publickey
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM yes
AuthorizedKeysFile /etc/ssh/authorized_keys/%u
CONFIG

    cat > "${IMAGE_ROOTFS}${sysconfdir}/systemd/network/10-stage2-wifi.network" <<'CONFIG'
[Match]
Type=wlan

[Network]
DHCP=yes
CONFIG

    cat > "${IMAGE_ROOTFS}${sysconfdir}/systemd/system/stage2-wifi@.service" <<'CONFIG'
[Unit]
Description=Stage 2 Wi-Fi client on %I
BindsTo=sys-subsystem-net-devices-%i.device
After=sys-subsystem-net-devices-%i.device
ConditionPathExists=/etc/rpi-health-monitor/wifi.conf

[Service]
ExecStart=/usr/sbin/wpa_supplicant -i %I -c /etc/rpi-health-monitor/wifi.conf
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
CONFIG

    # Raspberry Pi's onboard Wi-Fi normally appears as wlan0.
    ln -sf ../stage2-wifi@.service "${IMAGE_ROOTFS}${sysconfdir}/systemd/system/multi-user.target.wants/stage2-wifi@wlan0.service"
    touch "${IMAGE_ROOTFS}${sysconfdir}/rpi-health-monitor/stage2-wifi-image"
}
