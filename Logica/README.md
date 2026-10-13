# Lógica del robot Roomba-disco

Este proceso en C es la capa Lógica del robot: el **único propietario de `estado.json`**. Recibe los cambios deseados, los valida, mueve el hardware a través de `libroombateca`, ejecuta el modo autónomo, mantiene la odometría y el mapa, actualiza el estado reportado y guarda el snapshot completo.

En la imagen de Yocto corre en la rasp como `logica.service` (binario `/usr/bin/logica`). El mismo código se compila en la PC como `logica_simulador`, con la biblioteca simulada, para probar sin la placa (ver `Servidor/README.md`). Ver también las secciones 3 y 10 del [`README.md`](../README.md) de la raíz.

## Contrato IPC

La Lógica usa un socket Unix con un documento JSON por línea. La ruta por defecto es `/tmp/roomba-logica.sock` (desarrollo); en la imagen es `/run/roomba-logica.sock`, que se la pasa la unidad de systemd.

Solicitud de lectura:

```json
{"type":"get_state"}
```

Cambio parcial pedido por el cliente:

```json
{"type":"set_state","desired":{"mode":"AUTO","motion":{"direction":"FWD","speed":321}}}
```

Latido del servidor (enviado cada segundo -> "Watchdog"):

```json
{"type":"heartbeat"}
```

La respuesta a `get_state` y a un `set_state` que cambió algo es siempre un snapshot completo con `"type":"state"`. El servidor transporta estos documentos sin tomar decisiones sobre el robot.

Validación de `set_state` (un valor inválido se ignora y no cambia el estado):

| Campo de `desired` | Valores válidos |
|---|---|
| `mode` | `AUTO` o `MANUAL` |
| `motion.direction` | `FWD`, `BACK`, `TURN_L`, `TURN_R` o `STOP` |
| `motion.speed` | número de 0 a 1000 (el 1000 es la velocidad máxima) |
| `audio.action` | `PLAY`, `PAUSE`, `STOP`, `NEXT` o `PREV` |
| `audio.volume` | número de 0 a 100 |

Cada `set_state` que cambia algo incrementa `revision` y guarda `estado.json`. Los snapshots solo se envían a los clientes cuando el estado cambia: el ciclo de control corre cada 100 ms, pero no se transmiten copias idénticas.

## Ejecución

**En la imagen de Yocto** no hay que hacer nada: `logica.service` arranca con el sistema y se reinicia si falla (`Restart=on-failure`). Antes de iniciar, copia `/usr/share/roomba-disco/estado.json` a `/var/lib/roomba-disco/estado.json` si todavía no existe, y ejecuta:

```text
/usr/bin/logica /var/lib/roomba-disco/estado.json /run/roomba-logica.sock
```

```bash
# desde: la rasp, por SSH (cualquier directorio)
systemctl status logica.service --no-pager
```

**En la PC, en modo simulado.** Con la versión simulada compilada en `Servidor/build` (los comandos están en `Servidor/README.md`), se levantan la Lógica y el servidor en dos terminales:

```bash
# desde: la raíz del repositorio Roomba-Disco (terminal 1)
./Servidor/build/logica_simulador Logica/estado.json /tmp/roomba-logica.sock
```

```bash
# desde: la raíz del repositorio Roomba-Disco (terminal 2)
./Servidor/build/servidor 8080 "" /tmp/roomba-logica.sock Servidor/usuarios.conf
```

Si el sistema no encuentra `libroombateca.so`, se antepone `LD_LIBRARY_PATH=Servidor/build/roombateca` al primer comando. El cliente Angular se corre aparte y el archivo de usuarios se crea como indica `Servidor/README.md` (sin una cuenta no se puede iniciar sesión).

**Atajo.** `./run-demo.sh` (en la raíz del repo) compila el modo simulado y levanta la Lógica, el servidor y el cliente con un solo comando.

## Cómo está armada

