# Generar la imagen de Yocto en una PC nueva

Pasos para armar desde cero, en una máquina que solo tiene el repo `Roomba-Disco`, la
imagen del proyecto: `core-image-minimal` para la Raspberry Pi 4
(64 bits) con systemd, dropbear, WiFi, audio, PWM y las recetas de `meta-robot`.

Para entender qué contiene la imagen (capas, paquetes y recetas) ver las secciones 2 y 3 del
[`README.md`](README.md) de la raíz; para usar el sistema una vez grabado, la sección 10 de ese
README y el final del paso 9 de esta guía.

Referencia de versiones (las usadas para la imagen entregada, 6-oct-2026):

| Componente          | Rama        | Commit    |
|---------------------|-------------|-----------|
| Poky                | `scarthgap` | `77d1feb` (reporta 5.0.20) |
| meta-raspberrypi    | `scarthgap` | `6ca1f75` |
| meta-openembedded   | `scarthgap` | `b5874ea` (solo se usa `meta-oe`) |
| Capa propia         | —           | `Roomba-Disco/Yocto_min/meta-robot` |

Cosas a tener en cuenta:

- El repositorio mantiene dos carpetas de configuración: `Yocto/` (de desarrollo, con la
  receta de Tailscale) y `Yocto_min/` (la versión reducida, con el `local.conf` mínimo). La
  imagen entregada se compila desde **`Yocto_min/`**, y es la que usa esta guía.
- Las recetas de `meta-robot` toman el código de `Biblioteca/`, `Servidor/` y `Logica/`
  con rutas relativas (`${THISDIR}/../../../../...`). Por eso **la capa tiene que quedar
  dentro del clon del repo**: no se debe copiar a otra carpeta.

---

## 1. Requisitos de la máquina

- **Disco:** unos 90 GB libres. Tan solo `downloads/` y `sstate-cache/` ocupan 16 GB.
- **Tiempo:** el primer build tarda varias horas (compila el toolchain cruzado, el
  kernel y todo el userspace). Los siguientes reaprovechan el `sstate-cache`.
- **RAM:** con 16 GB va bien usando 10 hilos. Con menos, hay que bajar los hilos (ver paso 4).
- **Locale `en_US.UTF-8`** generado (`locale -a | grep -i en_us`). BitBake no arranca
  sin él.

### Arch Linux

```bash
sudo pacman -S --needed base-devel chrpath cpio diffstat gawk git inetutils lz4 perl \
    python python-jinja python-pexpect python-gitpython rpcsvc-proto socat texinfo \
    unzip wget xz zstd file
```

Arch no está en la lista de distros probadas de Yocto. BitBake avisa con un *warning* en
el sanity check, pero compila igual (la imagen del proyecto se compiló en Arch).

### Ubuntu 22.04 / 24.04

```bash
sudo apt install build-essential chrpath cpio debianutils diffstat file gawk gcc git \
    iputils-ping libacl1 liblz4-tool locales python3 python3-git python3-jinja2 \
    python3-pexpect python3-pip python3-subunit socat texinfo unzip wget xz-utils zstd
sudo locale-gen en_US.UTF-8
```

En **Ubuntu 24.04**, AppArmor bloquea los *user namespaces* que usa BitBake (error
«User namespaces are not usable by BitBake»). Para habilitarlos:

```bash
sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0
```

Con eso el cambio dura hasta el próximo reinicio. Para que sea permanente, se agrega esa línea
a `/etc/sysctl.d/60-bitbake.conf`.

---

## 2. Estructura de carpetas

Todo va en una misma carpeta de trabajo:

```
trabajo/
├── Roomba-Disco/          clon del repo grupal
├── poky/                  Poky
│   └── rpi4/              directorio de build (se crea en el paso 3)
├── meta-raspberrypi/
└── meta-openembedded/
```

```bash
mkdir -p ~/trabajo && cd ~/trabajo

git clone https://github.com/f4bms/Roomba-Disco

git clone -b scarthgap https://git.yoctoproject.org/poky
git -C poky checkout 77d1feb

git clone -b scarthgap https://git.yoctoproject.org/meta-raspberrypi
git -C meta-raspberrypi checkout 6ca1f75

git clone -b scarthgap https://github.com/openembedded/meta-openembedded
git -C meta-openembedded checkout b5874ea
```

Fijar los commits garantiza que salga la misma imagen que se entregó. Si en algún momento
se quiere actualizar, alcanza con quedarse en la punta de `scarthgap` en los tres; es la
misma serie y las capas son compatibles.

---

## 3. Crear el directorio de build

