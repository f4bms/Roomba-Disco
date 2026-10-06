# 🤖 Roomba-Disco (Raspberry Pi 4)

Este repositorio contiene la infraestructura de **Linux Embebido**, la cadena de compilación cruzada (**SDK/Toolchain**) y el software de abstracción de hardware desarrollados para el robot aspiradora **Roomba-Disco**. El proyecto se basa en la distribución **Poky (Release 5.0 Scarthgap)** del **Proyecto Yocto** para la arquitectura **Raspberry Pi 4**.

---

## 1. Diagramas de Arquitectura del Sistema

### Arquitectura de Software (Capas del Sistema)
El sistema está estructurado bajo un modelo en 5 niveles:

```text
+---------------------------------------------------------------------------+
|  CAPA 1: CLIENTE WEB (Angular, en un dispositivo externo)                 |
|  - Inicio de sesión y selector de modo autónomo / manual.                 |
|  - Controles direccionales, sensores en tiempo real, estado de LEDs.      |
|  - Mapa de recorrido (grilla 2D) y control de audio (lista, play, pausa,  |
|    stop, volumen).                                                        |
+---------------------------------------------------------------------------+
                                     │  (HTTP / WebSocket en /ws, puerto 8080, mensajes JSON)
                                     ▼
+---------------------------------------------------------------------------+
|  CAPA 2: SERVIDOR WEB (servidor, C + CivetWeb)  [servidor.service]        |
|  - Autenticación de usuarios (usuarios.conf, hash SHA-256).               |
|  - Una sesión solo habla con la lógica después de autenticarse.           |
|  - Reenvía comandos y estado como JSON; no toca el hardware.              |
+---------------------------------------------------------------------------+
                                     │  (Socket Unix /run/roomba-logica.sock, JSON)
                                     ▼
+---------------------------------------------------------------------------+
|  CAPA 3: LÓGICA DE CONTROL (logica, C)  [logica.service]                  |
|  - Dueña del estado del robot (estado.json) y de su hilo de control.      |
|  - Modo manual y modo autónomo (barrido en "S" con evasión, auto.c).      |
|  - Odometría con encoders y mapa de grilla (visitado / obstáculo /        |
|    desconocido).                                                          |
+---------------------------------------------------------------------------+
                                     │  (Enlace dinámico en C: -lroombateca)
                                     ▼
+---------------------------------------------------------------------------+
|  CAPA 4: BIBLIOTECA DE ABSTRACCIÓN DE HARDWARE (libroombateca.so)         |
|  - Motores, sensores HC-SR04, LEDs, encoders y succión (PWM software).    |
|  - Audio: hilos POSIX + fork()/execlp() de mpg123 (música y avisos).      |
+---------------------------------------------------------------------------+
                                     │  (libgpiod, sysfs PWM y ALSA)
                                     ▼
+---------------------------------------------------------------------------+
|  CAPA 5: INFRAESTRUCTURA / LINUX EMBEBIDO (imagen Yocto)                  |
|  - Kernel con pwm-bcm2835 y snd-bcm2835, libgpiod, ALSA y mpg123.         |
|  - systemd (Restart=on-failure en ambos servicios), WiFi y SSH.           |
+---------------------------------------------------------------------------+
```
| Header | Qué controla | Cómo accede al hardware |
|---|---|---|
| `motores.h` | Motor izquierdo y derecho (velocidad y freno) | PWM por hardware (sysfs) y GPIO de dirección |
| `sensores.h` | Sensores ultrasónicos HC-SR04 (frontal y trasero) | GPIO con libgpiod |
| `leds.h` | Los 4 LEDs de estado | GPIO con libgpiod |
| `encoders.h` | Encoders de la odometría | GPIO, con un hilo que cuenta los pulsos |
| `succion.h` | Motor de succión | PWM por software en un hilo |
| `audio_th.h` | Música y sonidos de aviso | Hilos POSIX y `mpg123` por `fork()`/`execlp()` |
| `gpio_control.h` y `pwm_control.h` | Base común de GPIO y PWM | libgpiod y `/sys/class/pwm` |

### Arquitectura de Hardware

![Diagrama de arquitectura de hardware](diagramas/arquitectura-hardware.png)

El sistema se divide en cuatro dominios:

- **Subsistema de energía.** Pack de 3 celdas Samsung 25R 18650 en 3S1P (11,1–12,6 V,
  2,5 Ah) con BMS 3S balanceada e interruptor general en la salida. De ahí salen dos
  rieles regulados por separado: el riel de batería alimenta la etapa de potencia y un
  convertidor DC-DC XL4016 deriva el riel lógico de 5 V, que llega a la rasp por USB-C. Las
  celdas se cargan de forma individual, fuera de línea.
- **Dominio lógico** (tierra `GND_L`). Raspberry Pi 4 con imagen mínima construida con
  Yocto; expone el acceso a hardware mediante la biblioteca de control. Cuelgan de ella
  los dos sensores ultrasónicos HC-SR04 (frontal y trasero), los 4 LEDs de estado, los dos
  encoders ópticos F249 de la odometría y la salida de audio: jack de 3,5 mm →
  amplificador PAM8403 → parlante.
- **Barrera de aislamiento galvánico.** Optoacopladores PC817 en las 6 líneas de control
  del driver (`IN1`–`IN4`, `ENA`, `ENB`). Las tierras `GND_L` y `GND_P` se mantienen
  separadas y su único punto de cruce es el optoacoplador. Los pull-ups de salida se
  alimentan con el regulador de 5 V propio del L298N, nunca con el riel lógico.
- **Dominio de potencia** (tierra `GND_P`). Driver de puente H (L298N) con control de
  velocidad por PWM en `ENA`/`ENB`, y los dos motores TT de la tracción diferencial, cada
  uno con un disco ranurado que leen los encoders sin contacto eléctrico.

#### Mapa de pines GPIO (Raspberry Pi 4)

![Diagrama de conexión del header GPIO](diagramas/conexion-gpio.png)

El PWM de los motores usa `GPIO12`/`GPIO13` (los dos canales de PWM0, función ALT0).
`GPIO18`/`GPIO19` son los mismos dos canales en pines alternativos, así que quedan sin
conectar. Las señales se agrupan por mazo: motores en los pines 29–37 y sensores y LEDs
en los pines 11–24.

| Función | Pin BCM | Pin físico | Dirección | Periférico | Nota |
|---|---|---|---|---|---|
| ENA (vel. motor izq.) | GPIO12 | 32 | out | PWM0 (hw) | por optoacoplador |
| ENB (vel. motor der.) | GPIO13 | 33 | out | PWM0 (hw) | por optoacoplador |
| IN1 (dir. motor izq. A) | GPIO5 | 29 | out | GPIO | por optoacoplador |
| IN2 (dir. motor izq. B) | GPIO6 | 31 | out | GPIO | por optoacoplador |
| IN3 (dir. motor der. A) | GPIO16 | 36 | out | GPIO | por optoacoplador |
| IN4 (dir. motor der. B) | GPIO26 | 37 | out | GPIO | por optoacoplador |
| LED autónomo (azul) | GPIO22 | 15 | out | GPIO | directo, 100 Ω serie, activo en alto |
| LED manual (amarillo) | GPIO23 | 16 | out | GPIO | directo, 220 Ω serie, activo en alto |
| LED alerta obstáculo (rojo) | GPIO24 | 18 | out | GPIO | directo, 270 Ω serie, activo en alto |
| LED encendido (verde) | GPIO25 | 22 | out | GPIO | directo, 220 Ω serie, activo en alto |
| TRIG HC-SR04 frontal | GPIO10 | 19 | out | GPIO | directo (3,3 V basta para disparar) |
| ECHO HC-SR04 frontal | GPIO9 | 21 | in | GPIO | por divisor 2,2 kΩ / 3,3 kΩ (5 V → ~3 V) |
| TRIG HC-SR04 trasero | GPIO11 | 23 | out | GPIO | directo |
| ECHO HC-SR04 trasero | GPIO8 | 24 | in | GPIO | por divisor 2,2 kΩ / 3,3 kΩ |
| Encoder izquierdo | GPIO27 | 13 | in | GPIO | directo (F249 a 3,3 V) |
| Encoder derecho | GPIO17 | 11 | in | GPIO | directo (F249 a 3,3 V) |
| Succión (compuerta del IRLZ44N) | GPIO21 | 40 | out | PWM por software, ~1 kHz | por optoacoplador, señal directa |
| Audio | — | — | — | jack | el jack analógico no usa pines del header |

