# Cliente web de Roomba-Disco (SCRAP-E Controller)

Panel de control web del robot, hecho con [Angular](https://angular.dev) 19 y Angular Material. Muestra el estado del robot en tiempo real (modo, sensores, mapa y audio) y permite controlarlo: cambiar entre modo autónomo y manual, mover el robot con un joystick y manejar la música.

El cliente es una aplicación **independiente que corre en otro dispositivo** (laptop, PC o celular), no en la rasp. Se conecta por red al servidor de la rasp mediante un WebSocket (`ws://<IP_RASPBERRY>:8080/ws`) y se autentica con usuario y contraseña. Por eso la receta `cliente` no entra en la imagen de Yocto (sección 3.3 del [`README.md`](../README.md) de la raíz). Cómo encaja con el resto del sistema se explica en la sección 10 de ese README y en [`Servidor/README.md`](../Servidor/README.md).

## Requisitos

- **Node.js y npm.** Angular 19 es compatible con las versiones `^18.19.1`, `^20.11.1` y `^22.0.0` de Node.js.
- Un navegador moderno.
- La rasp encendida, con el servidor corriendo y **al menos un usuario creado** (sin cuenta no se puede iniciar sesión; ver "Crear el primer usuario" en `Servidor/README.md`).

## Instalar y ejecutar

```bash
# desde: ~/Taller4/Roomba-Disco/Cliente  (en la computadora del cliente)
npm install
npm start
```

`npm start` ejecuta `ng serve` y deja el panel en `http://localhost:4200`. Para abrirlo desde otro dispositivo de la misma red (por ejemplo un celular), se expone en todas las interfaces:

```bash
# desde: ~/Taller4/Roomba-Disco/Cliente
npm start -- --host 0.0.0.0
```

y se entra a `http://<IP_DE_LA_COMPUTADORA>:4200`. La página debe abrirse con `http://` y no con `https://`: el servidor de la rasp solo habla `ws://` (sin TLS) y el navegador bloquearía esa conexión desde una página segura.

**Atajo para probar todo en la PC.** `./run-demo.sh`, desde la raíz del repositorio, levanta la Lógica simulada, el servidor y este cliente con un solo comando (ver `Servidor/README.md`).

## Iniciar sesión

La pantalla de inicio de sesión (`/login`) pide tres datos:

| Campo | Qué escribir |
|---|---|
| Servidor (IP:puerto) | La dirección de la rasp, por ejemplo `192.168.1.50:8080`. Si se omite el puerto se usa `8080`; también se acepta una dirección completa `ws://...`. Si se deja vacío, el cliente se conecta al mismo origen de donde se cargó la página (útil cuando el servidor sirve el cliente). |
| Usuario | La cuenta creada con `crear_usuario` en la rasp. |
| Contraseña | La contraseña de esa cuenta. |

La dirección del servidor se guarda en el navegador (`localStorage`, clave `roomba.serverAddress`), así que no hay que escribirla de nuevo.

La contraseña **nunca viaja por la red**. El cliente envía `auth_init` con el usuario, recibe del servidor un salt y un reto aleatorio, y responde con `SHA256(SHA256(salt || contraseña) || reto)`. El SHA-256 está implementado en el propio proyecto (`src/app/services/sha256.ts`), por lo que no depende de la API criptográfica del navegador y funciona también con páginas servidas por `http://` desde una IP. Si el servidor rechaza el inicio de sesión o no está disponible, el formulario muestra "Usuario o contraseña incorrectos, o servidor no disponible".

## Pantallas

| Ruta | Pantalla | Requiere sesión |
|---|---|---|
| `/login` | Inicio de sesión | No |
| `/` | Panel de control (la pantalla de uso real) | Sí |
| `/controller` y `/modes` | Prototipos de desarrollo, sin enlace desde la interfaz (ver abajo) | Sí |
| cualquier otra | Redirige a `/login` | — |

Sin una sesión autenticada, las rutas protegidas redirigen a `/login`.

`/controller` y `/modes` son pantallas de prototipo que no controlan el robot: `/controller` abre su propio WebSocket a `ws://localhost:8080/ws` sin autenticarse (el servidor actual no le dejaría hablar con la Lógica), y `/modes` muestra tarjetas "Loop", "Find" y "Follow" que no envían nada.

## Panel de control (`/`)

| Elemento | Qué hace | Qué envía |
|---|---|---|
| Barra de conexión | Muestra el estado (`desconectado`, `conectando`, `conectado`), permite cambiar la dirección de la rasp (**Conectar** reconecta) y **Cerrar sesión** | — |
| Botón de modo | Alterna entre `MANUAL` y `AUTO`; cambia de color según el modo que reporta el robot | `{"mode":"AUTO"}` o `{"mode":"MANUAL"}` |
| Indicador de alerta | "Alerta" o "Sin alerta", según el LED de alerta del robot | — |
| Sensores | Distancia en cm de los dos sensores (1 y 2); se resalta cuando hay obstáculo | — |
| Audio | Lista de pistas (con la actual resaltada), anterior, reproducir o pausar, detener, siguiente, y volumen de 0 a 100 | `audio.action` (`PLAY`, `PAUSE`, `STOP`, `NEXT`, `PREV`) y `audio.volume` |
| Mapa | Grilla del recorrido que se redibuja con cada cambio y se ajusta cuando el mapa crece | — |
| Joystick | Mueve el robot: arriba `FWD`, abajo `BACK`, izquierda `TURN_L`, derecha `TURN_R` y suelto `STOP` | `motion.direction` |
| Velocidad | Deslizador de 0 a 1000 | `motion.speed` |

Todos los comandos viajan como `{"type":"set_state","desired":{...}}`; el panel nunca modifica su propio estado: se actualiza cuando el servidor devuelve el snapshot que reporta la Lógica.

Reglas del panel:

- El joystick y la velocidad solo se habilitan en modo `MANUAL`. En modo `AUTO` la Lógica decide el movimiento e ignora los comandos manuales.
- Los controles se deshabilitan si el robot no reporta estar encendido (`power`) o si no hay conexión.
- El mapa pinta cada celda con un color:

| Estado de la celda | Color |
|---|---|
| Desconocida | Gris (`#e0e0e0`) |
| Visitada | Azul (`#90caf9`) |
| Obstáculo | Rojo (`#e53935`) |
| Libre observada | Verde (`#a5d6a7`) |

## Conexión y reconexión

- Si se cae la conexión con el servidor, el cliente intenta reconectarse cada 2 segundos. Al perder la conexión la sesión vuelve a "anónima" y el panel redirige a `/login`, así que hay que iniciar sesión otra vez.
- Después de autenticarse, el cliente pide el estado completo (`get_state`) y desde entonces reemplaza lo que muestra con cada snapshot (`"type":"state"`, `protocolVersion` 1) que llega.
- El servidor envía los latidos a la Lógica; el cliente no necesita enviarlos.

El formato de los mensajes y el contenido del estado están en la sección 10.5 del README raíz y en [`Logica/README.md`](../Logica/README.md).

## Estructura del código

```text
src/app/
├── auth/                  inicio de sesión y guardia de rutas
├── dashboard/             panel de control
│   ├── audio-panel/       lista de pistas, controles y volumen
│   └── map-view/          grilla del mapa
├── controller/            prototipo de control y componente del joystick (nipplejs)
├── modes/                 prototipo de tarjetas de modos
├── menu/navbar/           barra superior
└── services/
    ├── robot-socket.service.ts   WebSocket, autenticación, reconexión y estado
    └── sha256.ts                 SHA-256 y respuesta al reto de autenticación
```

Todo el contacto con el servidor está en `robot-socket.service.ts`: mantiene el estado de la conexión, el de la sesión y el último snapshot como señales de Angular, que los componentes leen.

## Compilar para producción

```bash
# desde: ~/Taller4/Roomba-Disco/Cliente
npm run build
```

Genera los archivos estáticos en `dist/scrap-e-controller/browser/`. Una forma de usarlos es dejar que el servidor los sirva, pasándole esa carpeta como raíz web; en ese caso el panel se abre en `http://<IP>:8080` y el campo de servidor se deja vacío (se conecta al mismo origen):

## Running unit tests
To execute unit tests with the [Karma](https://karma-runner.github.io) test runner, use the following command:

```bash
# desde: la raíz del repositorio Roomba-Disco
./Servidor/build/servidor 8080 "$PWD/Cliente/dist/scrap-e-controller/browser" /tmp/roomba-logica.sock Servidor/usuarios.conf
```

La opción `-DBUILD_CLIENT=ON` de `Servidor/CMakeLists.txt` ejecuta este mismo `npm run build` y lo instala junto al servidor. La imagen de Yocto no lo incluye.

## Pruebas

```bash
# desde: ~/Taller4/Roomba-Disco/Cliente
npm test
```

Ejecuta las pruebas unitarias con Karma y Jasmine (`ng test`); necesita Google Chrome o Chromium instalado.

## Limitaciones

- La pista se cambia con anterior y siguiente, no eligiendo una de la lista.
- El panel no usa la pose de la odometría (`reported.pose`), solo muestra el mapa.
- La conexión es `ws://` sin cifrado, pensada para la red de pruebas del curso.
