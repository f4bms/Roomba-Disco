# Yocto — Roomba-Disco

Infraestructura de Linux embebido para la Raspberry Pi 4: capa `meta-robot/` (recetas
propias) y la configuración de build en `local.conf`. Basado en **Poky (Scarthgap /
release 5.0)**.

El paso a paso de instalación, generación de la imagen y flasheo en la Pi está en el
[`README.md`](../README.md) de la raíz del repo (secciones 2 y 9). Este archivo cubre
solo los flujos de trabajo que no están ahí: conectar la Pi a WiFi, generar el SDK desde
cero, e iterar con `devtool` sobre la Pi corriendo sin reflashear.

La biblioteca de hardware (`roombateca`) no vive acá, sino en `../Biblioteca/` — ver la
sección 3 del README de la raíz para el detalle de la receta que la referencia.

## Conectar la Pi a WiFi

La imagen trae el driver y firmware del WiFi integrado, `wpa_supplicant` corriendo sobre
`wlan0` desde el arranque y DHCP por `systemd-networkd` (receta
`meta-robot/recipes-connectivity/wifi-config`). No trae ninguna red guardada: la primera
vez hay que entrar por cable (Ethernet) y agregarla desde la Pi:

```bash
# desde: ~/Taller4/Roomba-Disco (en el host; los comandos siguientes se escriben DENTRO de la sesión SSH de la Pi)
ssh root@<ip-por-ethernet>
wpa_passphrase "NOMBRE_RED" "contraseña" >> /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
systemctl restart wpa_supplicant@wlan0
ip -br addr show wlan0                 # debería mostrar la IP que dio el router
```

La red queda guardada en la SD, así que en los siguientes arranques se conecta sola y ya
se puede desconectar el cable. Si ambos están conectados, se prefiere la ruta por
Ethernet. Para cambiar de red, editar `/etc/wpa_supplicant/wpa_supplicant-wlan0.conf`
(borrar el bloque `network={...}` viejo) y repetir los dos últimos comandos. Para ver el
estado: `wpa_cli -i wlan0 status` e `iw dev wlan0 link`.

## Generar el SDK standalone (para cross-compilar sin la imagen completa)

```bash
# desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4
bitbake core-image-minimal -c populate_sdk
# (equivalente a "bitbake meta-toolchain", pero atado a los paquetes de
# core-image-minimal en vez del set genérico de meta-toolchain)
```

Genera un instalador en `tmp/deploy/sdk/poky-glibc-x86_64-...-toolchain-*.sh` (unos
250 MB). Al correrlo (`./poky-glibc-...-toolchain-5.0.20.sh -d /ruta/destino`) deja un
`environment-setup-cortexa72-poky-linux` para `source`ar:

```bash
# desde: la carpeta del programa que se quiere compilar (en el host)
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
# desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4  (todos los comandos de este bloque)
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

## Regenerar la imagen después de cambiar la configuración

Después de editar `conf/local.conf` (agregar o quitar paquetes, opciones del `config.txt`, etc.) normalmente **no hay que hacer ningún paso extra**: se vuelve a lanzar `bitbake` y este detecta qué cambió y rehace solo eso.

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15
source oe-init-build-env rpi4        # solo si la terminal es nueva
```

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4
bitbake core-image-minimal
```

La primera ejecución tras el cambio reparsea las recetas (cerca de un minuto). Cuánto trabaja después depende de lo que se tocó:

| Cambio | Qué se rehace |
|---|---|
| Agregar o quitar algo en `IMAGE_INSTALL` | Solo el armado del rootfs y de la imagen; si el paquete nuevo no estaba compilado, se compila antes. |
| `RPI_EXTRA_CONFIG` | Los archivos de arranque y la imagen. |
| `KERNEL_MODULE_AUTOLOAD` | El rootfs y la imagen. |
| `DISTRO_FEATURES` o `MACHINE` | Casi todo; puede tardar como un primer build. |
| Agregar o quitar capas (`bitbake-layers`) | Reparseo completo y lo que dependa de las recetas nuevas. |
| Código fuente o recetas (`.bb`, `Logica/`, `Servidor/`, `Biblioteca/`) | Esa receta y las que dependen de ella (BitBake calcula un checksum de los archivos del `SRC_URI`). |

Comandos útiles:

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4
bitbake -e core-image-minimal | grep -E '^IMAGE_INSTALL='     # ver qué quedó configurado
bitbake -c cleansstate logica && bitbake logica                # forzar que una receta se recompile desde cero
grep -E 'systemd-analyze|metricas|procps|alsa-utils|tailscale' tmp/deploy/images/raspberrypi4-64/*.manifest   # qué paquetes entraron
```

No se debe borrar `tmp/` ni `sstate-cache/` salvo que sea imprescindible: se pierde toda la caché. Cada cambio hecho al `local.conf` del build también debe copiarse a [`Yocto/local.conf`](local.conf) para que el del repo siga igual al que se usa.

## Medir el tamaño del rootfs sin flashear la Pi

El tamaño real del rootfs se puede conocer en el host, antes de grabar la microSD.

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4
ls -lh tmp/deploy/images/raspberrypi4-64/ | grep -E 'rootfs|wic|manifest'
```

El archivo `*.rootfs.ext3` (o `.ext4`) **no** es el valor a comparar con los 200 MB, porque incluye espacio libre de relleno. Lo que cuenta es el espacio usado:

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4/tmp/deploy/images/raspberrypi4-64
dumpe2fs -h core-image-minimal-raspberrypi4-64.rootfs.ext3 2>/dev/null | awk -F: '
/Block count/ {c=$2+0} /Free blocks/ {f=$2+0} /Block size/ {b=$2+0}
END {printf "Usado: %.1f MB\n", (c-f)*b/1024/1024}'
```

<!-- TODO (nombre del archivo rootfs): el nombre exacto y la extensión (.ext3 o .ext4) dependen del build. Confirmarlo con el `ls` de arriba y ajustar el comando. -->

Para ver qué directorios ocupan más espacio (montando la imagen en solo lectura):

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4/tmp/deploy/images/raspberrypi4-64
mkdir -p /tmp/rootfs_mnt
sudo mount -o loop,ro core-image-minimal-raspberrypi4-64.rootfs.ext3 /tmp/rootfs_mnt
sudo du -xsm /tmp/rootfs_mnt
sudo du -xm --max-depth=2 /tmp/rootfs_mnt | sort -rn | head -20
sudo umount /tmp/rootfs_mnt
```

Para saber qué **paquete** pesa cuánto se activa temporalmente `buildhistory`:

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4
echo 'INHERIT += "buildhistory"' >> conf/local.conf      # solo para medir; quitarla después y NO copiarla a Yocto/local.conf
bitbake core-image-minimal
find buildhistory -name installed-package-sizes.txt
```

```bash
# Ejecutar desde: ~/Taller4/poky-scarthgap-5.0.15/rpi4
sort -rn <ruta-que-imprimio-find> | head -25      # los 25 paquetes más grandes, en KiB
```

Esa lista alimenta la columna "Tamaño instalado" de la tabla de paquetes del README raíz (sección 2).