Hay dos hilos que comparten el documento JSON del estado protegido con un `state_mutex`: uno atiende el socket y otro ejecuta el ciclo de control. La biblioteca se usa como una caja negra: la Lógica solo llama a las funciones declaradas en los `.h` de `Biblioteca/include/`.

```text
Cliente (WebSocket) -> servidor
	|
	| socket Unix: JSON por línea
	v
logica.c  (hilo del socket + hilo de control cada 100 ms)
	|
	| roombateca_control.c
	v
libroombateca (real o simulada)
	+-- motores, encoders, sensores, LEDs, succión, audio
	|
	+-- odometria.c -> reported.pose
	+-- mapa.c      -> reported.map
	+-- auto.c      -> órdenes del modo autónomo
```

| Archivo | Qué hace |
|---|---|
| `logica.c` | Socket, validación del estado, ciclo de control, watchdog y persistencia |
| `roombateca_control.c` y `.h` | Capa entre la Lógica y la biblioteca: velocidades, sincronización de ruedas, LEDs, audio y succión |
| `odometria.c` y `.h` | Posición y orientación a partir de los encoders |
| `mapa.c` y `.h` | Grilla del recorrido |
| `auto.c` y `.h` | Máquina de estados del modo autónomo |
| `estado.json` | Estado inicial y copia persistida |
| `test_*.c` | Pruebas de odometría, encoders simulados, mapa y modo autónomo |

## Hilo de control

`logica.c` tiene un hilo separado que se despierta cada 100 ms. No espera a que el cliente envíe un mensaje. En cada ciclo:

1. Lee los dos sensores, frontal y trasero, y los dos encoders.
2. Detecta obstáculos: hay obstáculo si algún sensor válido mide menos de 20 cm. Al aparecer, enciende el LED de alerta y dispara una vez la alerta sonora (en modo `AUTO` además detiene los motores). La alerta no se repite mientras el obstáculo siga presente.
3. Sincroniza las dos ruedas (ver abajo).
4. Actualiza la odometría y, con la pose, el mapa: marca la celda visitada y observa con cada sensor (a la distancia medida se le suma el radio del robot, porque los sensores están en el borde y la pose es el centro).
5. Si el modo es `AUTO`, ejecuta un paso del modo autónomo y aplica la orden que devuelve.
6. Actualiza `reported.sensors`, `reported.pose`, el audio y el mapa.

Los dos encoders se leen con una sola operación conjunta, para que la rueda izquierda y la derecha no se lean en instantes distintos y no aumente el error de la odometría.

## Velocidad, rampa y sincronización de ruedas

La velocidad que llega del cliente va de 0 a 1000 y la Lógica la escala al rango de los motores `[-100, 100]` de la biblioteca (que a su vez la traduce a un ciclo de trabajo del PWM). Los movimientos `FWD`, `BACK`, `TURN_L` y `TURN_R` pasan por dos ayudas en `roombateca_control.c`:

- **Rampa de arranque:** la velocidad base sube 20 unidades por ciclo, de 0 a 100 en cerca de 0.5 s, para no patinar ni pedirle a la batería un pico de corriente.
- **Sincronización de ruedas:** en cada ciclo se compara cuántos pulsos lleva cada rueda desde que empezó el movimiento y se **frena la que se adelanta** (control proporcional-integral: `KP` = 4.0 unidades de velocidad por pulso de diferencia, `KI` = 0.5 por pulso y por ciclo, con una corrección máxima de 40). La parte integral se conserva entre movimientos del mismo tipo y funciona como una compensación aprendida. Se frena la adelantada y no se acelera la otra porque a velocidad 100 no hay margen.

Los valores de `KP` y `KI` están marcados en el código como "por definir": falta afinarlos con el robot en el piso.

## Simulación de motores y encoders

En modo simulado (`ROOMBATECA_HARDWARE=OFF`), `motores.c` no controla GPIO. Solo guarda las velocidades actuales de las ruedas y las muestra en la terminal:

```text
[motores] izquierdo = 50
[motores] derecho = 50
```

La velocidad simulada máxima es:

```text
100 por ciento = 500 mm/s
```

