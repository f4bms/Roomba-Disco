# Simulador de Logica

Este proceso C simula temporalmente la futura capa Logica. Es el unico propietario de `estado.json`: recibe cambios deseados, valida sus valores, actualiza el estado reportado y persiste el snapshot completo.

## Contrato IPC

Usa un socket Unix, por defecto `/tmp/roomba-logica.sock`, con un documento JSON por linea.

Solicitud de lectura:

```json
{"type":"get_state"}
```

Cambio parcial solicitado por el cliente:

```json
{"type":"set_state","desired":{"mode":"AUTO","motion":{"direction":"FWD","speed":321}}}
```

La respuesta siempre es un snapshot completo con `type: "state"`. El Servidor transporta estos documentos sin implementar decisiones del robot.

## Ejecucion

Desde la raiz del repositorio, despues de compilar:

```bash
./Servidor/build-sim/logica_simulador Logica/estado.json /tmp/roomba-logica.sock
./Servidor/build-sim/servidor 8080 "$PWD/Cliente/dist/scrap-e-controller/browser" /tmp/roomba-logica.sock
```

## Que se implemento

Esta etapa prepara la logica para probar movimiento y odometria sin tener la
Raspberry Pi ni los sensores fisicos. La biblioteca se usa como una caja
negra: Logica solo llama las funciones declaradas en sus archivos `.h`.

El flujo actual es:

```text
Cliente o prueba Python
	|
	| socket Unix: JSON por linea
	v
logica.c
	|
	| roombateca_control.c
	v
libroombateca simulada
	|
	+-- motores.c
	+-- encoders.c
	+-- sensores.c
	v
odometria.c
	|
	v
reported.pose
```

## Hilo de control

`logica.c` tiene un hilo separado que se despierta cada 100 ms. Este hilo no
espera a que el cliente envie un mensaje. En cada ciclo:

1. Lee los tres sensores.
2. Lee los dos encoders.
3. Actualiza la odometria.
4. Actualiza `reported.sensors`.
5. Actualiza `reported.pose`.

El hilo que atiende el socket y el hilo de control comparten el documento JSON.
Por eso se usa `state_mutex`: antes de leer o modificar el estado, cada hilo
lo bloquea. Esto evita que un hilo lea el JSON mientras el otro lo esta
modificando.

## Simulacion de motores

En modo simulado, `motores.c` no controla GPIO. Solo guarda las velocidades
actuales de las ruedas y las muestra en la terminal:

```text
[motores] izquierdo = 50
[motores] derecho = 50
```

La velocidad simulada maxima actual es:

```text
100 por ciento = 500 mm/s
```

Este valor es temporal y sirve para poder observar movimiento. No representa
todavia la velocidad real del robot.

## Simulacion de encoders

`encoders.c` consulta las velocidades actuales de los motores y calcula cuanto
recorrio cada rueda desde la ultima lectura:

```text
distancia nueva = velocidad simulada * tiempo transcurrido
```

La distancia se acumula. Por eso una lectura puede devolver, por ejemplo:

```text
primera lectura: 0 mm
segunda lectura: 50 mm
tercera lectura: 100 mm
```

Si las dos ruedas tienen velocidad positiva, ambas distancias aumentan. Si
una rueda tiene velocidad positiva y la otra negativa, una distancia aumenta
y la otra disminuye. Esto permite simular un giro sobre el eje del robot.

Los encoders tambien entregan pulsos, velocidad y tiempo del ultimo pulso,
pero la odometria actual utiliza principalmente `distancia_mm`.

## Odometria

Los archivos de odometria son:

```text
Logica/odometria.h
Logica/odometria.c
```

La odometria mantiene tres valores:

```text
x_mm       posicion horizontal
y_mm       posicion vertical
theta_rad  orientacion
```

La configuracion actual usa temporalmente:

```text
distancia entre ruedas = 200 mm
```

El modulo recibe las distancias acumuladas de ambos encoders. Primero calcula
el cambio desde la lectura anterior:

