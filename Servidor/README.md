# Servidor web y WebSocket

El servidor es un programa en C que usa CivetWeb (es de un `third_party/`) para exponer un canal WebSocket en `/ws`. No accede a la biblioteca ni al hardware: transporta documentos JSON entre el cliente y la capa Lógica a través de un socket Unix local, y autentica a cada cliente antes de dejarlo hablar con la Lógica.

El cliente Angular es una aplicación independiente que corre en otro dispositivo (laptop, PC o celular) y se conecta por red a la rasp. En la imagen de Yocto el servidor corre como un servicio de systemd; el resto del sistema está descrito en las secciones 3 y 10 del [`README.md`](../README.md) de la raíz.

```text
Cliente Angular (otro dispositivo)
        │  ws://<IP_RASPBERRY>:8080/ws   (JSON, con autenticación)
        ▼
servidor (este programa)
        │  socket Unix /run/roomba-logica.sock   (un JSON por línea)
        ▼
logica  →  libroombateca.so  →  hardware
```

## Argumentos

```text
servidor [puerto] [raiz_web] [socket_logica] [archivo_usuarios]
```

| Argumento | Valor por defecto | Descripción |
|---|---|---|
| `puerto` | `8080` | Puerto de escucha. |
| `raiz_web` | `../Cliente/dist/scrap-e-controller/browser` (o `share/roomba-disco/www` junto al binario, si existe) | Carpeta con los archivos del cliente. Es opcional: con el cliente separado se pasa vacío (`""`). |
| `socket_logica` | `/tmp/roomba-logica.sock` | Socket Unix de la Lógica. |
| `archivo_usuarios` | `usuarios.conf` | Archivo de usuarios (ver "Autenticación"). Si no se puede leer, el servidor avisa y rechaza todos los inicios de sesión. |

Comportamiento del servidor:

- Acepta hasta 16 clientes WebSocket a la vez (8 hilos de CivetWeb).
- Envía un `{"type":"heartbeat"}` a la Lógica cada segundo. Si la Lógica no recibe latidos durante más de 2 segundos, detiene los motores.
- Si pierde la conexión con la Lógica, intenta reconectarse cada segundo.

## Compilar con CMake

El `CMakeLists.txt` de esta carpeta compila el servidor, `crear_usuario`, el programa de la Lógica (`logica_simulador`) y las pruebas. La primera vez descarga cJSON v1.7.19 desde GitHub, así que necesita red.

`logica_simulador` es el mismo programa de `Logica/`; en la imagen de Yocto se llama `logica`. Se comporta como simulador solo cuando la biblioteca se compila con `ROOMBATECA_HARDWARE=OFF`.

**En una PC sin hardware (modo simulado).** La opción `ROOMBATECA_HARDWARE` está activada por defecto y pide los headers de `libgpiod`, así que en una PC normal hay que desactivarla:

```bash
# desde: la raíz del repositorio Roomba-Disco
cmake -S Servidor -B Servidor/build -DROOMBATECA_HARDWARE=OFF
cmake --build Servidor/build
```

**Con hardware real.** Se necesita `libgpiod` (por ejemplo `libgpiod-dev` en el host o el SDK de Yocto, ver la sección 4 del README raíz):

```bash
# desde: la raíz del repositorio Roomba-Disco (con el entorno del SDK cargado, si se compila de forma cruzada)
cmake -S Servidor -B Servidor/build-hw -DROOMBATECA_HARDWARE=ON
cmake --build Servidor/build-hw
```

Otras opciones: `-DBUILD_CLIENT=ON` compila el cliente Angular con `npm` y lo instala junto al servidor (despliegue monolítico, no se usa en la imagen); `-DROOMBATECA_AUDIO_DIR=<ruta>` cambia la carpeta de los `.mp3`.

Los scripts de prueba esperan los binarios en una carpeta fija: `Servidor/build` (`test_integracion.py`, `test_auth.mjs`, `test_simulador.mjs` y `run-demo.sh`) o `Servidor/build-sim` (`prueba_mapa.py` y los ejemplos de `Logica/README.md`). Para usar todos, se compila la versión simulada en las dos carpetas.

## Ejecutar en desarrollo

Con el modo simulado compilado en `Servidor/build`, se levantan la Lógica y el servidor en dos terminales:

```bash
# desde: la raíz del repositorio Roomba-Disco (terminal 1)
./Servidor/build/logica_simulador Logica/estado.json /tmp/roomba-logica.sock
```