Este valor es temporal y sirve para poder observar movimiento. No representa la velocidad real del robot.

`encoders.c` (versión simulada) consulta las velocidades actuales de los motores y calcula cuánto recorrió cada rueda desde la última lectura:

```text
distancia nueva = velocidad simulada * tiempo transcurrido
```

La distancia se acumula. Si las dos ruedas tienen velocidad positiva, ambas distancias aumentan; si una es positiva y la otra negativa, una aumenta y la otra disminuye, lo que simula un giro sobre el eje del robot. Los encoders también entregan pulsos, velocidad y tiempo del último pulso, pero la odometría usa principalmente `distancia_mm`.

## Odometría

Los archivos son `Logica/odometria.h` y `Logica/odometria.c`. Mantiene tres valores:

```text
x_mm       posición horizontal
y_mm       posición vertical
theta_rad  orientación
```

La configuracion actual usa temporalmente:

```text
distancia entre ruedas = 220 mm
```

El módulo recibe las distancias acumuladas de ambos encoders. Primero calcula el cambio desde la lectura anterior:

```text
delta izquierda = distancia actual izquierda - distancia anterior izquierda
delta derecha   = distancia actual derecha - distancia anterior derecha
```

Después calcula el avance promedio y el cambio de orientación:

```text
avance = (delta izquierda + delta derecha) / 2

cambio de ángulo = (delta derecha - delta izquierda)
		   / distancia entre ruedas
```

La posicion se actualiza usando el angulo medio del pequeño movimiento:

```text
x = x + avance * cos(angulo medio)
y = y + avance * sin(angulo medio)
```

La primera lectura no genera movimiento: solo se guarda como referencia, para no interpretar como movimiento nuevo la distancia que ya tenía el encoder al iniciar.

## Watchdog

El movimiento es persistente: `FWD`, `BACK`, `TURN_L` y `TURN_R` mantienen la última velocidad aplicada hasta recibir otro comando o `STOP`. Por eso el watchdog no usa un tiempo sin comandos, que rompería ese comportamiento. En su lugar, el servidor envía un latido (`heartbeat`) cada segundo. Si la conexión entre servidor y Lógica desaparece durante más de 2 segundos, la Lógica detiene los motores y la succión, y el modo `AUTO` no se ejecuta hasta que vuelvan los latidos. Al cerrar el socket también se ejecuta una parada inmediata.

## Estado publicado

El estado reportado (`reported`) contiene:

| Campo | Contenido |
|---|---|
| `power` | Sistema encendido (LED verde) |
| `mode` | `AUTO` o `MANUAL` |
| `modeIndicator` | `MANUAL`, `AUTO` u `OFF`, según los LEDs de modo |
| `alertIndicator` | `true` mientras el LED de alerta está encendido |
| `motion` | `direction` y `speed` |
| `audio` | `status` (`playing`, `paused` o `stopped`), `volume`, `track` y la lista `tracks` |
| `sensors` | Para cada sensor: `id`, `distanceCm` y `obstacle` |
| `map` | `width`, `height` y `cells` |
| `pose` | `xMm`, `yMm` y `thetaRad` de la odometría |

```json
"pose": {
  "xMm": 75.0,
  "yMm": 0.0,
  "thetaRad": 0.0
}
```

El nombre `reported.pose` es temporal para probar la conexion. Todavia no se
visualiza en el cliente porque el componente del mapa aun no consume esta
pose.

**Mapa.** La grilla arranca de 8 x 6 celdas de 100 mm, con el origen del robot en la celda (4, 3), y **crece sola** cuando la pose sale de sus límites, hasta un máximo de 100 x 100 celdas (10 m por lado). Si la pose sale de ese máximo, el robot deja de dibujarse en el mapa pero el control sigue funcionando. Los estados de celda son:

```text
0 = desconocida
1 = visitada por el robot
2 = obstaculo
3 = libre observada por un sensor
4 = aspirada (solo interna: al cliente se le reporta como 1)
```

