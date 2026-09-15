SUMMARY = "Biblioteca GPIO"
DESCRIPTION = "Metadatos en CMake cross-compile el control de perifericos e hilos de audio"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Clase CMake
inherit cmake

# El código de Biblioteca/ vive fuera de esta capa, en el mismo repo
FILESEXTRAPATHS:prepend := "${THISDIR}/../../../../Biblioteca:"

SRC_URI = "file://CMakeLists.txt \
           file://include \
           file://lib \
           file://audio \
"

S = "${WORKDIR}"

# audio_th.c reproduce los mp3 invocando mpg123
RDEPENDS:${PN} = "mpg123"

# Indicar a Yocto que empaque la biblioteca compartida (.so) y headers (.h)
FILES:${PN} = "${libdir}/lib*.so"
FILES:${PN} += "${includedir}/*.h"
FILES:${PN} += "${datadir}/roomba-disco/audio/*.mp3"

do_install:append() {
    install -d ${D}${datadir}/roomba-disco/audio
    install -m 0644 ${S}/audio/*.mp3 ${D}${datadir}/roomba-disco/audio/
}

# Vaciar el paquete -dev para evitar que intente reclamar el archivo .so plano
FILES:${PN}-dev = ""

# Desactiva validación de control para ambas variantes
INSANE_SKIP:${PN} += "dev-elf"
INSANE_SKIP:${PN}-dev += "dev-elf"