```bash
# desde: la raíz del repositorio Roomba-Disco (terminal 2)
./Servidor/build/servidor 8080 "" /tmp/roomba-logica.sock Servidor/usuarios.conf
```

El canal WebSocket queda en `ws://<IP_DE_LA_RASPBERRY>:8080/ws` (o `ws://127.0.0.1:8080/ws` en la misma PC). En el dispositivo del cliente se corre la aplicación Angular por separado (`npm start`, que ejecuta `ng serve`, o el resultado de `npm run build`) y se escribe la dirección de la rasp en el campo de conexión del panel, por ejemplo `192.168.1.50:8080`.

**Atajo con un solo comando.** Desde la raíz del repositorio, `./run-demo.sh` compila el modo simulado en `Servidor/build`, levanta la Lógica simulada, el servidor y `ng serve`, y deja el panel en `http://127.0.0.1:4200/login` (en el formulario se usa `127.0.0.1:8080`). Necesita `cmake`, `npm` y `node`, y que `Servidor/usuarios.conf` ya exista: la primera vez hay que compilar y crear el usuario como se explica en la sección siguiente.

## Autenticación académica

El WebSocket requiere autenticación antes de aceptar comandos o enviar snapshots. El cliente nunca envía la contraseña. El intercambio es:

| Paso | Mensaje | Quién lo envía |
|---|---|---|
| 1 | `auth_init` con el usuario | Cliente |
| 2 | `auth_challenge` con un salt y un reto aleatorio de 32 bytes | Servidor |
| 3 | `auth_response` con `SHA256(SHA256(salt || contraseña) || reto)` | Cliente |
| 4 | `auth_result` con `"ok": true` o `false` | Servidor |

El servidor consume el reto una sola vez y autoriza esa conexión WebSocket si la respuesta coincide. Hasta entonces la sesión no puede hablar con la Lógica. El archivo de usuarios guarda una línea `usuario:salt_hex:verificador_hex` por cuenta; el salt y el verificador los genera `crear_usuario`, y el archivo debe mantenerse fuera de la raíz web.

La autenticación usa un SHA-256 implementado en el proyecto, como requisito académico. No es una solución criptográfica de producción ni activa TLS: el servidor expone `ws://`, así que conviene limitarlo a la red de pruebas del curso.

No se versiona ninguna cuenta por defecto: hay que crear al menos una en cada entorno antes de poder iniciar sesión. Para desarrollo local:

```bash
# desde: la raíz del repositorio Roomba-Disco
cmake --build Servidor/build --target crear_usuario
./Servidor/build/crear_usuario admin 'elegir-clave' Servidor/usuarios.conf
chmod 600 Servidor/usuarios.conf
./Servidor/build/servidor 8080 "" /tmp/roomba-logica.sock Servidor/usuarios.conf
```

En la imagen de Yocto se detiene el servicio, se crea la cuenta en el archivo persistente y se vuelve a iniciar:

```bash
# desde: la rasp, por SSH (cualquier directorio)
systemctl stop servidor
/usr/bin/crear_usuario admin 'elegir-clave' /var/lib/roomba-disco/usuarios.conf
chmod 600 /var/lib/roomba-disco/usuarios.conf
systemctl start servidor
```

Conviene no reutilizar claves de ejemplo como `roomba123`, que se usó en una prueba temporal.

## En la imagen de Yocto

La receta `servidor` (en `meta-robot/recipes-apps/servidor/`) compila el servidor y `crear_usuario` con el compilador cruzado de Yocto, sin pasar por este `CMakeLists.txt`, e instala el servicio de systemd. No hace falta instalar nada a mano.

| Elemento | Valor |
|---|---|
| Servicio | `servidor.service`, que arranca después de `network.target` y de `logica.service`, con `Restart=on-failure` |
| Binarios | `/usr/bin/servidor` y `/usr/bin/crear_usuario` |
| Comando | `/usr/bin/servidor 8080 /usr/share/roomba-disco/www /run/roomba-logica.sock /var/lib/roomba-disco/usuarios.conf` |
| Socket de la Lógica | `/run/roomba-logica.sock` |
| Usuarios | `/var/lib/roomba-disco/usuarios.conf` (carpeta persistente que crea systemd) |
| Puerto | `8080` |

Para ver el estado y los registros del servicio:

```bash
# desde: la rasp, por SSH (cualquier directorio)
systemctl status servidor.service --no-pager
journalctl -u servidor.service --no-pager | tail -n 20
```

