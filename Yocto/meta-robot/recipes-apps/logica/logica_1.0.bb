SUMMARY = "Lógica de control del robot Roomba-Disco"
DESCRIPTION = "Proceso C dueño del estado del robot: atiende JSON por socket Unix y mueve los motores con libroombateca"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd

DEPENDS = "libroombateca cjson"

FILESEXTRAPATHS:prepend := "${THISDIR}/../../../../Logica:"

SRC_URI = "file://logica.c \
           file://roombateca_control.c \
           file://roombateca_control.h \
           file://auto.c \
           file://auto.h \
           file://odometria.c \
           file://odometria.h \
           file://mapa.c \
           file://mapa.h \
           file://estado.json \
           file://logica.service \
"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} -I${STAGING_INCDIR}/cjson -I${S} \
        logica.c roombateca_control.c auto.c odometria.c mapa.c \
        ${LDFLAGS} -lcjson -lroombateca -lpthread -lm -o logica
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/logica ${D}${bindir}/logica
    install -d ${D}${datadir}/roomba-disco
    install -m 0644 ${S}/estado.json ${D}${datadir}/roomba-disco/estado.json
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/logica.service ${D}${systemd_system_unitdir}/logica.service
}

FILES:${PN} += "${datadir}/roomba-disco/estado.json"

RDEPENDS:${PN} = "libroombateca"

SYSTEMD_SERVICE:${PN} = "logica.service"
SYSTEMD_AUTO_ENABLE = "enable"
