# Tailscale: solo para desarrollo (acceso remoto a la Pi). Eliminar esta receta
# antes de la entrega junto con tun.cfg y su IMAGE_INSTALL en local.conf.
SUMMARY = "Tailscale VPN (binarios estáticos oficiales)"
HOMEPAGE = "https://tailscale.com"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

COMPATIBLE_HOST = "aarch64.*-linux"

SRC_URI = "https://pkgs.tailscale.com/stable/tailscale_${PV}_arm64.tgz"
SRC_URI[sha256sum] = "9dd1e6a592a014bbaea0103167ffe299adeda4ba14e078ce9c2895364f6c4c3f"

S = "${WORKDIR}/tailscale_${PV}_arm64"

inherit systemd

SYSTEMD_SERVICE:${PN} = "tailscaled.service"
SYSTEMD_AUTO_ENABLE = "enable"

# Binarios Go prebuilt: sin strip ni chequeos de ELF
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INSANE_SKIP:${PN} += "already-stripped ldflags textrel"

do_compile[noexec] = "1"

do_install() {
    install -d ${D}${sbindir} ${D}${bindir}
    install -m 0755 ${S}/tailscaled ${D}${sbindir}/tailscaled
    install -m 0755 ${S}/tailscale ${D}${bindir}/tailscale
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/systemd/tailscaled.service ${D}${systemd_system_unitdir}/tailscaled.service
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${S}/systemd/tailscaled.defaults ${D}${sysconfdir}/default/tailscaled
}

FILES:${PN} += "${systemd_system_unitdir}/tailscaled.service ${sysconfdir}/default/tailscaled"