Los sensores ocupan `GPIO8`–`GPIO11`, que son los pines de SPI0, así que el SPI debe
quedar deshabilitado en la imagen. `GPIO2`/`GPIO3` (I2C) y `GPIO14`/`GPIO15` (UART, consola
de depuración) quedan libres, igual que `GPIO4`, `GPIO7` y `GPIO20`.

En la tracción, la salida del optoacoplador es open-collector e invierte la señal recibida;
la compensación se hace en la biblioteca de control, no en la asignación de pines. En la
succión el PC817 trabaja como seguidor de emisor hacia la compuerta del MOSFET, así que la
señal llega sin invertir.

![Circuito de la etapa de succión](diagramas/succion-mosfet.png)

La succión no tiene canal de PWM por hardware libre (PWM0 lo usa el L298N y PWM1 el jack
de audio), así que `libroombateca` genera el PWM por software en un hilo. El motor es de
7,4 V y el riel de batería llega a 12,6 V: la potencia 100 de `succion_set()` equivale a un
duty del 55 %, con rampa de arranque de 1 s.

---

## 2. Instrucciones de Generación de la Imagen Yocto

**Aviso**: En los ejemplos de código habrán direcciones que tienen ruta parecida a `home/irmunoz/Taller4/`, ajustar al usuario o directorio donde se trabajará.

### Resumen del procedimiento

La guía completa para generar y flashear la imagen en una PC nueva, con los comandos y los commits fijados de cada capa, está en [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md). Este README no lo repite; resume los pasos y documenta lo que esa guía no cubre (capas, configuración y paquetes).

| Paso | Qué se hace | Detalle en `GENERAR_IMAGEN.md` |
|---|---|---|
| 1 | Revisar requisitos de la máquina y la estructura de carpetas | §1 y §2 |
| 2 | Clonar el repo y las capas (Poky, meta-raspberrypi, meta-openembedded) fijando los commits | §2 |
| 3 | Crear el directorio de build (`oe-init-build-env rpi4`) | §3 |
| 4 | Copiar y ajustar `conf/local.conf` | §4 |
| 5 | Registrar las capas con `bitbake-layers add-layer` | §5 |
| 6 | (Opcional) reutilizar `downloads/` y `sstate-cache/` de otra PC | §6 |
| 7 | Compilar con `bitbake core-image-minimal` | §7 |
| 8 | Flashear la microSD | §8 |
| 9 | Primer arranque, WiFi y SSH sin contraseña | §9 |

### Requisitos del Host (Anfitrión)
* **SO Recomendado:** Ubuntu 22.04 LTS o 24.04 LTS.
* **Espacio Libre:** Mínimo 90 GB en disco duro.
* **Paquetes Esenciales:** `gawk`, `wget`, `git`, `diffstat`, `unzip`, `texinfo`, `gcc`, `build-essential`, `chrpath`, `socat`, `cpio`, `xz-utils`, `zstd`, `liblz4-tool`, `file`.

### Fuentes del kernel offline (opcional, en caso de que falle el fetch)

Si al compilar el kernel aparece un error de red, se puede bajar el kernel por aparte y usarlo como fuente local:

```bash
#desde: ~/Taller4
# Descargar en el navegador: https://github.com/raspberrypi/linux/archive/refs/heads/rpi-6.6.y.tar.gz
mkdir -p ~/k-robot
tar -xf linux-rpi-6.6.y.tar.gz -C ~/k-robot --strip-components=1
```

La ruta `k-robot` es personal de cada integrante, por eso **no va en el repo**: se agrega al `conf/local.conf` del build de cada quien (ver la configuración de `local.conf` más abajo). Estas son las líneas que se usan:

```text
INHERIT += "externalsrc"
EXTERNALSRC:pn-linux-raspberrypi = "/home/irmunoz/k-robot"      # ajustar a la ruta propia
```

### Lista final de capas

Rutas relativas a la carpeta de trabajo de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md) (§2).
| Capa | Ruta | Prioridad | Para qué se usa |
|---|---|---|---|
| `core` (meta) | `poky/meta` | 5 | OpenEmbedded-Core: recetas base como `busybox`, `systemd`, `alsa-lib`, `mpg123`, la clase `cmake`. |
| `yocto` (meta-poky) | `poky/meta-poky` | 5 | Configuración de la distribución `poky`. |
| `openembedded-layer` (meta-oe) | `meta-openembedded/meta-oe` | 6 | Recetas adicionales que usa el proyecto: `libgpiod` 2.1.3 (acceso a GPIO) y `cjson` 1.7.19 (JSON en la lógica y el servidor). |
| `raspberrypi` (meta-raspberrypi) | `meta-raspberrypi` | 9 | BSP de la Raspberry Pi 4: kernel `linux-raspberrypi`, device tree overlays (PWM), firmware y `config.txt`. |
| `robot` (meta-robot) | `Roomba-Disco/Yocto_min/meta-robot` | 6 | Capa propia del proyecto: recetas de `libroombateca`, `logica`, `servidor`, `cliente`, `metricas`, `wifi-config`, ajustes del kernel y de `mpg123`. |

> **Capas que agrega `oe-init-build-env` solas:** `core`, `yocto` y `yoctobsp` ya vienen registradas en el `bblayers.conf` del directorio de build, por eso [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md) (paso 5) no las agrega con `add-layer`. Solo se agregan a mano `meta-oe`, `meta-raspberrypi` y `meta-robot`.
>
> `mpg123` (1.32.10) sale de `core` (`meta/recipes-multimedia`), por eso no hace falta `meta-multimedia`.

> **Carpetas `Yocto/` y `Yocto_min/`:** el repositorio mantiene las dos. Comparten las recetas de `meta-robot`; `Yocto_min/` es la versión reducida: su `local.conf` es el mínimo que se muestra más abajo, no incluye la receta de Tailscale ni el fragmento `tun.cfg` del kernel, y su `bbappend` de `mpg123` quita PulseAudio. La imagen entregada se genera desde `Yocto_min/`; `Yocto/` conserva su `local.conf` de desarrollo.

### Paso 4: Configuración del Sistema (`conf/local.conf`)

La configuración del proyecto está versionada en el repo (`Yocto_min/local.conf`) y se copia al directorio de build como indica el paso 4 de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md). Se agregan líneas personales si hace falta (por ejemplo la ruta del kernel offline).

Las líneas importantes del archivo local.conf son:
```text
# [Especifica el HW al que se va a cargar] en este caso a para nuestra rasp en 64bits
MACHINE ?= "raspberrypi4-64"
# borra el directorio de trabajo de cada receta al terminar
INHERIT += "rm_work"
DISTRO_FEATURES:append = " systemd usrmerge"
VIRTUAL-RUNTIME_init_manager = "systemd"
VIRTUAL-RUNTIME_initscripts = ""
# [SSH] servidor dropbear; root sin contraseña mientras exista debug-tweaks (solo desarrollo)
EXTRA_IMAGE_FEATURES ?= "debug-tweaks"
EXTRA_IMAGE_FEATURES:append = " ssh-server-dropbear"
# [Política de licencias] Deja compilar componentes con restricciones de patentes y firmware propietario
LICENSE_FLAGS_ACCEPTED:append = " synaptics-killswitch"
LICENSE_FLAGS_ACCEPTED:append = " commercial"
# [Audio, GPIO, métricas y aplicación] (ver tabla de paquetes más abajo)
IMAGE_INSTALL:append = " alsa-lib mpg123 libgpiod systemd-analyze metricas logica libroombateca servidor kernel-module-snd-bcm2835"
KERNEL_MODULE_AUTOLOAD:append = " snd-bcm2835"
# [Motores] 2 canales PWM por hardware en GPIO12 (ENA) y GPIO13 (ENB), más el jack analógico
RPI_KERNEL_DEVICETREE_OVERLAYS:append = " overlays/pwm-2chan.dtbo"
RPI_EXTRA_CONFIG = "dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4\ndtparam=audio=on"
IMAGE_INSTALL:append = " kernel-module-pwm-bcm2835"
KERNEL_MODULE_AUTOLOAD:append = " pwm-bcm2835"
# [Audio] sin audio por HDMI: el jack analógico queda como tarjeta ALSA por defecto
VC4DTBO = "vc4-kms-v3d,noaudio"
# [WiFi] viene integrado (wlan0)
IMAGE_INSTALL:append = " kernel-module-brcmfmac kernel-module-brcmfmac-wcc linux-firmware-rpidistro-bcm43455 wireless-regdb-static wifi-config"
# [Reducción de tamaño] sin teclado ni pantalla: fuera la consola virtual y la base de datos de hardware de udev
PACKAGECONFIG:remove:pn-systemd = "vconsole"
BAD_RECOMMENDATIONS += "udev-hwdb"
# (OPCIONAL) [Optimización de recursos en PC host] limita a 5 hilos en paralelo para la estabilidad de la VM
BB_NUMBER_THREADS = "5"
PARALLEL_MAKE = "-j 5"
```