La carpeta web `/usr/share/roomba-disco/www` que recibe el servicio no existe en la imagen, porque el cliente no se instala en la rasp (receta `cliente`, sección 3.3 del README raíz). El servidor solo se usa por WebSocket, así que no afecta, pero con el cliente separado el argumento podría ir vacío (`""`), como en desarrollo.

### Instalación manual (alternativa sin Yocto)

Si se compila el servidor directamente en la rasp o con el SDK, `cmake --install` copia los programas:

```bash
# desde: la raíz del repositorio Roomba-Disco
cmake --install Servidor/build-hw --prefix /opt/roomba-disco
/opt/roomba-disco/bin/servidor 8080 "" /tmp/roomba-logica.sock /opt/roomba-disco/usuarios.conf
```

La instalación copia `servidor`, `crear_usuario` y `logica_simulador` a `bin/` y `Logica/estado.json` a `share/roomba-disco/`. Los archivos del cliente (`share/roomba-disco/www`) solo se instalan si se compiló con `-DBUILD_CLIENT=ON`; en ese caso el servidor los encuentra solo y el panel se abre en `http://IP_DE_LA_RASPBERRY:8080`. Este camino no instala servicios de systemd.

## Estado del canal

Cuando un cliente abre el WebSocket y se autentica, solicita el snapshot completo a la Lógica. Los cambios de controles se envían como `set_state`; la Lógica los valida, actualiza `Logica/estado.json` y devuelve un nuevo snapshot a todos los clientes conectados. El servidor no toma decisiones sobre el robot: solo transporta los mensajes.

| Mensaje | Dirección | Uso |
|---|---|---|
| `auth_init`, `auth_response` | cliente → servidor | Inicio de sesión (ver "Autenticación") |
| `auth_challenge`, `auth_result` | servidor → cliente | Respuestas del inicio de sesión |
| `get_state` | cliente → Lógica | Pide el snapshot completo |
| `set_state` | cliente → Lógica | Cambio parcial de lo deseado (modo, movimiento, audio) |
| `state` | Lógica → clientes | Snapshot completo |
| `heartbeat` | servidor → Lógica | Cada segundo, para el watchdog |

```js
const socket = new WebSocket('ws://localhost:8080/ws');
// Hay que autenticarse primero (auth_init / auth_response); luego:
socket.onopen = () => socket.send(JSON.stringify({ type: 'auth_init', user: 'admin' }));
socket.onmessage = event => console.log(JSON.parse(event.data));
```

El contrato completo de los mensajes y el contenido del estado están en `Logica/README.md` y en la sección 10.5 del README raíz.

## Pruebas y herramientas de apoyo

Las pruebas de C se registran en CTest y se ejecutan con:

```bash
# desde: la raíz del repositorio Roomba-Disco
ctest --test-dir Servidor/build --output-on-failure
```

Incluyen odometría, encoders simulados, mapa, SHA-256 y el modo autónomo (`test_auto`).

| Archivo | Qué hace | Binarios que usa |
|---|---|---|
| `test_integracion.py` | Recorrido completo WebSocket → servidor → Lógica → archivo JSON → WebSocket | `Servidor/build` |
| `pruebas/test_auth.mjs` | Valida el inicio de sesión (reto y respuesta) contra el servidor C real; el SHA-256 de Node se cruza con el de C | `Servidor/build` |
| `pruebas/test_simulador.mjs` y `pruebas/simulador_estado.mjs` | Cliente → servidor → simulador de la Lógica hecho en Node, y de vuelta a dos clientes | `Servidor/build` |
| `pruebas/prueba_mapa.py` | Movimiento → pose → mapa → `STOP` con la Lógica simulada | `Servidor/build-sim` |
| `pruebas/consola_logica.py` | Consola para hablar con la Lógica real a través del servidor (`--url ws://127.0.0.1:8080/ws`, `--full` muestra snapshots repetidos) | servidor en marcha |
| `pruebas/inyectar_sensores.py` | Cambia las distancias de los sensores simulados (frontal y trasero, entre 2 y 400 cm) escribiendo en `/tmp/roomba-sensores.txt` | biblioteca compilada con `ROOMBATECA_SIM_INJECT=ON` y la variable `ROO_SIM_SENSORS_FILE` |

```bash
# desde: la raíz del repositorio Roomba-Disco (con el servidor compilado en Servidor/build)
python3 Servidor/test_integracion.py
node Servidor/pruebas/test_auth.mjs
node Servidor/pruebas/test_simulador.mjs
```