Al observar con un sensor, las celdas se marcan libres hasta 100 mm antes del eco; la celda donde rebota queda sin marcar (o como obstáculo si mide menos de 20 cm). El estado 4 lo marca el modo `AUTO` con el ancho de la boquilla cuando la aspiradora está encendida, y no se rebaja a 1 si el robot vuelve a pasar. El mapa y la odometría se conservan al cambiar de modo (lo recorrido en `MANUAL` se mantiene) hasta reiniciar la Lógica.

## Control autónomo

Con `mode = AUTO`, el ciclo de control llama a `auto_paso()` (`Logica/auto.c`), una máquina de estados que barre la habitación en forma de "S". Recibe la pose de la odometría y las lecturas de los dos sensores (`auto_sensores_t`: frontal y trasero). Empieza buscando una esquina para luego iniciar el recorrido:

```text
esquina: pared -> giro der 90 -> pared (este tramo va a lo largo de la pared 1 y ya aspira)
barrido: fila -> obstáculo -> giro 90 -> mover 12 cm -> giro 90 -> fila ...
         (el lado del giro alterna derecha/izquierda en cada vuelta, de manera que mapea en forma de "S")
rumbo:   si la fila se desvía más de 5°, para, corrige el rumbo y sigue la misma fila
atasco:  si el robot no avanza -> retrocede 5 cm (o hasta que el sensor trasero vea algo) -> sigue con la maniobra
fin de "S": el movimiento lateral ya no cabe o la fila sale casi vacía
         -> con el mapa: planear -> ir a la zona sin aspirar más cercana -> otra "S" ahí
fin:     no queda zona sin aspirar alcanzable en el mapa, se atasca más de 3 veces seguidas
         o se cumple el tiempo máximo del recorrido (30 min)
```

Fases: avanzar (recto hasta un obstáculo), parar (pausa corta entre maniobras), girar, mover (hacia la fila siguiente), retroceder (tras un atasco), planear y ir (navegación con el mapa) y fin. Al terminar, la Lógica imprime el motivo (`AUTO terminado: borde lateral`, por ejemplo), que queda en `motivo_fin`.

**Giros a rumbo absoluto.** El rumbo del tramo 2 de la esquina queda como referencia (`rumbo_fila`). Las filas van a ese rumbo o al opuesto (+180°) y el movimiento lateral siempre a `rumbo_fila - 90°`. Cada giro apunta a uno de esos rumbos y no a "90° desde donde quedó", así el error de un giro no se arrastra al siguiente. En los últimos ~20° gira a `AUTO_VEL_GIRO_FINO`; si se pasa del rumbo, gira de vuelta en lugar de seguir.

**Navegación con el mapa.** `logica.c` le pasa a `auto.c` el mismo mapa que se reporta al cliente (`auto_usar_mapa()`). Cuando una "S" se corta, en lugar de terminar:

1. **Planear:** busca en el mapa, con una búsqueda en anchura desde la celda del robot, la celda sin aspirar más cercana. Solo pasa por celdas *transitables*: piso conocido (visitado, libre observado o aspirado) en todo lo que ocupa el robot (la celda y sus 8 vecinas). Así no se planea pegado a una pared ni por lo desconocido.
2. **Ir:** del camino encontrado toma el punto más lejano al que se llega en línea recta (máximo 1.5 m), gira hacia él y avanza. Si el ultrasónico ve algo o se atasca, vuelve a planear con el mapa actualizado (el atasco queda marcado como obstáculo).
3. **Otra "S":** al llegar empieza una "S" nueva con las filas paralelas a las de la primera, hacia el lado donde el mapa muestra más por aspirar.

Una zona que no se logra alcanzar en 3 intentos, o donde ya empezó una "S", se descarta (no se vuelve a buscar en 25 cm a la redonda). El recorrido termina con "mapa completo" cuando no queda ninguna zona sin aspirar alcanzable. Sin mapa (`auto_usar_mapa(NULL)`), la primera "S" que se corta termina el recorrido.

