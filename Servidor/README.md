# Servidor web y WebSocket

El servidor C usa CivetWeb vendorizado para entregar el cliente Angular compilado y el canal WebSocket desde el mismo proceso. No accede al Driver ni al hardware: su rol es transportar datos entre el Cliente y la futura capa Lógica.

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
./Servidor/build/servidor 8080 "$PWD/Cliente/dist/scrap-e-controller/browser"
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

El cliente usara en el siguiente paso el mismo origen para conectarse por WebSocket en:

```text
ws://IP_DE_LA_RASPBERRY:8080/ws
```

## Estado actual del canal

Cuando un cliente abre el WebSocket, la consola muestra `cliente conectado`. Por ahora el servidor registra y devuelve mediante echo los mensajes de texto recibidos. El reenvio entre Servidor y Lógica mediante IPC, asi como el estado de sensores, LEDs, modo, mapa y audio, se implementan en pasos posteriores.

```js
const socket = new WebSocket('ws://localhost:8080');
socket.onopen = () => socket.send('prueba de conexion');
socket.onmessage = event => console.log(event.data);
```
