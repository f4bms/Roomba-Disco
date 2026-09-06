# Servidor WebSocket minimo

Servidor de prueba para el cliente Angular. No requiere CivetWeb ni bibliotecas externas: usa sockets POSIX y la biblioteca estandar de C.

## Compilar con CMake

Desde la raiz del repositorio:

```bash
cmake -S Servidor -B Servidor/build
cmake --build Servidor/build
```

El ejecutable se genera en `Servidor/build/servidor`.

Tambien se conserva un `Makefile` como atajo compatible para entornos que ya lo utilicen:

```bash
make -C Servidor
```

## Ejecutar

```bash
./Servidor/build/servidor
```

El puerto predeterminado es `8080`. Se puede cambiar como primer argumento:

```bash
./Servidor/build/servidor 9000
```

Cuando un navegador establece correctamente el handshake WebSocket, la consola muestra:

```text
cliente conectado
```

El servidor tambien muestra `cliente desconectado`, responde a `ping` con `pong` y devuelve al cliente los mensajes de texto recibidos.

Para probarlo desde la consola del navegador mientras el cliente Angular esta abierto:

```js
const socket = new WebSocket('ws://localhost:8080');
socket.onopen = () => socket.send('prueba de conexion');
socket.onmessage = event => console.log(event.data);
```