**Obstáculo y atasco.** Con un obstáculo a menos de 20 cm (sensor frontal) termina la fila: el robot se detiene (con velocidad 1000 queda a unos 5 cm) y gira. El movimiento lateral usa el mismo umbral de 20 cm, porque frena igual. Además detecta un **atasco** con la odometría: si con los motores andando los encoders no registran avance en una ventana de 0.4 s (al menos 10 mm, o 0.05 rad en un giro), lo toma como un obstáculo que el ultrasónico no vio. Entonces retrocede unos 5 cm, sin retroceder si el sensor trasero ve algo a menos de 20 cm, y retoma la maniobra. Una pausa de gracia de 0.5 s al empezar cada fase evita confundir la aceleración con un atasco. Si el robot se atasca más de 3 veces seguidas sin completar una maniobra (al cuarto atasco), termina el recorrido.

**Succión.** Se enciende (a la potencia máxima) desde el tramo 2 de la búsqueda de esquina (que recorre la pared 1) y durante el barrido, y se apaga al frenar, al cambiar de modo y al terminar.

**Reglas del modo `AUTO`:**

- Los comandos manuales de movimiento se ignoran.
- Cambiar de modo siempre frena los motores y apaga la succión; el recorrido autónomo empieza de cero cada vez que se entra a `AUTO`.
- Al terminar, el robot se detiene y vuelve a `MANUAL` (se actualizan los LEDs y `revision`).

Los parámetros están en `auto.h`. Los marcados con **CALIBRAR** tienen al lado un comentario que indica cuándo subirlos o bajarlos al probar en el piso:

| Parámetro | Valor |
|---|---|
| Distancia de obstáculo / fin de fila (CALIBRAR) | 20 cm |
| Tope del movimiento lateral (CALIBRAR) | 20 cm |
| Tope del sensor trasero al retroceder | 20 cm |
| Paso entre filas (CALIBRAR) | 120 mm (12 cm) |
| Fila mínima | 50 mm |
| Giro: tolerancia / tramo fino / anticipo (CALIBRAR) | 0.04 rad (~2°) / 0.35 rad (~20°) / 0 |
| Desvío máximo de rumbo en una fila (CALIBRAR) | 0.09 rad (~5°) |
| Pausa entre maniobras | 0.3 s |
| Tiempo máximo avanzando / girando / moviendo | 30 s / 4 s / 3 s |
| Tiempo máximo de todo el recorrido | 30 min |
| Atasco: gracia / ventana / avance mínimo | 0.5 s / 0.4 s / 10 mm (0.05 rad al girar) |
| Retroceso tras un atasco | 50 mm, con límite de 0.8 s |
| Atascos seguidos tolerados (el siguiente termina el recorrido) | 3 |
| Radio del robot / ancho de la boquilla | 150 mm / 120 mm |
| Navegación: llegada / tramo recto máximo / tiempo por tramo | 80 mm / 1.5 m / 6 s |
| Navegación: intentos por zona / radio de descarte / "S" máximas | 3 / 25 cm / 60 |
| Velocidades (escala 0 a 1000) | 1000 en avance, movimiento, giro y retroceso; 500 en el giro fino (CALIBRAR) |

## Audio integrado

Las acciones de audio pasan por `roombateca_control.c` y llegan a `audio_th`:

```text
PLAY/PAUSE/STOP/NEXT/PREV/volumen
	|
	v
roombateca_control.c
	|
	v
audio_th.c -> mpg123 (salida ALSA)
```

Las pistas son `1.mp3`, `2.mp3` y `3.mp3`, en `/usr/share/roomba-disco/audio/`, y aparecen como "Pista 1", "Pista 2" y "Pista 3" en `reported.audio.tracks`. `NEXT` y `PREV` avanzan o retroceden de forma circular. Si `mpg123` no está disponible, la Lógica sigue funcionando para movimiento, sensores y mapa, pero las acciones de audio fallan sin falsear el estado reportado.

Sonidos de aviso (se reproducen sin bloquear la navegación):

