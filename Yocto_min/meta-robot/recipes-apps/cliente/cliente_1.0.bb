SUMMARY = "Cliente web Angular del robot Roomba-Disco"
DESCRIPTION = "Compila el cliente Angular y lo instala como archivos estáticos que sirve el servidor"
LICENSE = "CLOSED"

inherit allarch

DEPENDS = "nodejs-native"

FILESEXTRAPATHS:prepend := "${THISDIR}/../../../../Cliente:"

SRC_URI = "file://package.json \
           file://package-lock.json \
           file://angular.json \
           file://tsconfig.json \
           file://tsconfig.app.json \
           file://src \
           file://public \
"

S = "${WORKDIR}"

# npm ci descarga las dependencias: única tarea con red
do_compile[network] = "1"
do_compile() {
    export HOME=${WORKDIR}
    export NG_CLI_ANALYTICS=false
    export npm_config_cache=${WORKDIR}/npm-cache
    cd ${S}
    npm ci --no-audit --no-fund
    npm run build
}

do_install() {
    install -d ${D}${datadir}/roomba-disco/www
    cp -r ${S}/dist/scrap-e-controller/browser/. ${D}${datadir}/roomba-disco/www/
}

FILES:${PN} = "${datadir}/roomba-disco/www"