Las líneas de `externalsrc` del kernel offline **no** van en este archivo: son personales y se agregan en el `conf/local.conf` de cada build.

### Paquetes agregados a la imagen mínima y su justificación

La base es `core-image-minimal`. Todo lo siguiente se agrega encima de ella:

| Paquete | Justificación |
|---|---|
| `alsa-lib` | Librería de audio que usa `mpg123` para sacar sonido por el jack |
| `mpg123` | Decodificador y reproductor de MP3 que invoca `audio_th.c` | 
| `kernel-module-snd-bcm2835` | Controlador de audio analógico de la rasp | 
| `libgpiod` | Acceso a GPIO por el dispositivo de caracteres (sensores y LEDs) desde `libroombateca`. |
| `kernel-module-pwm-bcm2835` | Controlador del PWM por HW para los motores | 
| `libroombateca` | Biblioteca dinámica propia con el acceso al hardware | 
| `logica` | Proceso que mantiene el estado del robot y ejecuta los modos manual y autónomo usando `libroombateca` | 
| `servidor` | Servidor web/WebSocket con autenticación; se comunica con `logica` | 
| `systemd-analyze` | Para la medición del tiempo de arranque (`time`, `blame`, `critical-chain`) |
| `metricas` | Script `medir_metricas.sh` que genera el reporte de rootfs, arranque, RAM y CPU. | 
| `ssh-server-dropbear` | Acceso remoto por SSH (servidor dropbear) para controlar y medir el sistema | 
| `kernel-module-brcmfmac`, `kernel-module-brcmfmac-wcc`, `linux-firmware-rpidistro-bcm43455`, `wireless-regdb-static`, `wifi-config` | WiFi integrado de la RaspberryPi 4 (driver, firmware, regulación y la receta propia que levanta `wlan0`). | 

Se instalan **automáticamente** por dependencia, sin estar en `IMAGE_INSTALL`: 
- `cjson` (biblioteca compartida de `logica` y `servidor`) 
- `mpg123` (dependencia de `libroombateca`) 
- `wpa-supplicant` con sus utilidades (dependencias de `wifi-config`)

**Opciones de configuración que cambian el contenido de la imagen:**

| Opción | Efecto |
|---|---|
| `VC4DTBO = "vc4-kms-v3d,noaudio"` | Sin audio por HDMI: el jack analógico queda como tarjeta ALSA por defecto. |
| `PACKAGECONFIG:remove:pn-systemd = "vconsole"` | Quita la consola virtual de `systemd` (el sistema no tiene teclado ni pantalla). |
| `BAD_RECOMMENDATIONS += "udev-hwdb"` | No instala la base de datos de hardware de `udev`, que no hace falta y ocupa espacio. |
| `EXTRA_IMAGE_FEATURES ?= "debug-tweaks"` | Root sin contraseña; es una opción de desarrollo. |

### Paso 7: Compilación de la imagen

La imagen final se reproduce desde cero con `bitbake core-image-minimal`, sin pasos manuales adicionales (paso 7 de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md)). Conviene compilar primero las recetas propias, porque los errores aparecen más rápido:

```bash
# desde: el directorio de build rpi4/ (con el entorno cargado)
bitbake libroombateca logica servidor metricas
bitbake core-image-minimal
```

---

## 3. Recetas Propias de la Capa `meta-robot`

La capa [`Yocto_min/meta-robot/`](Yocto_min/meta-robot) contiene las recetas BitBake del proyecto. Todas toman el código desde las carpetas del repo (`Biblioteca/`, `Logica/`, `Servidor/`, `Cliente/`) mediante `FILESEXTRAPATHS`, así que no se copia código directamente dentro de la capa(layer).

### 3.1 Receta principal: `libroombateca_1.0.bb`

Con cross-compile usando **CMake** la biblioteca dinámica `libroombateca.so`, encapsula: motores (PWM), sensores , LEDs (GPIO) y la reproducción de audio.

**Ubicación del archivo en la capa:** `Yocto_min/meta-robot/recipes-apps/libroombateca/libroombateca_1.0.bb`

```text
SUMMARY = "Biblioteca GPIO"
DESCRIPTION = "Metadatos en CMake cross-compile el control de perifericos e hilos de audio"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Clase CMake
inherit cmake

# gpio_control.c enlaza contra libgpiod (API v2, char device)
DEPENDS += "libgpiod"

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

#Le dice a Yocto que empaquete la biblioteca compartida (.so) y headers (.h)
FILES:${PN} = "${libdir}/lib*.so"
FILES:${PN} += "${includedir}/*.h"
FILES:${PN} += "${datadir}/roomba-disco/audio/*.mp3"
do_install:append() {
    install -d ${D}${datadir}/roomba-disco/audio
    install -m 0644 ${S}/audio/*.mp3 ${D}${datadir}/roomba-disco/audio/
}

# Vacia el paquete -dev para que no busque el archivo .so plano
FILES:${PN}-dev = ""

# Desactiva validación de control para ambas variantes
INSANE_SKIP:${PN} += "dev-elf"
INSANE_SKIP:${PN}-dev += "dev-elf"
```
Qué hace cada bloque:

| Parte | Qué hace |
|---|---|
| `inherit cmake` | Usa el sistema de construcción CMake con la toolchain cruzada de Yocto |
| `DEPENDS += "libgpiod"` | Dependencia **de compilación**: necesita los headers de `libgpiod` para `gpio_control.c` |
| `FILESEXTRAPATHS:prepend` y `SRC_URI` | Le dicen a BitBake dónde está el código (`Biblioteca/`) y qué copiar: `CMakeLists.txt`, `include/`, `lib/` y los MP3 de `audio/` |
| `S = "${WORKDIR}"` | El código fuente queda directamente en el directorio de trabajo |
| `RDEPENDS:${PN} = "mpg123"` | Dependencia **de ejecución**: `audio_th.c` lanza `mpg123` para reproducir |
| `FILES:${PN}` y `do_install:append` | Empaquetan la `.so`, headers y MP3 (instalados en `/usr/share/roomba-disco/audio`) |
| `FILES:${PN}-dev = ""` e `INSANE_SKIP` | No dejan que Yocto reclame la `.so` sin versión como paquete de desarrollo y silencian esa validación |

### 3.2 Otras recetas de la capa

