# Yocto — Roomba-Disco

Infraestructura de Linux embebido para la Raspberry Pi 4: capa `meta-robot/` (recetas
propias) y la configuración de build en `local.conf`. Basado en **Poky (Scarthgap /
release 5.0)**.

El paso a paso de instalación, generación de la imagen y flasheo en la Pi está en el
[`README.md`](../README.md) de la raíz del repo (secciones 2 y 9). Este archivo cubre
solo los dos flujos de trabajo que no están ahí: generar el SDK desde cero, e iterar con
`devtool` sobre la Pi corriendo sin reflashear.

La biblioteca de hardware (`roombateca`) no vive acá, sino en `../Biblioteca/` — ver la
sección 3 del README de la raíz para el detalle de la receta que la referencia.

## Generar el SDK standalone (para cross-compilar sin la imagen completa)

```bash
bitbake core-image-minimal -c populate_sdk
# (equivalente a "bitbake meta-toolchain", pero atado a los paquetes de
# core-image-minimal en vez del set genérico de meta-toolchain)
```

Genera un instalador en `tmp/deploy/sdk/poky-glibc-x86_64-...-toolchain-*.sh` (unos
250 MB). Al correrlo (`./poky-glibc-...-toolchain-5.0.20.sh -d /ruta/destino`) deja un
`environment-setup-cortexa72-poky-linux` para `source`ar:

```bash
source /ruta/destino/environment-setup-cortexa72-poky-linux
$CC mi_programa.c -o mi_programa      # ya cross-compila para aarch64
```

Sirve para cross-compilar `Biblioteca/` a mano, sin pasar por `bitbake` cada vez — pero
ojo: el `$CC` del SDK linkea contra el sysroot que trae *ese* instalador, generado en el
momento del `populate_sdk`. Si cambiaste headers/ABI de una receta después de instalar el
SDK, hay que regenerarlo (repetir este paso) para que el sysroot del SDK quede al día.

## Probar cambios en el hardware real sin reflashear la SD (`devtool`)

Para iterar una receta (por ejemplo `libroombateca`) y probarla en la Pi corriendo, sin
rebuildear `core-image-minimal` ni tocar la SD:

```bash
# 1. Traer la receta a un workspace editable (crea build/workspace/, no toca la capa real)
devtool modify libroombateca

# 2. Editar el código — devtool enlaza los archivos del workspace con symlinks hacia
#    workspace/sources/libroombateca/oe-local-files/, que es donde hay que editar
#    (los que se ven en include/ y lib/ son symlinks a esa carpeta)

# 3. Compilar solo esa receta
devtool build libroombateca

# 4. Desplegar el resultado directo a la Pi por SSH (usa rsync, no reflashea nada)
devtool deploy-target libroombateca root@<IP-de-la-Pi>

# 5. Probar en la Pi (por SSH, corriendo algo que la enlace) y repetir 2-4 las veces
#    que haga falta

# 6. Cuando el cambio esté listo, volcarlo de vuelta a la capa real del repo
#    (esto sincroniza los archivos editados a Biblioteca/ vía FILESEXTRAPATHS)
devtool finish libroombateca /ruta/al/repo/Roomba-Disco/Yocto/meta-robot

# Si en cambio NO querés quedarte con el cambio, en vez de "finish":
devtool reset libroombateca
```

`deploy-target` necesita SSH sin pedir contraseña hacia la Pi (llave copiada con
`ssh-copy-id`) y que el usuario remoto (`root` en esta imagen de desarrollo) tenga permiso
de escritura en las rutas que instala la receta. Para revertir lo desplegado sin esperar
al próximo build de imagen: `devtool undeploy-target libroombateca root@<IP>`.
