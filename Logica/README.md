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