| Receta | Ruta en `Yocto_min/meta-robot/` | Qué hace | Se instala en la imagen como |
|---|---|---|---|
| `logica_1.0.bb` | `recipes-apps/logica/` | Proceso C dueño del estado del robot: compila `logica.c`, `roombateca_control.c`, `auto.c` (modo autónomo), `odometria.c` y `mapa.c`; enlaza `-lcjson -lroombateca -lpthread -lm`. Atiende JSON por un socket Unix. | `/usr/bin/logica`, `/usr/share/roomba-disco/estado.json` y `logica.service` (`Restart=on-failure`, socket `/run/roomba-logica.sock`) |
| `servidor_1.0.bb` | `recipes-apps/servidor/` | Servidor web y WebSocket (CivetWeb en `third_party/`) con autenticación (`auth/`, hash SHA-256). Habla con la lógica por el socket Unix. Depende de `cjson` y de `logica`. | `/usr/bin/servidor`, `/usr/bin/crear_usuario` y `servidor.service` (puerto 8080, `Restart=on-failure`, arranca después de `logica.service`) |
| `metricas_1.0.bb` | `recipes-apps/metricas/` | Instala el script `medir_metricas.sh` que mide rootfs, arranque, RAM y CPU (sección 7). Solo necesita `busybox`. | `/usr/bin/medir_metricas.sh` |
| `wifi-config_1.0.bb` | `recipes-connectivity/wifi-config/` | Levanta `wpa_supplicant` en `wlan0` al arrancar y da DHCP con `systemd-networkd`. Las redes se agregan en la rasp con `wpa_passphrase` (ver el paso 9 de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md)). | `/etc/systemd/network/25-wlan.network`, `/etc/wpa_supplicant/wpa_supplicant-wlan0.conf` |
| `mpg123_%.bbappend` | `recipes-multimedia/mpg123/` | Poky agrega `pulseaudio` a `DISTRO_FEATURES` por defecto y con eso `mpg123` se compilaría contra PulseAudio, que arrastra `libpulse`, `libsndfile` y bibliotecas de X11. El `bbappend` quita `pulseaudio` y deja solo la salida ALSA (`mpg123 -o alsa`), que es la que usa la biblioteca. | Cambia cómo se compila `mpg123` |
| `linux-raspberrypi_6.6.bbappend` | `recipes-kernel/linux/` | Ajustes del kernel: `KERNEL_DANGLING_FEATURES_WARN_ONLY` y `KERNEL_VERSION_SANITY_SKIP` | Cambia la configuración del kernel |
| `cliente_1.0.bb` | `recipes-apps/cliente/` | Compila el cliente Angular a archivos estáticos (`npm ci` y `npm run build`, usando `nodejs-native` solo en el host). | **No se instala** (ver abajo) |

### 3.3 Receta `cliente` no entra en la imagen

El cliente corre en un dispositivo externo y se conecta a la rasp por WebSocket. La rasp solo ejecuta `logica` y `servidor`. Por eso `cliente` está en el layer, para poder generar los estáticos cuando se necesiten, pero no aparece en `IMAGE_INSTALL`. Así la imagen no carga los archivos del cliente y se mantiene el rootfs por debajo del limite requerido de 200 MB.

---

## 4. Instalación y Compilación con la Toolchain (SDK)

### Instalación del SDK Cruzado
El SDK generado por Yocto se encuentra instalado en la ruta fija del Host:
`/opt/poky/5.0.20/` (ruta por defecto del instalador; puede cambiarse con `-d`).

### Carga del Entorno de Compilación
Cada vez que se abra una terminal nueva para compilar código de forma manual o local, se debe ejecutar el script para inicializar variables:
```bash
source /opt/poky/5.0.20/environment-setup-cortexa72-poky-linux
```

### Validación Corta del Entorno
* **Verificación de Variable `$CC`:** 
  ```text
  bash: echo $CC
  ```
Al ejecutar `echo $CC` se demuestra que el compilador apunta al toolchain cruzado de ARM, este se describe en su salida: `aarch64-poky-linux-gcc -mcpu=cortex-a72+crc -mbranch-protection=standard -fstack-protector-strong -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security --sysroot=/opt/poky/5.0.20/sysroots/cortexa72-poky-linux`

* **Compilación Manual de Prueba:**
  ```bash
  $CC helloworld.c -o test_arm
  ```
* **Prueba de Incompatibilidad en Host (Error Controlado):** Al intentar correr el binario en Ubuntu (`./test_arm`), el procesador x86_64 no lo entiende, ya que no está compilado para él:
  ```text
  bash: ./test_arm: cannot execute binary file: Exec format error
  ```
* **Emulación con QEMU:** Para probar el binario localmente sin la placa física:
  ```bash
  sudo apt-get install qemu-user
  qemu-aarch64 -L $SDKTARGETSYSROOT ./test_arm

  # Salida: ~~~¡Genial! Prueba sencilla de Roomba-disco desde la Raspberry Pi 4~~~
  ```

---

## 5. Evidencias de Cross-Compile

### 5.1 Fragmento del log.do_compile
Compilación cruzada de `libroombateca` con la toolchain de Yocto (`cortexa72-poky-linux`). Ruta del log en el build: `tmp/work/*/libroombateca/*/temp/log.do_compile`. El log completo está en [`evidencias/log.do_compile_libroombateca.txt`](evidencias/log.do_compile_libroombateca.txt).

```text
DEBUG: Executing shell function do_compile
NOTE: VERBOSE=1 cmake --build <workarea>/poky-scarthgap-5.0.15/rpi4/tmp/work/cortexa72-poky-linux/libroombateca/1.0/build --target all --
Run Build Command(s): ninja -v -j 10 all
[1/9] <workarea>/.../recipe-sysroot-native/usr/bin/aarch64-poky-linux/aarch64-poky-linux-gcc --sysroot=<workarea>/.../recipe-sysroot -DROOMBATECA_HARDWARE -mcpu=cortex-a72+crc -mbranch-protection=standard -fstack-protector-strong -O2 -std=gnu11 -fPIC ... -c lib/leds.c -o CMakeFiles/roombateca.dir/lib/leds.c.o
[2/9] ... (se compilan los demás módulos de lib/ con el mismo compilador cruzado)
...
[9/9] <workarea>/.../recipe-sysroot-native/usr/bin/aarch64-poky-linux/aarch64-poky-linux-gcc --sysroot=<workarea>/.../recipe-sysroot -mcpu=cortex-a72+crc -mbranch-protection=standard -fstack-protector-strong -O2 -fPIC ... (enlace de libroombateca.so)

DEBUG: Shell function do_compile finished
```

Se ve que el compilador es `aarch64-poky-linux-gcc` con `--sysroot` del recipe y `-mcpu=cortex-a72+crc` (no el `gcc` del host), y que `cmake` y `ninja` terminaron los 9 pasos sin errores.

### 5.2 Resumen de la construcción de la imagen

Resumen de `bitbake core-image-minimal`:

```text
NOTE: Resolving any missing task queue dependencies
Build Configuration:
BB_VERSION           = "2.8.1"
BUILD_SYS            = "x86_64-linux"
NATIVELSBSTRING      = "universal"
TARGET_SYS           = "aarch64-poky-linux"
MACHINE              = "raspberrypi4-64"
DISTRO               = "poky"
DISTRO_VERSION       = "5.0.20"
TUNE_FEATURES        = "aarch64 crc cortexa72"
TARGET_FPU           = ""
meta
meta-poky
meta-yocto-bsp       = "HEAD:69ae79bf5a01a24491648e2fdea6faf51aeb3bf2"
meta-raspberrypi     = "scarthgap:6ca1f75017cc5d5acdb8bb05634c4bc01fa049fd"
meta-robot           = "<unknown>:<unknown>"
meta-oe              = "scarthgap:0f00f8b9a21950640da8c5707343e5540133f86e"
...
Sstate summary: Wanted 0 Local 0 Mirrors 0 Missed 0 Current 2283 (100% match, 100% complete)
NOTE: Executing Tasks
NOTE: Tasks Summary: Attempted 6022 tasks of which 6022 didn't need to be rerun and all succeeded.
```

`TARGET_SYS = aarch64-poky-linux`, `MACHINE = raspberrypi4-64` y `TUNE_FEATURES = aarch64 crc cortexa72` confirman que la imagen se compila para ARM de 64 bits, el Cortex-A72 de la Raspberry Pi 4. El `Sstate summary` final muestra `Wanted 0` y 100 % de coincidencia, y las 6022 tareas se resolvieron sin rehacerse (`didn't need to be rerun`): la imagen se reconstruye completa desde el caché, sin recompilar nada y sin errores.

**Versiones de las capas:** este resumen corresponde a una construcción hecha con Poky `69ae79bf5a` y meta-oe `0f00f8b9a2`. La imagen entregada se generó con Poky `77d1feb` y meta-oe `b5874ea`, que son los commits que fija [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md); `meta-raspberrypi` es `6ca1f75` en ambas. Todos son commits de la rama `scarthgap`.

### 5.3 Ejecución en la Raspberry Pi 4 (target)

