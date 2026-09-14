# Servidor web y WebSocket

El servidor C usa CivetWeb vendorizado para entregar el cliente Angular compilado y el canal WebSocket desde el mismo proceso. No accede al Driver ni al hardware: transporta documentos JSON entre el Cliente y la capa Logica por un socket Unix local.

## Compilar con CMake

Desde la raiz del repositorio:

```bash
cmake -S Servidor -B Servidor/build
cmake --build Servidor/build
```

El build compila el cliente Angular y genera el ejecutable en `Servidor/build/servidor`.

Tambien se conserva un `Makefile` como atajo compatible para entornos que ya lo utilicen:

```bash
make -C Servidor
```

## Ejecutar en desarrollo

```bash
./Servidor/build/logica_simulador Logica/estado.json /tmp/roomba-logica.sock
./Servidor/build/servidor 8080 "$PWD/Cliente/dist/scrap-e-controller/browser" /tmp/roomba-logica.sock
```

Abre `http://localhost:8080` en el navegador. El canal WebSocket se expone en `ws://localhost:8080/ws`.

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
