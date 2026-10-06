SUMMARY = "Servidor web y WebSocket del robot Roomba-Disco"
DESCRIPTION = "Servidor C (CivetWeb) que entrega el cliente Angular y transporta JSON hacia la lógica por un socket Unix"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://third_party/civetweb/LICENSE.md;md5=e5f28949b2d9ec1f4da8bb00aff8b6d4"

inherit systemd

DEPENDS = "cjson"

FILESEXTRAPATHS:prepend := "${THISDIR}/../../../../Servidor:"

SRC_URI = "file://servidor.c \
           file://auth \
           file://third_party \
           file://servidor.service \
"

S = "${WORKDIR}"

# El cliente corre en un dispositivo externo; la Pi solo necesita la logica.
RDEPENDS:${PN} = "logica"

CIVETWEB_DEFS = "-DUSE_WEBSOCKET -DNO_SSL -DNO_CGI -DNO_LUA -DNO_DUKTAPE"

do_compile() {
    ${CC} ${CFLAGS} ${CIVETWEB_DEFS} -Ithird_party/civetweb/include -c third_party/civetweb/src/civetweb.c -o civetweb.o
    ${CC} ${CFLAGS} -Ithird_party/civetweb/include -Iauth -I${STAGING_INCDIR}/cjson \
        -c servidor.c -o servidor.o
    ${CC} ${CFLAGS} -Iauth -c auth/auth.c -o auth.o
    ${CC} ${CFLAGS} -Iauth -c auth/sha256.c -o sha256.o
    ${CC} ${CFLAGS} -Iauth -c auth/crear_usuario.c -o crear_usuario.o
    ${CC} ${LDFLAGS} servidor.o auth.o sha256.o civetweb.o -lcjson -lpthread -o servidor
    ${CC} ${LDFLAGS} crear_usuario.o auth.o sha256.o -o crear_usuario
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/servidor ${D}${bindir}/servidor
    install -m 0755 ${S}/crear_usuario ${D}${bindir}/crear_usuario
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/servidor.service ${D}${systemd_system_unitdir}/servidor.service
}

SYSTEMD_SERVICE:${PN} = "servidor.service"
SYSTEMD_AUTO_ENABLE = "enable"