Salida real de la Raspberry Pi con la imagen entregada, generada por `medir_metricas.sh` (archivo completo: [`evidencias/metricas_20261006_081800.txt`](evidencias/metricas_20261006_081800.txt)):

```text
Fecha (de la rasp):  Tue Oct  6 08:18:00 UTC 2026
Placa:           Raspberry Pi 4 Model B Rev 1.2
Kernel:          Linux 6.6.63-v8 (aarch64)
Estado de systemd: running
Unidades fallidas: 0

servidor.service @1.901s
`-logica.service @1.853s +41ms
  `-basic.target @1.830s
    ...
logica.service activo a los: 3.184 s desde el inicio del kernel
servidor.service activo a los: 3.192 s desde el inicio del kernel
```

---

## 6. Documentación de la API de la Biblioteca Dinámica

`libroombateca.so` encapsula todo el acceso al hardware: motores (PWM), sensores de proximidad, LEDs, encoders, succión y audio. No existe un header único: la API está repartida en siete headers de [`Biblioteca/include/`](Biblioteca/include), todos con `extern "C"` para poder usarse desde C o C++.

### 6.1 Convenciones generales

| Tema | Regla |
|---|---|
| Ciclo de vida | Cada módulo tiene un `*_init()` que se llama antes de usarlo y un `*_cleanup()` que libera pines, PWM, hilos y procesos. |
| Orden entre módulos | `motor_control_init()` debe llamarse **antes** de `encoders_init()`, porque el signo del conteo sale del sentido de giro de los motores. |
| Códigos de retorno | `0` en éxito o `-errno` en error (`-EINVAL` valor fuera de rango, `-ENODEV` módulo sin inicializar, `-ETIMEDOUT` sin eco, `-EIO` sensor sin respuesta). Excepción: `pinMode` y `digitalRead` devuelven `-1` en error. |
| Unidades | Velocidad de motores en porcentaje con signo `[-100, 100]`; potencia de succión `[0, 100]`; distancias de sensores en cm y de encoders en mm; velocidades en mm/s; volumen `[0, 100]`; tiempos en ns con `CLOCK_MONOTONIC`. |
| Pines | Los pines BCM de cada señal están en [`Biblioteca/lib/pinout.h`](Biblioteca/lib/pinout.h) y en la tabla de la sección 1. |

### 6.2 Motores (`motores.h`)

Control diferencial de los dos motores DC con PWM por hardware (ENA/ENB en GPIO12/13) y pines de dirección por GPIO.

| Función | Parámetros | Retorno | Descripción |
|---|---|---|---|
| `motor_control_init()` | — | `0` o `-errno` | Configura PWM y GPIO de ambos motores y los deja detenidos. |
| `motor_control_cleanup()` | — | — | Detiene los motores y libera GPIO y PWM. |
| `motor_izquierdo_set(v)` / `motor_derecho_set(v)` | `v` en `[-100, 100]` (`MOTOR_VELOCIDAD_MAX` = 100) | `0`, `-EINVAL` fuera de rango o `-ENODEV` sin init | El signo es el sentido (positivo = adelante) y la magnitud es la fracción de la velocidad máxima segura. `0` deja la rueda libre. |
| `motor_izquierdo_get()` / `motor_derecho_get()` | — | Última velocidad aplicada | Es `0` tras `motores_frenar()` o sin init. |
| `motores_frenar()` | — | `0` o `-errno` | Freno dinámico de ambos motores; se sale llamando a cualquier `*_set`. |

Internamente, la velocidad se traduce a un ciclo de trabajo del PWM entre `MOTOR_DUTY_MIN_PCT` (25 %, para saltar la zona muerta del motor) y `MOTOR_DUTY_MAX_PCT` (90 %), y se deja una pausa de rueda libre de 30 ms al invertir el sentido de giro.

Giros con radio variable: se obtienen asignando velocidades distintas a cada rueda (por ejemplo `50` y `20`); con signos opuestos el robot gira sobre su eje.

### 6.3 Sensores de proximidad (`sensores.h`)

Dos sensores ultrasónicos HC-SR04, identificados con `sensor_id_t`: `SENSOR_FRONTAL` y `SENSOR_TRASERO`.

| Función | Parámetros | Retorno | Descripción |
|---|---|---|---|
| `sensores_init()` | — | `0` o `-errno` | Configura TRIG y ECHO de ambos sensores. |
| `sensores_cleanup()` | — | — | Libera los recursos del módulo. |
| `sensor_medir(id, &cm)` | `id`: sensor; `cm`: puntero a `float` | `0` con la distancia en `*cm`; `-ETIMEDOUT` sin eco; `-EIO` sensor sin respuesta (¿desconectado?); `-ENODEV` sin init | Dispara y mide. Rango útil de `SENSOR_DISTANCIA_MIN_CM` (2 cm) a `SENSOR_DISTANCIA_MAX_CM` (400 cm). |

Concurrencia: `sensor_medir` es seguro entre hilos. Garantiza `SENSOR_INTERVALO_MIN_MS` (60 ms) entre dos disparos cualesquiera para no leer ecos del disparo anterior, así que puede bloquear ese tiempo más la duración del eco (hasta unos 25 ms).

### 6.4 LEDs (`leds.h`)

| Función | Parámetros | Retorno | Descripción |
|---|---|---|---|
| `leds_init()` | — | `0` o `-errno` | Configura los pines y deja todos los LEDs apagados. |
| `leds_cleanup()` | — | — | Apaga todos y libera los pines. |
| `led_set(led, encendido)` | `led`: `led_id_t`; `encendido`: `bool` | `0` o `-errno` | Enciende o apaga un LED. |
| `led_get(led)` | `led`: `led_id_t` | `1` encendido, `0` apagado o `-errno` | Estado actual. |

`led_id_t`: `LED_ENCENDIDO` (verde), `LED_ALERTA` (rojo), `LED_MANUAL` (amarillo), `LED_AUTONOMO` (azul).

### 6.5 Encoders y odometría (`encoders.h`)

| Función | Parámetros | Retorno | Descripción |
|---|---|---|---|
| `encoders_init()` | — | `0` o `-errno` | Configura ambos encoders y arranca el hilo de conteo. Requiere `motor_control_init()` antes. |
| `encoders_cleanup()` | — | — | Libera los recursos de los encoders. |
| `encoder_leer(id, &lectura)` | `id`: `ENCODER_IZQUIERDO` o `ENCODER_DERECHO` | `0` o `-errno` | Lee un encoder. |
| `encoders_leer_todos(lecturas)` | arreglo de `ENCODER_CANTIDAD` elementos | `0` o `-errno` | Lee ambos con una sola actualización temporal, lo que reduce el error de la odometría. |
| `encoders_reset()` | — | `0` o `-errno` | Pone a cero los contadores. |

`encoder_lectura_t` contiene `pulsos` (acumulados con signo desde el último reset), `distancia_mm`, `velocidad_mm_s` (con signo, `0` si la rueda está quieta) y `ultimo_pulso_ns` (`CLOCK_MONOTONIC`, `0` si aún no hubo pulsos). Las lecturas son seguras entre hilos.

### 6.6 Succión (`succion.h`)

| Función | Parámetros | Retorno | Descripción |
|---|---|---|---|
| `succion_init()` | — | `0` o `-errno` | Configura el pin y arranca el PWM con el motor apagado. |
| `succion_cleanup()` | — | — | Apaga el motor y libera el pin. |
| `succion_set(p)` | `p` en `[0, 100]` (`SUCCION_POTENCIA_MAX`) | `0`, `-EINVAL` o `-ENODEV` | `0` apaga y `100` es el máximo seguro del motor. Al subir, el motor acelera con rampa. |
| `succion_get()` | — | Última potencia pedida | `0` sin init. |

No hay un canal de PWM por hardware libre para este motor (PWM0 lo usa el L298N y PWM1 el jack de audio), por eso la biblioteca genera el PWM por software en un hilo propio.

### 6.7 Audio (`audio_th.h`)

Hay dos canales independientes: una **pista de música** (un proceso `mpg123` en modo remoto, controlado por comandos, con un hilo que lee su estado) y **sonidos de notificación** (procesos `mpg123 -q` lanzados desde hilos o esperados). La música y la navegación corren en paralelo. En ambos canales `mpg123` se lanza con salida ALSA (`-o alsa`).