```bash
cd ~/trabajo/poky
source oe-init-build-env rpi4        # crea rpi4/ y te deja parado adentro
```

Cada vez que se abra una terminal nueva para compilar, hay que repetir este `source` (desde
`~/trabajo/poky`). Si ya existe `rpi4/`, solo carga el entorno.

---

## 4. Configuración (`conf/local.conf`)

Se parte del `local.conf` en el repo:

```bash
cp ~/trabajo/Roomba-Disco/Yocto_min/local.conf conf/local.conf
```

Revisar y ajustar estas líneas dependiendo de la cantidad de paralelización que se requiera (Opcionales):

```conf
# Hilos: la cantidad de núcleos menos 2 (en el repo quedó en 5)
BB_NUMBER_THREADS = "10"
PARALLEL_MAKE = "-j 10"
```

Si el fetch de alguna receta falla con un error de `git://` (típico «exit code 128»),
agregar estas dos líneas al final (no están en el `local.conf` del repo):

```conf
PREMIRRORS:prepend = "git://.*/.* https://yoctoproject.org \n"
BB_FETCH_PREFERENCE = "https http git"
```

No hace falta tocar nada más. El `local.conf` del repo ya trae `MACHINE`, systemd,
dropbear, los overlays de PWM y audio, el WiFi y el `IMAGE_INSTALL` con `libroombateca`,
`logica`, `servidor` y `metricas`.

---

## 5. Registrar las capas

Desde `~/trabajo/poky/rpi4`, con el entorno cargado:

```bash
bitbake-layers add-layer ../../meta-raspberrypi
bitbake-layers add-layer ../../meta-openembedded/meta-oe
bitbake-layers add-layer ../../Roomba-Disco/Yocto_min/meta-robot

bitbake-layers show-layers           # deben aparecer las 3 + meta, meta-poky, meta-yocto-bsp
```

`meta-oe` hace falta porque de ahí sale `cjson`, que usan `logica` y `servidor`.

---

## 6. (Opcional) Reaprovechar las descargas y el caché de otra PC

Para no descargar ni recompilar todo de nuevo, se copian estas dos carpetas de una PC donde ya
se compiló la imagen a la nueva **antes** del primer `bitbake`:

| En la PC que ya compiló                 | En la PC nueva                      |
|-----------------------------------------|-------------------------------------|
| `<build>/downloads/` (8,4 GB)           | `~/trabajo/poky/rpi4/downloads/`    |
| `<build>/sstate-cache/` (7,4 GB)        | `~/trabajo/poky/rpi4/sstate-cache/` |

Por ejemplo, con un disco externo o con `rsync -a --info=progress2` por red. El
`sstate-cache` solo sirve si los commits de las capas coinciden con los de la tabla de
arriba: por eso conviene fijarlos.

---

## 7. Compilar la imagen

```bash
bitbake core-image-minimal
```

Para validar el entorno con algo más corto antes de lanzar el build completo:

```bash
bitbake libroombateca                # compila el toolchain + la biblioteca
```

La imagen queda en:

```
~/trabajo/poky/rpi4/tmp/deploy/images/raspberrypi4-64/
    core-image-minimal-raspberrypi4-64.rootfs.wic.bz2     ← la que se flashea
    core-image-minimal-raspberrypi4-64.rootfs.wic.bmap
```

Los nombres sin fecha son enlaces simbólicos a la última imagen compilada.

---

## 8. Flashear la microSD

Identificar el dispositivo de la SD **con cuidado**: `dd` sobrescribe lo que se le indique.

```bash
lsblk                                # buscar la SD por tamaño, p. ej. /dev/sdb o /dev/mmcblk0
```

Desmontar sus particiones si se automontaron y escribir la imagen:

```bash
cd ~/trabajo/poky/rpi4/tmp/deploy/images/raspberrypi4-64
bzcat core-image-minimal-raspberrypi4-64.rootfs.wic.bz2 \
    | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

Si se tiene `bmaptool` instalado, este es más rápido (usa el `.wic.bmap` para escribir solo
los bloques con datos):

```bash
sudo bmaptool copy core-image-minimal-raspberrypi4-64.rootfs.wic.bz2 /dev/sdX
```

Otra opción es Raspberry Pi Imager → «Use custom» → el `.wic.bz2`.

---

## 9. Primer arranque

1. Insertar la SD en la rasp, conectar el cable de red al router y encenderla.
2. Buscar la IP que le asignó el DHCP (desde el router, o con un monitor y teclado:
   `ip a`). El hostname es `raspberrypi4-64`. No hay avahi, así que
   `raspberrypi.local` no resuelve.
3. Entrar con `ssh root@<ip>`. Root no tiene contraseña (`debug-tweaks`).
4. Si ya se había conectado a esa IP con una imagen anterior, primero hay que borrar la llave
   vieja: `ssh-keygen -R <ip>`.

### WiFi

La imagen no trae ninguna red guardada. Se agrega la primera vez, conectado por cable:

```bash
wpa_passphrase "NOMBRE_RED" "contraseña" >> /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
systemctl restart wpa_supplicant@wlan0
ip addr show wlan0
```

Queda guardada en la SD. En los arranques siguientes se conecta sola y se puede quitar
el cable. La IP por WiFi es distinta de la de Ethernet.

### SSH sin contraseña (para `devtool deploy-target` y para el alias)

En la PC nueva:

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_roomba_rpi
ssh-copy-id -i ~/.ssh/id_ed25519_roomba_rpi.pub root@<ip>
```