```text
delta izquierda = distancia actual izquierda - distancia anterior izquierda
delta derecha   = distancia actual derecha - distancia anterior derecha
```

Despues calcula el avance promedio:

```text
avance = (delta izquierda + delta derecha) / 2
```

Y el cambio de orientacion:

```text
cambio de angulo = (delta derecha - delta izquierda)
		   / distancia entre ruedas
```

La posicion se actualiza usando el angulo medio del pequeno movimiento:

```text
x = x + avance * cos(angulo medio)
y = y + avance * sin(angulo medio)
```

La primera lectura no genera movimiento. Solo se guarda como referencia. Esto
evita interpretar la distancia que ya tenia el encoder al iniciar como un
movimiento nuevo.

## Estado publicado

Durante el primer ciclo, Logica agrega esta seccion al estado reportado:

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

## Pruebas automatizadas

La prueba de odometria esta en:

```text
Logica/test_odometria.c
```

Comprueba:

- avance recto;
- giro sobre el eje;
- reinicio de la odometria;
- configuracion invalida;
- punteros nulos.

La prueba del simulador esta en:

```text
Logica/test_encoders_sim.c
```

Comprueba:

- avance de ambas ruedas;
- giro con velocidades opuestas;
- detencion de los motores;
- que la distancia deje de aumentar despues de `STOP`.

Para ejecutarlas:

```bash
cmake -S Servidor -B Servidor/build-sim -DROOMBATECA_HARDWARE=OFF
cmake --build Servidor/build-sim --target test_odometria test_encoders_sim
ctest --test-dir Servidor/build-sim --output-on-failure \
  -R 'odometria|encoders_sim'
```

## Que falta

Esta etapa todavia no implementa:

- `mapa.c` y `mapa.h`;
- conversion de la pose a celdas;
- celdas visitadas y obstaculos;
- publicacion espontanea de estados al servidor;
- control autonomo;
- correccion del joystick;
- calibracion con medidas fisicas reales.

El siguiente paso es crear el modulo de mapa usando la pose ya calculada.
Despues se conectaran los sensores a las celdas libres y a las celdas con
obstaculos.
Luego abre `http://localhost:8080`. Para probar todo el recorrido automaticamente:

```bash
python3 Servidor/test_integracion.py
```

## Como correr cliente-servidor-logica-biblioteca como conjunto

1) Compilar cliente

```bash
cd Cliente
npm install
npm run build
```

2) Compilar cliente, servidor y logica en modo simulado

```bash
cd ..
npm --prefix Cliente install
npm --prefix Cliente run build
cmake -S Servidor -B Servidor/build-sim -DROOMBATECA_HARDWARE=OFF
cmake --build Servidor/build-sim --target logica_simulador servidor
```

El modo simulado excluye `gpio_control.c` y `libgpiod`, por lo que permite
probar la comunicación, los motores simulados, los sensores y los encoders sin
hardware real. La salida de la biblioteca se muestra en la terminal donde se
ejecuta Logica.

Para compilar con el soporte GPIO real, usando un host con `libgpiod-dev` o el
SDK de Yocto, se utiliza `ROOMBATECA_HARDWARE=ON`:

```bash
cmake -S Servidor -B Servidor/build -DROOMBATECA_HARDWARE=ON
cmake --build Servidor/build --target logica_simulador
```

3) Ejecutar logica(en una segunda terminal)

```bash
LD_LIBRARY_PATH=Servidor/build-sim/roombateca \
./Servidor/build-sim/logica_simulador \
Logica/estado.json \
/tmp/roomba-logica.sock
```

4) Ejecutar servidor(en una tercera terminal)

```bash
./Servidor/build-sim/servidor \
8080 \
"$PWD/Cliente/dist/scrap-e-controller/browser" \
/tmp/roomba-logica.sock
```
---
Para ejecutar el servidor localmente, primero debe estar disponible el cliente
compilado. El servidor y Logica se comunican mediante el socket Unix indicado
en ambos comandos.

5) Probar la aplicación

```bash
http://localhost:8080

```