| Función | Parámetros | Retorno | Descripción |
|---|---|---|---|
| `audio_control_init()` | — | `0` o `-errno` | Inicializa el canal de música en `AUDIO_STOP`. Si ya estaba iniciado devuelve `0`. |
| `audio_control_cleanup()` | — | — | Detiene los sonidos y libera los recursos. |
| `audio_play(path)` | ruta absoluta del `.mp3` | `0`, `-EINVAL` ruta nula o `-ENAMETOOLONG` | Carga y reproduce una pista; reemplaza la actual. |
| `audio_pause()` / `audio_resume()` | — | `0` o `-errno` | Pausa o reanuda; no hacen nada (devuelven `0`) si el estado no corresponde. |
| `audio_stop()` | — | `0` o `-errno` | Detiene la música. |
| `audio_get_state()` | — | `AUDIO_STOP`, `AUDIO_PLAY` o `AUDIO_PAUSA` | Estado actual del canal de música. |
| `audio_set_volume(v)` / `audio_get_volume()` | `v` en `[0, 100]` | `0` o `-EINVAL` / volumen actual | Ajusta y consulta el volumen. |
| `trigger_notification_audio(path)` | ruta del `.mp3` | — | Reproduce un aviso **sin bloquear** (en un hilo). |
| `play_notification_wait(path)` | ruta del `.mp3` | `0`, `-EINVAL` o `-errno` | Reproduce un aviso y **espera** a que termine. |

Los archivos viven en `/usr/share/roomba-disco/audio/` (`AUDIO_DIR`). Constantes disponibles: `AUDIO_INICIO_SYS` (`arranque.mp3`), `AUDIO_AUTO_MODE` (`pirin.mp3`), `AUDIO_ALERTA` (`alerta.mp3`), `AUDIO_MANUAL_MODE` (`ding.mp3`) y `AUDIO_TRACK_1` a `AUDIO_TRACK_3` (`1.mp3` a `3.mp3`). Si `mpg123` no está disponible, las funciones de audio devuelven error pero el resto de la biblioteca sigue funcionando.

### 6.8 GPIO genérico (`gpio_control.h`)

Capa base sobre `libgpiod` (API v2, dispositivo de caracteres) que usan los demás módulos; también se puede usar directamente.

| Función | Descripción |
|---|---|
| `pinMode(pin, modo)` | Configura un pin como `GPIO_INPUT` o `GPIO_OUTPUT`. Devuelve `0` o `-1`. |
| `pinModeEx(pin, modo, flags, debounce_us)` | Igual, con opciones `GPIO_FLAG_*` (`ACTIVE_LOW`, `PULL_UP`, `PULL_DOWN`, `EDGE_RISING`, `EDGE_FALLING`, `EDGE_BOTH`) y filtro de rebotes del kernel en entradas. Devuelve `0` o `-errno`. |
| `digitalWrite(pin, valor)` / `digitalRead(pin)` | Escribe `0` o `1` en una salida / lee una entrada o salida (`0`, `1` o `-1`). |
| `waitEdge(pin, timeout_ns, &evento)` | Espera un flanco: `1` si llegó (con `tipo` y `timestamp_ns`), `0` si venció el tiempo, `-errno` en error. `timeout_ns < 0` espera sin límite y `0` no bloquea. |
| `edgeFd(pin)` | Descriptor para `poll()` o `select()` sobre los flancos de un pin. No se debe cerrar. |
| `blink(pin, hz, segundos)` | Alterna un pin a la frecuencia dada durante el tiempo indicado (bloqueante). |
| `pinRelease(pin)` / `gpio_control_cleanup()` | Libera un pin / libera todos y cierra el chip. Dejar el pin en estado seguro antes de liberarlo. |

### 6.9 Compilación de la biblioteca

La biblioteca se compila con CMake (`Biblioteca/CMakeLists.txt`) y depende de `pthread` y, con soporte de hardware, de `libgpiod`.

| Opción de CMake | Efecto |
|---|---|
| `ROOMBATECA_HARDWARE=ON` (por defecto) | Compila el acceso real a GPIO, PWM, motores, sensores y encoders. Es lo que usa la receta de Yocto. |
| `ROOMBATECA_HARDWARE=OFF` | Usa motores, sensores y encoders simulados (sin `libgpiod`), para probar en una PC sin la placa. |
| `ROOMBATECA_SIM_INJECT=ON` | En simulación, los sensores leen sus valores de un archivo (`ROO_SIM_SENSORS_FILE`). |
| `ROOMBATECA_AUDIO_DIR=<ruta>` | Cambia la carpeta de los `.mp3`. |

### 6.10 Ejemplo de uso

```c
#include <stdio.h>
#include <unistd.h>
#include "motores.h"
#include "sensores.h"
#include "leds.h"
#include "audio_th.h"

int main(void) {
    if (motor_control_init() != 0 || sensores_init() != 0 || leds_init() != 0 ||
        audio_control_init() != 0) {
        fprintf(stderr, "no se pudo inicializar la biblioteca\n");
        return 1;
    }

    led_set(LED_ENCENDIDO, true);
    trigger_notification_audio(AUDIO_INICIO_SYS);

    for (int i = 0; i < 20; i++) {
        float d;
        int rc = sensor_medir(SENSOR_FRONTAL, &d);
        if (rc == 0 && d > 25.0f) {
            motor_izquierdo_set(50);               /* 50 % hacia adelante */
            motor_derecho_set(50);
            led_set(LED_ALERTA, false);
        } else {
            motores_frenar();                      /* obstáculo o sin eco */
            led_set(LED_ALERTA, true);
        }
        usleep(100 * 1000);
    }

    motores_frenar();
    audio_control_cleanup();
    leds_cleanup();
    sensores_cleanup();
    motor_control_cleanup();
    return 0;
}
```

Compilar de forma cruzada con el SDK (el mismo flujo de la sección 9):

```bash
# desde: la carpeta que contiene ejemplo.c, include/ y libroombateca.so (con el entorno del SDK cargado)
$CC ejemplo.c -I./include -L. -lroombateca -lpthread -o ejemplo
```

---

## 7. Reporte de Métricas de Recursos
Las métricas se obtienen con el script [`medir_metricas.sh`](Yocto_min/meta-robot/recipes-apps/metricas/files/medir_metricas.sh), que la receta `metricas` instala en la imagen como `/usr/bin/medir_metricas.sh`. Usa: `/proc`, `/sys` y `systemd`.

### 7.1 Método de medición

| Métrica | Cómo se mide | Herramienta |
|---|---|---|
| Tamaño del rootfs | Espacio usado de la partición root | `df -k /` y `du -xsk /` |
| Tiempo de arranque | Tiempo desde que inicia el kernel hasta que `servidor.service` queda activo | `systemd-analyze time`, `critical-chain`, `blame` y `systemctl show -p ActiveEnterTimestampMonotonic` |
| RAM del sistema | `MemTotal - MemAvailable`, muestreada cada intervalo | `/proc/meminfo` |
| CPU del sistema | Diferencia de jiffies ocupados contra totales entre dos lecturas | `/proc/stat` |
| RAM y CPU por proceso | RSS, RSS pico y PSS; CPU como porcentaje de un núcleo entre inicio y fin de la ventana | `/proc/<pid>/status`, `smaps_rollup` y `stat` |
| Temperatura del SoC | Lectura directa | `/sys/class/thermal/thermal_zone0/temp` |

**Cómo usarlo (ejecutarlo):**

```bash
# desde: la rasp, por SSH
medir_metricas.sh -d 30 -i 2 -o /tmp/metricas_normal.txt
```

```bash
# desde: ~/Taller4/Roomba-Disco  (en el host)
scp root@<IP_RASPBERRY>:/tmp/metricas_normal.txt .
```

Opciones: `-d` duración del muestreo en segundos, `-i` intervalo, `-o` archivo `.txt` de salida y `-p` lista de procesos a medir (por defecto `logica servidor mpg123`).

**Medición del arranque:** se reinicia la rasp, se espera a que conecte y se corre el script, este cuenta desde el inicio del kernel y **no** incluye el tiempo del firmware y el bootloader de la rasp.

