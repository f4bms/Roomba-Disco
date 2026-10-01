# Servidor web y WebSocket

El servidor C usa CivetWeb vendorizado para exponer el canal WebSocket que comunica al cliente remoto con la capa Logica. No accede al Driver ni al hardware: transporta documentos JSON entre el Cliente y la capa Logica por un socket Unix local. El cliente Angular es una aplicacion independiente que corre en otro dispositivo (laptop, PC) y se conecta por red a la Raspberry Pi.

## Compilar con CMake

Desde la raiz del repositorio:

```bash
cmake -S Servidor -B Servidor/build
cmake --build Servidor/build
```

Esto compila el servidor y `logica_simulador` sin necesidad de `npm`. El cliente ya no se compila ni se instala en la Pi. Si aun asi quieres empacar el cliente junto al servidor (despliegue monolitico opcional), activa la opcion:

```bash
cmake -S Servidor -B Servidor/build -DBUILD_CLIENT=ON
cmake --build Servidor/build
```

## Ejecutar en desarrollo

En la Raspberry Pi (o en la maquina de desarrollo) se levantan solo la logica y el servidor:

```bash
./Servidor/build/logica_simulador Logica/estado.json /tmp/roomba-logica.sock
./Servidor/build/servidor 8080 "" /tmp/roomba-logica.sock
```

El segundo argumento es la raiz web opcional; con el cliente separado puede quedar vacio. El canal WebSocket se expone en `ws://<IP_DE_LA_PI>:8080/ws`.

En el dispositivo del cliente se corre la aplicacion Angular por separado (`ng serve` o el bundle de `ng build`) y se escribe la direccion de la Pi en el campo de conexion del panel, por ejemplo `192.168.1.50:8080`.

## Instalar en la Raspberry Pi

```bash
cmake --install Servidor/build --prefix /opt/roomba-disco
/opt/roomba-disco/bin/servidor
```

La instalacion incluye el ejecutable y los assets del cliente en `share/roomba-disco/www`. El binario los localiza automaticamente. Desde otro equipo de la misma red, abre:

```text
http://IP_DE_LA_RASPBERRY:8080
```

El cliente usa el mismo origen para conectarse por WebSocket en:

```text
ws://IP_DE_LA_RASPBERRY:8080/ws
```

## Estado actual del canal

Cuando un cliente abre el WebSocket, solicita el snapshot completo a Logica. Los cambios de controles se envian como `set_state`; Logica los valida, actualiza `Logica/estado.json` y devuelve un nuevo snapshot a todos los clientes conectados.

```js
const socket = new WebSocket('ws://localhost:8080/ws');
socket.onopen = () => socket.send(JSON.stringify({ type: 'get_state' }));
socket.onmessage = event => console.log(JSON.parse(event.data));
```

La prueba automatizada del recorrido WebSocket -> Servidor -> Logica -> archivo JSON -> WebSocket se ejecuta con:

```bash
python3 Servidor/test_integracion.py
```
