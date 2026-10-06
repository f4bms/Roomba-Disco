SUMMARY = "Script métricas de eficiencia de recursos - Roomba-Disco"
DESCRIPTION = "Mide rootfs, tiempo de arranque, RAM y CPU en la rasp usando busybox, /proc y systemd. Guarda el resultado en un .txt"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://medir_metricas.sh"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/medir_metricas.sh ${D}${bindir}/medir_metricas.sh
}

# Solo necesita un shell POSIX (busybox)
RDEPENDS:${PN} = "busybox"