**Evidencia original:** [`evidencias/metricas_20261006_081800.txt`](evidencias/metricas_20261006_081800.txt), generado en la Raspberry Pi el Tue Oct  6 08:18:00 UTC 2026 (a los 239.24 s de haber arrancado).

### 7.2 Resultados

#### Tamaño del rootfs

| Métrica | Valor | Referencia | Estado |
|---|---|---|---|
| Espacio usado en `/` (`df`) | 80.35 MB | ≤ 200 MB | CUMPLE |
| Contenido real (`du -xsk /`) | 56.19 MB | — | — |
| Tamaño total de la partición raíz | 160.66 MB | — | — |
| Partición `/boot` (aparte, no cuenta como rootfs) | 47.99 MB de 129.84 MB | — | — |

#### Tiempo de arranque

| Métrica | Valor | Referencia | Estado |
|---|---|---|---|
| Kernel (`systemd-analyze`) | 1.290 s | — | — |
| Userspace (`systemd-analyze`) | 2.130 s | — | — |
| Inicio de userspace (desde el inicio del kernel) | 1.290 s | — | — |
| Kernel → `logica.service` activo | 3.184 s | — | — |
| Kernel → `servidor.service` activo (control operativo) | 3.192 s | ≤ 15 s | CUMPLE |
| Kernel → fin de arranque de systemd | 3.421 s | — | — |
| Firmware y bootloader → inicio del kernel | _no lo mide el script_ | — | — |

Cadena crítica de `servidor.service`: arranca después de `logica.service` (`@1.853s`, `+41ms`), que a su vez espera a `basic.target`. La unidad más lenta del arranque fue `dev-mmcblk0p2.device` (584 ms).

#### RAM y CPU en operación normal

| Condición | Valor |
|---|---|
| Duración y intervalo de muestreo | 30 s cada 2 s |
| Momento de la medición | 239.24 s después del arranque |
| Canción reproduciéndose | Sí: `mpg123` estaba activo (PID 250) |
| Temperatura del SoC (promedio) | 46.0 °C |

| Métrica | Promedio | Máximo |
|---|---|---|
| CPU del sistema (todos los núcleos) | 1.4 % | 2.1 % |
| RAM usada (`MemTotal - MemAvailable`) | 97.66 MB | 98.21 MB |
| RAM total de la placa | 1845.29 MB | — |

| Proceso | PID | CPU (% de un núcleo) | RSS | RSS pico | PSS |
|---|---|---|---|---|---|
| `logica` | 239 | 4.4 | 2.37 MB | 2.37 MB | 1.15 MB |
| `servidor` | 240 | 0.0 | 1.75 MB | 1.75 MB | 0.47 MB |
| `mpg123` | 250 | 1.1 | 3.12 MB | 3.12 MB | 1.78 MB |

### 7.3 Análisis y justificación de desviaciones

Ningún valor supera las referencias de la especificación, así que **no hay desviaciones que justificar**:

- **Rootfs:** 80.35 MB usados, el 40 % del presupuesto de 200 MB. `df` y `du` difieren (80.35 MB contra 56.19 MB) porque `df` cuenta además los metadatos del sistema de archivos, que `du` no ve.
- **Qué ocupa más espacio:** `/usr` concentra casi todo el contenido (55.66 MB). Dentro de él, los MP3 de `/usr/share/roomba-disco/audio` pesan 9.56 MB (el 17 % de `/usr`), `/usr/lib/systemd` 9.41 MB y `/usr/lib/firmware` 1.42 MB (de los cuales 1.10 MB son del firmware `cypress` del WiFi).
- **Arranque:** 3.192 s hasta tener `servidor.service` activo, el 21 % del límite de 15 s. Del total de 3.421 s, 1.290 s son del kernel y 2.130 s de userspace.
- **RAM:** 97.66 MB en promedio, el 5.3 % de los 1845.29 MB de la placa.
- **CPU:** 1.4 % del sistema en promedio (máximo 2.1 %). El proceso que más CPU usa es `logica` (4.4 % de un núcleo), seguido de `mpg123` (1.1 %); `servidor` quedó en 0.0 %.

**Limitaciones:** el tiempo de arranque no incluye el firmware ni el bootloader de la rasp (el script mide desde el inicio del kernel), y la medición de CPU y RAM fue de 30 s, así que representa el estado estable y no picos largos.

---

## 8. Resultados más relevantes del Proyecto y Conclusiones

### 8.1 Resultados de eficiencia

Con la imagen entregada (detalle y evidencia en la sección 7):

| Métrica | Resultado | Referencia |
|---|---|---|
| Rootfs usado | 80.35 MB | ≤ 200 MB |
| Kernel hasta `servidor.service` operativo | 3.192 s | ≤ 15 s |
| RAM usada en operación | 97.66 MB en promedio, de 1845.29 MB | — |
| CPU del sistema | 1.4 % en promedio (máximo 2.1 %) | — |

### 8.2 Pruebas automatizadas disponibles

El repositorio incluye pruebas que se ejecutan en una PC con la biblioteca en modo simulado (sección 10.6): odometría, encoders simulados, mapa, SHA-256, modo autónomo (`test_auto`, que simula una habitación y comprueba cuánto cubre) y la prueba de integración WebSocket, servidor, lógica y archivo de estado.

### 8.3 Conclusiones

- La imagen mínima cumple las dos referencias de la especificación con amplio margen: usa el 40 % del presupuesto de rootfs y el 21 % del tiempo de arranque, con un uso de CPU y RAM bajo con audio y servidor activos a la vez.
- La arquitectura en capas (cliente, servidor, lógica y biblioteca), comunicada por WebSocket y por un socket Unix con mensajes JSON, permitió desarrollar y probar cada parte por separado. La biblioteca en modo simulado dejó probar la lógica, la odometría y el modo autónomo sin la placa.
- Fijar los commits de las capas y documentar el procedimiento en `GENERAR_IMAGEN.md` hace reproducible la imagen en otra PC.
- Poner la lógica y el servidor como unidades systemd propias con `Restart=on-failure` dejó el sistema funcionando al encender, sin interfaz gráfica local.

**Limitaciones conocidas:**

- El mapa arranca en 8 x 6 celdas de 100 mm y crece sola hasta un máximo de 100 x 100 (10 m por lado). Si el robot sale de ese límite deja de dibujarse en el mapa, pero el control sigue funcionando. (`Logica/README.md` todavía dice que la grilla es fija: esa documentación quedó desactualizada.)
- Los parámetros de odometría y de velocidad (distancia entre ruedas, velocidad máxima) son temporales: falta calibrarlos con medidas físicas reales.
- La conexión usa `ws://` sin TLS y una autenticación académica propia, pensada para la red de pruebas del curso.
- La imagen conserva `debug-tweaks` (root sin contraseña), que es una opción de desarrollo. El acceso con llave SSH está documentado en [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md), pero no se configuró como único acceso; somos conscientes del riesgo.
- El tiempo de arranque se midió desde el inicio del kernel; no incluye el firmware ni el bootloader de la rasp.

---

## 9. Probar el entorno en una compu nueva

### Inicializar las cosas en la Compu para probar

**Instalar el SDK de 64 bits en la compu**

Abre una terminal, ir donde se descargó el instalador .sh y ejecutar con permisos de administrador:

```bash
chmod +x poky-glibc-x86_64-core-image-minimal-cortexa72-raspberrypi4-64-toolchain-5.0.20.sh
sudo ./poky-glibc-x86_64-core-image-minimal-cortexa72-raspberrypi4-64-toolchain-5.0.20.sh
```

Cuando pregunte la ruta de instalación, darle Enter para aceptar la ruta por default (`/opt/poky/5.0.20/`).

**Flashear el linux nuestro en la rasp**

- Poner la MicroSD en la PC.
- Abrir programita de Raspberry Pi Imager.
- Seleccionar el archivo comprimido `.wic.bz2` que está dentro del zip del drive (sacarlo del .zip antes), seleccionar la SD y presionar Flash. También se puede flashear con `bmaptool` o `dd` (ver el paso 8 de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md)).
- Sacar la tarjeta, poner en la rasp y conectar un cable de red hacia el router o configurar el Wi-Fi.

