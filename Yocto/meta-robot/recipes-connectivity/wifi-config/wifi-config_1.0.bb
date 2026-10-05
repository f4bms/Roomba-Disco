SUMMARY = "Configuración de WiFi (wlan0) para el robot Roomba-Disco"
DESCRIPTION = "Levanta wpa_supplicant en wlan0 al arrancar y le da DHCP con systemd-networkd. Las redes se agregan en la Pi con wpa_passphrase."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://25-wlan.network \
           file://wpa_supplicant-wlan0.conf \
"

S = "${WORKDIR}"

RDEPENDS:${PN} = "wpa-supplicant wpa-supplicant-passphrase wpa-supplicant-cli"

do_install() {
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${S}/25-wlan.network ${D}${sysconfdir}/systemd/network/25-wlan.network
    install -d ${D}${sysconfdir}/wpa_supplicant
    install -m 0600 ${S}/wpa_supplicant-wlan0.conf ${D}${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf
    install -d ${D}${sysconfdir}/systemd/system/multi-user.target.wants
    ln -sf ${systemd_system_unitdir}/wpa_supplicant@.service \
        ${D}${sysconfdir}/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service
}

CONFFILES:${PN} = "${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf"
