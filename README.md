# Roomba-Disco
Primer Proyecto del curso de Sistemas Empotrados

## Arquitectura de hardware

![Diagrama de arquitectura de hardware](diagramas/arquitectura-hardware.svg)

> Fuente del diagrama: [`diagramas/arquitectura-hardware.d2`](diagramas/arquitectura-hardware.d2)
> ([D2](https://d2lang.com/)). Regenerar con:
> `d2 diagramas/arquitectura-hardware.d2 diagramas/arquitectura-hardware.svg`.

El sistema se divide en cuatro dominios:

- **Subsistema de energía.** Pack de baterías 18650 en 3S1P (11,1–12,6 V) con BMS 3S
  balanceada. De ahí salen dos rieles regulados por separado: el riel de batería
  (11,1–12,6 V) alimenta la etapa de potencia y un convertidor DC-DC deriva el riel
  lógico de 5 V. Las celdas se cargan de forma individual, fuera de línea.
- **Dominio lógico** (tierra `GND_L`). Raspberry Pi 4 con imagen mínima construida con
  Yocto; expone el acceso a hardware mediante la biblioteca de control. Cuelgan de ella
  los sensores de proximidad (≥ 2, frontal y lateral), los 4 LEDs de estado, la salida
  de audio y la odometría de los motores.
- **Barrera de aislamiento galvánico.** Optoacopladores en las 6 líneas de control del
  driver (`IN1`–`IN4`, `ENA`, `ENB`). Las tierras `GND_L` y `GND_P` se mantienen
  separadas y su único punto de cruce es el optoacoplador.
- **Dominio de potencia** (tierra `GND_P`). Driver de puente H (L298N) con control de
  velocidad por PWM en `ENA`/`ENB`, y los dos motores DC de la tracción diferencial.

### Decisiones de hardware pendientes

Las cajas y flechas punteadas del diagrama marcan puntos aún sin cerrar:

| Elemento | Pendiente |
|---|---|
| Sensores de proximidad | Tecnología: ultrasónico (HC-SR04) o infrarrojo |
| Salida de audio | Ruta: analógica (jack + amplificador) o DAC I2S |
| Odometría | Método: encoders en las ruedas o estimación por tiempo/PWM |

<!-- TODO: revisar y completar esta sección conforme se cierren las decisiones de HW. -->