La imagen usa **dropbear**: la llave queda en `/home/root/.ssh/authorized_keys`. **Se
pierde cada vez que se reflashea la SD**, así que hay que repetir el `ssh-copy-id`.

Alias en `~/.ssh/config`:

```
Host roomba-rpi
    HostName <ip-de-wlan0>
    User root
    IdentityFile ~/.ssh/id_ed25519_roomba_rpi
```

Como la IP la asigna el DHCP y cambia, hay que actualizar `HostName` cuando cambie.

Para comprobar que entra con la llave y no por la falta de contraseña:
`ssh -o PreferredAuthentications=publickey roomba-rpi true`.

### Crear el primer usuario y abrir el panel

El repositorio no trae ninguna cuenta para el panel web: hay que crear al menos una en la
rasp antes de poder iniciar sesión.

```bash
# desde: la rasp, por SSH (cualquier directorio)
systemctl stop servidor
/usr/bin/crear_usuario admin 'elegir-clave' /var/lib/roomba-disco/usuarios.conf
chmod 600 /var/lib/roomba-disco/usuarios.conf
systemctl start servidor
systemctl status logica.service servidor.service --no-pager   # ambos deben estar activos
```

Después, en la PC (o en un celular de la misma red) se corre el cliente web y se escribe
`<ip>:8080` en el campo de servidor del inicio de sesión. Los pasos están en
[`Cliente/README.md`](Cliente/README.md) y en la sección 10 del [`README.md`](README.md) de la
raíz. Opcionalmente se puede medir el consumo de la imagen con `medir_metricas.sh` (sección 7
del README raíz).

---

## 10. Después del primer build

- **Recompilar tras cambiar código** en `Biblioteca/`, `Servidor/` o `Logica/`:
  `bitbake <receta>` (`libroombateca`, `servidor`, `logica`) y luego
  `bitbake core-image-minimal` para regenerar la imagen y reflashear.
- **Probar en la rasp sin reflashear:** flujo de `devtool` documentado en
  [`Yocto_min/README.md`](Yocto_min/README.md).
- **Medir rootfs, arranque, RAM y CPU:** `medir_metricas.sh` ya viene en la imagen (método y
  resultados en la sección 7 del [`README.md`](README.md) de la raíz).
- **SDK para cross-compilar a mano:** `bitbake core-image-minimal -c populate_sdk`;
  instalador en `tmp/deploy/sdk/`. Hay que regenerarlo si cambian los headers de
  `libroombateca`, porque su sysroot es una foto del momento en que se generó.

## Problemas comunes

| Síntoma | Causa / solución |
|---------|------------------|
| `Please use a locale setting which supports UTF-8` | Generar `en_US.UTF-8` (paso 1). |
| `User namespaces are not usable by BitBake` | Ubuntu 24.04 + AppArmor (paso 1). |
| Fetch falla con `git://` / código 128 | Agregar `PREMIRRORS` y `BB_FETCH_PREFERENCE` (paso 4). |
| `Nothing PROVIDES 'cjson'` | Falta `meta-oe` en las capas (paso 5). |
| `Unable to find file file://servidor.c` (o similar) | La capa `meta-robot` se movió fuera del clon (ver «Cosas a tener en cuenta»). |
| El build se detiene por espacio | `BB_DISKMON_DIRS` corta con menos de 1 GB libre: liberar disco. `rm_work` ya está activo. |
| La rasp no muestra `wlan0` | Imagen sin `kernel-module-brcmfmac-wcc`; el `local.conf` del repo ya lo incluye, revisar que no se haya borrado. |
| El sistema se cuelga o se reinicia durante el build | Demasiados hilos para la RAM: bajar `BB_NUMBER_THREADS`/`PARALLEL_MAKE`. |