| Evento | Archivo |
|---|---|
| Arranque del sistema | `arranque.mp3` |
| Cambio a modo `AUTO` | `pirin.mp3` |
| Cambio a modo `MANUAL` | `ding.mp3` |
| Obstáculo detectado | `alerta.mp3` |

Cuando aparece un obstáculo, la alerta se ejecuta solo una vez mientras el obstáculo permanezca detectado. Si la música estaba sonando:

```text
PAUSE musica
reproducir alerta.mp3 hasta terminar
RESUME musica
```

`PAUSE` conserva la posición de la pista en `mpg123`, así que la música continúa desde donde fue interrumpida. Cuando el obstáculo desaparece y vuelve a aparecer, la alerta puede dispararse de nuevo.

## Pruebas automatizadas

Las pruebas de esta carpeta se compilan con `Servidor/CMakeLists.txt` y se registran en CTest:

| Prueba | Archivo | Qué comprueba |
|---|---|---|
| `odometria` | `test_odometria.c` | Avance recto, giro sobre el eje, reinicio, configuración inválida y punteros nulos |
| `encoders_sim` | `test_encoders_sim.c` | Avance de ambas ruedas, giro con velocidades opuestas, detención de los motores y que la distancia deje de aumentar después de `STOP` |
| `mapa` | `test_mapa.c` | Celdas visitadas, observación de celdas libres y obstáculos (con el margen antes del eco), crecimiento de la grilla y su límite, celdas aspiradas, obstáculos marcados y conversión punto/celda |
| `auto` | `test_auto.c` | Pruebas unitarias de la máquina de estados (secuencia del barrido, reinicio a mitad de recorrido, atasco girando y avanzando, sensor trasero, corrección de sobregiro y de rumbo, tiempo total, fin de "S" con y sin mapa, planear e ir a una zona pendiente) y una simulación de un robot de 30 cm con la rampa de motores, el atraso del sensor y el freno, calibrada para frenar a ~5 cm como el robot real. Imprime una tabla de cobertura por escenario, sin mapa y con mapa; con `test_auto mapa` dibuja además lo aspirado |

```bash
# desde: la raíz del repositorio Roomba-Disco
cmake -S Servidor -B Servidor/build -DROOMBATECA_HARDWARE=OFF
cmake --build Servidor/build
ctest --test-dir Servidor/build --output-on-failure -R 'odometria|encoders_sim|mapa|auto'
```

La prueba de integración de todo el recorrido (WebSocket, servidor, Lógica, archivo de estado y de vuelta) y las demás herramientas de apoyo están en `Servidor/README.md`.

## Cómo correr cliente, servidor y Lógica como conjunto (en la PC)

La forma más simple es `./run-demo.sh`, desde la raíz del repositorio. Hace lo mismo que los pasos manuales de abajo y deja el panel en `http://127.0.0.1:4200/login`.

1. Compilar la Lógica y el servidor en modo simulado (el modo simulado excluye `gpio_control.c` y `libgpiod`, así que se pueden probar la comunicación, los motores simulados, los sensores y los encoders sin hardware):

```bash
# desde: la raíz del repositorio Roomba-Disco
cmake -S Servidor -B Servidor/build -DROOMBATECA_HARDWARE=OFF
cmake --build Servidor/build
```

2. Crear el usuario (solo la primera vez) y levantar la Lógica y el servidor en dos terminales, como en la sección "Ejecución". La salida de la biblioteca simulada se ve en la terminal donde corre la Lógica.

3. Correr el cliente Angular aparte:

```bash
# desde: la raíz del repositorio Roomba-Disco
npm --prefix Cliente install
npm --prefix Cliente start
```

4. Abrir `http://localhost:4200`, escribir `127.0.0.1:8080` en el campo de conexión e iniciar sesión.

Para compilar con soporte de GPIO real se usa `-DROOMBATECA_HARDWARE=ON`, en un host con `libgpiod-dev` o con el SDK de Yocto (ver `Servidor/README.md`).
