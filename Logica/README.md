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
./Servidor/build/logica_simulador Logica/estado.json /tmp/roomba-logica.sock
./Servidor/build/servidor 8080 "$PWD/Cliente/dist/scrap-e-controller/browser" /tmp/roomba-logica.sock
```

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
npm start
```

2) Compilar servidor y logica

```bash
cd .. 
make -S Servidor -B Servidor/build
cmake --build Servidor/build
```

3) Ejecutar logica(en una segunda terminal)

```bash
LD_LIBRARY_PATH=Servidor/build/roombateca \
./Servidor/build/logica_simulador \
Logica/estado.json\
/tmp/roomba-logica.sock
```

4) Ejecutar servidor(en una tercera terminal)

```bash
./Servidor/build/servidor \
8080 \
"$PWD/Cliente/dist/scrap-e-controller/browser" \
/tmp/roomba-logica.sock
```
---
Tanto la ejecución y la logica se hacen de la manera anterior debido a la conexión de socket que se utiliza para su comunicación

5) Probar la aplicación

```bash
http://localhost:8080

```