**Conexión SSH**

Cambiar `<IP_RASPBERRY>` por la IP asignada a la rasp y conectarse:

```bash
ssh root@<IP_RASPBERRY>
```

Con la imagen de desarrollo actual (que incluye `debug-tweaks`) se entra como `root` sin contraseña. Para dejar la llave SSH de la PC (necesaria para `devtool deploy-target`) y para configurar el WiFi, ver el paso 9 de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md).

**Probar el .c de sonido con hilos de ejemplo**

Cargar el entorno de cross-compile (64 bits):

```bash
source /opt/poky/5.0.20/environment-setup-cortexa72-poky-linux
```

Compilar el binario para la rasp:

```bash
$CC main_test.c -I./include -L. -lroombateca -lpthread -o el_test
```

Transferir el ejecutable y la biblioteca por red a la rasp:

```bash
scp el_test libroombateca.so root@<IP_RASPBERRY>:/usr/lib/
```

En la terminal SSH de la rasp, conectar audífonos o parlantes al Jack y correr el binario:

```bash
export LD_LIBRARY_PATH=/usr/lib
el_test
```

Para generar el SDK desde cero y para iterar sobre la rasp corriendo con `devtool` (sin
reflashear), ver [`Yocto/README.md`](Yocto/README.md).

---

## 10. Configuración y Uso del Sistema

Esta sección explica cómo se usa el robot una vez que la imagen está grabada y la rasp arrancó. Para generar y flashear la imagen, ver [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md); para la documentación detallada de cada parte, ver `Servidor/README.md` y `Logica/README.md`.

### 10.1 ¿Qué corre en la raspberryPi 4?

Al encender, `systemd` levanta solos dos servicios, sin interfaz gráfica y con reinicio automático si fallan (`Restart=on-failure`):

| Servicio | ¿Qué hace? |
|---|---|
| `logica.service` | Dueño del estado del robot: lee sensores y encoders, mueve motores, ejecuta el modo autónomo y el audio usando `libroombateca`. |
| `servidor.service` | Servidor WebSocket en el puerto 8080 con autenticación. Arranca después de `logica` y le transporta los mensajes del cliente. |

Para comprobar que están activos:

```bash
# desde: la rasp, por SSH (cualquier directorio)
systemctl status logica.service servidor.service --no-pager
```

### 10.2 Crear el primer usuario

El repositorio no trae ninguna cuenta: hay que crear al menos una en cada rasp antes de poder iniciar sesión.

```bash
# desde: la rasp, por SSH (cualquier directorio)
systemctl stop servidor
/usr/bin/crear_usuario admin 'elegir-clave' /var/lib/roomba-disco/usuarios.conf
chmod 600 /var/lib/roomba-disco/usuarios.conf
systemctl start servidor
```

El archivo guarda `usuario:salt:verificador`, nunca la contraseña. Conviene no reutilizar claves de ejemplo.

### 10.3 Abrir el panel de control

El cliente Angular corre en otro dispositivo (computadora o celular), no en la rasp. Se conecta al servidor por WebSocket en `ws://<IP_RASPBERRY>:8080/ws`. La IP de la rasp se obtiene como indica el paso 9 de [`GENERAR_IMAGEN.md`](GENERAR_IMAGEN.md).

```bash
# desde: ~/Taller4/Roomba-Disco/Cliente  (en la computadora del cliente)
npm install
npm start
```

`npm start` ejecuta `ng serve`; el panel queda en `http://localhost:4200`. En el campo de conexión del panel se escribe la dirección de la rasp, por ejemplo `192.168.1.50:8080`, y se inicia sesión con el usuario creado en 10.2. El cliente nunca envía la contraseña: se autentica con un desafío (ver 10.5).

### 10.4 Qué se puede hacer desde el panel

| Control | Efecto |
|---|---|
| Modo `AUTO` / `MANUAL` | Cambia entre navegación autónoma y control manual. Cambiar de modo siempre frena los motores. |
| Controles direccionales y velocidad (modo manual) | `FWD`, `BACK`, `TURN_L`, `TURN_R` y `STOP`. El movimiento es persistente: se mantiene hasta recibir otro comando o `STOP`. |
| Audio | Lista de pistas, `PLAY`, `PAUSE`, `STOP`, `NEXT`, `PREV` y volumen de 0 a 100. |
| Sensores | Distancia de los dos sensores (frontal y trasero) y si hay obstáculo. |
| Mapa | Grilla de recorrido que se actualiza mientras el robot avanza. |

Reglas del comportamiento:

- En modo `AUTO` los comandos manuales se ignoran. El robot avanza en "S", empieza buscando una esquina, y al terminar se detiene y vuelve a `MANUAL`. Si detecta un obstáculo a menos de 20 cm se detiene y gira 90°; si queda atascado (los encoders no registran avance), retrocede un poco y gira, y después de 3 atascos seguidos termina el recorrido.
- Las dos ruedas se mantienen sincronizadas: en cada ciclo se compara el avance de ambas con los encoders y se frena la que se adelanta (control proporcional-integral). Además, la velocidad sube con una rampa de arranque (de 0 a 100 en cerca de 0.5 s) para no patinar.
- Si la conexión entre servidor y lógica se pierde más de 2 segundos (el servidor envía un latido cada segundo), la lógica detiene ambos motores.
- Los LEDs reflejan el estado: verde al encender el sistema, azul en modo autónomo, amarillo en modo manual y rojo mientras hay un obstáculo.
- Suena un aviso al arrancar, al entrar en modo autónomo, al volver a modo manual y al detectar un obstáculo. Si había música, se pausa durante la alerta y continúa después; la alerta suena una sola vez mientras el obstáculo siga presente.

### 10.5 Protocolo (resumen)

Entre servidor y lógica se usa un socket Unix (`/run/roomba-logica.sock`) con un documento JSON por línea. Entre el cliente y el servidor el mismo formato viaja por WebSocket.

```json
{"type":"get_state"}
{"type":"set_state","desired":{"mode":"AUTO","motion":{"direction":"FWD","speed":321}}}
```

La respuesta siempre es un snapshot completo (`"type":"state"`) con las secciones:

| Campo de `reported` | Contenido |
|---|---|
| `power`, `mode` | Sistema encendido y modo actual. |
| `motion` | `direction` y `speed`. |
| `audio` | `status`, `volume`, `track` y la lista `tracks`. |
| `sensors` | Para cada sensor: `id`, `distanceCm` y `obstacle`. |
| `map` | `width`, `height` y `cells` (`0` desconocida, `1` visitada, `2` obstáculo, `3` libre observada). |
| `pose` | `xMm`, `yMm` y `thetaRad` de la odometría. |

Autenticación: el cliente envía `auth_init`, recibe un salt y un reto aleatorio de 32 bytes, y responde con `SHA256(SHA256(salt || contraseña) || reto)`. Hasta autenticarse no se aceptan comandos ni se envían snapshots.

### 10.6 Probar sin la rasp (modo simulado) y pruebas automatizadas

La biblioteca compilada con `ROOMBATECA_HARDWARE=OFF` simula motores, sensores y encoders, así que toda la cadena se puede probar en una PC:

```bash
# desde: la raíz del repositorio Roomba-Disco
cmake -S Servidor -B Servidor/build-sim -DROOMBATECA_HARDWARE=OFF
cmake --build Servidor/build-sim
ctest --test-dir Servidor/build-sim --output-on-failure
```

`ctest` ejecuta las pruebas de odometría, encoders simulados, mapa, SHA-256 y modo autónomo. La prueba de integración del recorrido completo (WebSocket, servidor, lógica, archivo de estado y de vuelta) se ejecuta con:

```bash
# desde: la raíz del repositorio Roomba-Disco (con logica y servidor ya compilados en modo simulado)
python3 Servidor/test_integracion.py
```

Para levantar todo en la PC y abrir el panel, el procedimiento paso a paso está en `Logica/README.md` ("Cómo correr cliente-servidor-logica-biblioteca como conjunto").

### 10.7 Limitaciones del uso actual

- El cliente no viene en la imagen: hay que correrlo en otro dispositivo (sección 3.3).
- La conexión es `ws://` sin cifrado: usarla solo en la red de pruebas del curso.
